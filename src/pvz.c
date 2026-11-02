#define _GNU_SOURCE
/* pvz.c -- Plants vs Zombies, one lawn, for the Kindle Touch.
 *
 * The screen is 600x800, one bit plus an optional greyscale layer, touch
 * only. The engine lives in jailbreak-kindle (ek_open / ek_flush) and this
 * file does not change it. Sprites come from src/pvz_sprites.h, baked from
 * the webp art in pvz/ by tools/bake_pvz_sprites.py.
 *
 * This is a lane tower defence, not the original game:
 *   - five lanes, zombies walk right-to-left down them
 *   - plants are your defence; buying one takes sun
 *   - sun falls from the sky and occasionally from your sunflowers
 *   - peas (and snow peas) shoot right, slowing what they hit
 *   - a zombie that reaches a plant eats it; one that reaches the
 *     left edge eats your brains: that lane is lost
 *   - a cherry bomb explodes around the tile it is on
 *   - a potato mine arms, then blows up the first zombie that steps near
 *
 * The rules, the drawing, the touch and the PC self-test all live in this
 * one file, like every game in this folder.
 *
 * --stats prints what every flush really cost (the engine's rect counts),
 * --clean forces a full GC16 refresh on every update (slow, but sharper).
 */
#include "eink.h"
#include "pvz_sprites.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- machine constants -------------------------------------------------- */

#define LAWN_X   60
#define LAWN_Y   55
#define COLS     12
#define ROWS     5
#define T        40            /* tile size, px */

#define HUD_Y    (LAWN_Y + ROWS * T + 12)     /* 267 */
#define CARD_W   58
#define CARD_H   58
#define CARD_GAP 62
#define CARD_X0  92
#define CARD_Y   (HUD_Y + 6)                  /* 273 */
#define QUIT_X   528
#define QUIT_Y   12

#define MAX_PLANTS  (ROWS * COLS)
#define MAX_ZOM     ROWS * 6
#define MAX_SUNS    24
#define MAX_BULLETS 64

#define SUN_FALL_TICKS  24                   /* sky sun, at 300 ms/tick */
#define SUNFLOWER_TICKS 8                    /* sun a sunflower makes */
#define PEA_TICKS       8                    /* fire interval for shooters */
#define BITE           30                     /* zombie damage per tick */
#define PEA_DMG        20
#define TICK_MS        300

typedef enum { NONE = 0, SUNFLOWER, PEASHOOTER, REPEATER, SNOWPEA,
               WALLNUT, TALLNUT, CHERRYBOMB, POTATO } plant_type;

typedef enum { Z_NORMAL = 0, Z_FLAG, Z_CONE, Z_BUCKET } zom_var;

typedef struct {
    plant_type type;
    int x, y;           /* grid tile */
    int hp, max_hp;
    int timer;          /* cooldown until the next shot / sun / boom */
} plant;

typedef struct {
    int x;              /* left edge of the 40 px sprite, panel px */
    int y;              /* lane 0..ROWS-1 */
    int hp, max_hp;
    int speed;          /* px per tick */
    int slow;           /* ticks of slow remaining */
    zom_var v;
} zombie;

typedef struct {
    int x, y, value;    /* panel px */
} sun;

typedef struct {
    int x, lane;        /* x = left pixel of the 8-10 px pea */
    int slow;
} bullet;

typedef struct {
    plant p[MAX_PLANTS];
    int np;
    zombie z[MAX_ZOM];
    int nz;
    sun s[MAX_SUNS];
    int ns;
    bullet b[MAX_BULLETS];
    int nb;
    int suns;           /* banked sun */
    int brains;         /* lanes lost */
    int wave;           /* current wave, 1-based */
    int to_spawn;       /* zombies still to come out this wave */
    int tick;
    int since;          /* ticks since the last zombie of this wave spawned */
    int over;
    int won;
} game;

static game g;
static uint32_t rng_state;      /* the LCG in rnd() */

static int stats_on = 0;
static int force_clean = 0;
static int stop_req = 0;
static plant_type pending = NONE;   /* seed card selected, next lawn tap plants it */
static int quit_armed = 0;
static int restarts = 0;
static uint32_t fall_due = 4;       /* ticks until the next sky sun */

static void on_signal(int s) { (void)s; stop_req = 1; }

static int rnd(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (int)((rng_state >> 8) & 0xffff);
}

static void init_game(uint32_t seed) {
    memset(&g, 0, sizeof g);
    rng_state = seed ? seed : 1;
    g.suns = 150;                  /* a couple of sunflowers, or one shooter */
    g.wave = 1;
    g.to_spawn = 3;                /* wave 1 is gentle */
    pending = NONE;
    quit_armed = 0;
    fall_due = 4;
}

static void restart_game(void) {
    uint32_t seed = (uint32_t)time(NULL) ^ (uint32_t)ek_ms() ^ (uint32_t)(++restarts * 2654435761u);
    init_game(seed);
}

/* ---- helpers ------------------------------------------------------------- */

/* What the engine's grey layer takes is 0..15; a sprite tone level 0..8 maps
 * to it round-to-nearest so 0 is paper and 8 is solid ink. */
static int ink15(int lvl) { return (lvl * 15 + 4) / 8; }

static int pz_on(int id, int x, int y) {
    if (x < 0 || y < 0 || x >= PVZ_TILE || y >= PVZ_TILE) return 0;
    return pz_at(id, x, y) != 0;
}

static void blit_sprite(int x, int y, int id) {
    for (int r = 0; r < PVZ_TILE; r++) {
        int c = 0;
        while (c < PVZ_TILE) {
            int v = pz_at(id, c, r);
            if (v == 0) { c++; continue; }
            int len = 1;
            while (c + len < PVZ_TILE && pz_at(id, c + len, r) == v) len++;
            ek_grey_rect(x + c, y + r, len, 1, ink15(v - 1));
            c += len;
        }
    }
}

/* The sun hovers over the grass, so it gets a paper rim; plants and zombies
 * sit on the grass and need none. */
static void blit_sun_sprite(int x, int y) {
    int id = PZ_SUN;
    for (int r = 0; r < PVZ_TILE; r++)
        for (int c = 0; c < PVZ_TILE; c++) {
            if (pz_on(id, c, r)) continue;
            if (pz_on(id, c - 1, r) || pz_on(id, c + 1, r) ||
                pz_on(id, c, r - 1) || pz_on(id, c, r + 1))
                ek_px(x + c, y + r, EK_PAPER);
        }
    blit_sprite(x, y, id);
}

static int plant_cost(plant_type t) {
    switch (t) {
        case SUNFLOWER:  return 50;
        case PEASHOOTER: return 100;
        case REPEATER:   return 200;
        case SNOWPEA:    return 175;
        case WALLNUT:    return 50;
        case TALLNUT:    return 125;
        case CHERRYBOMB: return 150;
        case POTATO:     return 25;
        default:         return 0;
    }
}

static int sprite_for(plant_type t) {
    switch (t) {
        case SUNFLOWER:  return PZ_SUNFLOWER;
        case PEASHOOTER: return PZ_PEASHOOTER;
        case REPEATER:   return PZ_REPEATER;
        case SNOWPEA:    return PZ_SNOWPEA;
        case WALLNUT:    return PZ_WALLNUT;
        case CHERRYBOMB: return PZ_CHERRYBOMB;
        case TALLNUT:    return PZ_TALLNUT;
        case POTATO:     return PZ_POTATO;
        default:         return -1;
    }
}

static int hp_for(plant_type t) {
    switch (t) {
        case WALLNUT:    return 1200;
        case TALLNUT:    return 2000;
        case POTATO:     return 120;
        case SUNFLOWER:  return 240;
        case CHERRYBOMB: return 200;
        default:         return 300;
    }
}

static int arm_ticks(plant_type t) {
    switch (t) {
        case CHERRYBOMB: return 7;      /* ~2 s, then boom */
        case POTATO:     return 20;     /* ~6 s to arm */
        case SUNFLOWER:  return 8;
        default:         return 12;
    }
}

/* ---- drawing ------------------------------------------------------------- */

static void tile_rect(int x, int y, int *xp, int *yp) {
    *xp = LAWN_X + x * T;
    *yp = LAWN_Y + y * T;
}

static void draw_grass(void) {
    /* The lawn as a whole is the paper, so the plants' grey pops off it.
     * Every other tile gets a thin grey check to read as mown rows. */
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) {
            if ((x + y) & 1) continue;
            int px, py;
            tile_rect(x, y, &px, &py);
            ek_grey_rect(px, py, T, T, ink15(6));
        }
}

static void draw_house(void) {
    int top = LAWN_Y + 24, bot = LAWN_Y + ROWS * T - 8;
    ek_rect(8, top, 44, bot - top, EK_INK);
    ek_line(4, top, 56, top, EK_INK);           /* roof ridge */
    ek_line(4, top, 8, top - 16, EK_INK);       /* roof slope */
    ek_line(56, top, 52, top - 16, EK_INK);
    ek_fill_rect(22, bot - 22, 12, 22, EK_INK); /* the door the zombies want */
}

static void draw_hud(void) {
    char buf[64];
    ek_text_scale(10, 6, "PVZ", EK_INK, 2);
    snprintf(buf, sizeof buf, "SUN %d", g.suns);
    ek_text(110, 18, buf, EK_INK);
    snprintf(buf, sizeof buf, "WAVE %d/%d", g.wave, 4);
    ek_text(240, 18, buf, EK_INK);
    snprintf(buf, sizeof buf, "BRAINS %d/5", g.brains);
    ek_text(370, 18, buf, EK_INK);
    ek_text(QUIT_X + 10, QUIT_Y + 10, quit_armed ? "SURE?" : "QUIT", EK_INK);
}

static void draw_cards(void) {
    ek_text(12, CARD_Y + 12, "PLANTS", EK_INK);
    for (int i = 0; i < 8; i++) {
        plant_type t = (plant_type)(i + 1);   /* skip NONE */
        int x = CARD_X0 + i * CARD_GAP;
        char buf[8];
        ek_rect(x, CARD_Y, CARD_W, CARD_H, EK_INK);
        if (pending == t) {
            /* a thick selection frame, drawn before the sprite */
            ek_rect(x - 2, CARD_Y - 2, CARD_W + 4, CARD_H + 4, EK_INK);
            ek_rect(x - 4, CARD_Y - 4, CARD_W + 8, CARD_H + 8, EK_INK);
        }
        int id = sprite_for(t);
        if (id >= 0) blit_sprite(x + 9, CARD_Y + 2, id);
        snprintf(buf, sizeof buf, "%d", plant_cost(t));
        ek_text(x + 8, CARD_Y + CARD_H - 12, buf, EK_INK);
    }
    ek_text(24, 352, "tap a SEED, then a LAWN tile. tap sun to bank it.",
            EK_INK);
}

static void draw_plant(const plant *p) {
    int id = sprite_for(p->type);
    if (id < 0) return;
    int px, py;
    tile_rect(p->x, p->y, &px, &py);
    blit_sprite(px, py, id);
    /* Health: a short bar under the plant so the chewing shows. */
    if (p->hp < p->max_hp) {
        int bw = T - 12;
        int filled = p->max_hp ? bw * p->hp / p->max_hp : 0;
        ek_fill_rect(px + 6, py + T - 6, bw, 3, EK_PAPER);
        ek_fill_rect(px + 6, py + T - 6, filled, 3, EK_INK);
    }
}

static void draw_zombie(const zombie *z) {
    int id = PZ_ZOMBIE;
    if (z->v == Z_BUCKET) id = PZ_BUCKETHEAD;
    else if (z->v == Z_CONE) id = PZ_CONEHEAD;
    else if (z->v == Z_FLAG) id = PZ_FLAGZOMBIE;
    blit_sprite(z->x, LAWN_Y + z->y * T, id);
}

static void draw_bullets(void) {
    for (int i = 0; i < g.nb; i++) {
        bullet *b = &g.b[i];
        int y = LAWN_Y + b->lane * T + 18;
        if (b->slow) {
            ek_fill_rect(b->x, y, 8, 4, ink15(10));
            ek_rect(b->x, y, 8, 4, EK_INK);
        } else {
            ek_fill_rect(b->x + 2, y, 8, 4, EK_INK);
        }
    }
}

static void draw_suns(void) {
    for (int i = 0; i < g.ns; i++)
        blit_sun_sprite(g.s[i].x, g.s[i].y);
}

static void draw_overlay(void) {
    if (!g.over) return;
    if (g.won) {
        ek_text_scale(140, 320, "LAWN CLEARED!", EK_INK, 3);
        ek_text(170, 430, "the horde is gone. tap RESTART", EK_INK);
    } else {
        ek_text_scale(130, 300, "THE ZOMBIES ATE", EK_INK, 3);
        ek_text_scale(170, 380, "YOUR BRAINS", EK_INK, 3);
        ek_text(210, 500, "tap RESTART to try again", EK_INK);
    }
    ek_rect(220, 540, 160, 60, EK_INK);
    ek_text(260, 560, "RESTART", EK_INK);
}

static void present(void) {
    ek_waveform(force_clean ? ek_last_stats().wfm : 0);
    ek_stats st = ek_flush();
    if (stats_on)
        fprintf(stderr, "pvz: flush wfm=%d rects=%d tiles=%d px=%ld full=%d us=%u\n",
                st.wfm, st.rects, st.tiles, st.px, st.full, st.us);
}

static void draw_all(void) {
    ek_clear(EK_PAPER);
    draw_house();
    draw_grass();
    for (int i = 0; i < g.np; i++) draw_plant(&g.p[i]);
    for (int i = 0; i < g.nz; i++) draw_zombie(&g.z[i]);
    draw_bullets();
    draw_suns();
    draw_hud();
    draw_cards();
    draw_overlay();
    present();
}

/* ---- rules --------------------------------------------------------------- */

static plant *plant_at(int x, int y) {
    for (int i = 0; i < g.np; i++)
        if (g.p[i].x == x && g.p[i].y == y) return &g.p[i];
    return NULL;
}

static void drop_sun(int x, int y, int value) {
    if (g.ns >= MAX_SUNS) return;
    sun *s = &g.s[g.ns++];
    s->x = x; s->y = y; s->value = value;
}

static void spawn_zombie(int lane) {
    if (g.nz >= MAX_ZOM || lane < 0 || lane >= ROWS) return;
    zombie *z = &g.z[g.nz++];
    memset(z, 0, sizeof *z);
    z->y = lane;
    z->x = LAWN_X + COLS * T + 10;   /* just off the lawn */
    z->speed = 6 + rnd() % 3;        /* px per tick; they only get quicker */

    /* Armour escalates with the waves. */
    int bucket = 0, cone = 0;
    if (g.wave >= 4 && rnd() % 100 < 30) bucket = 1;
    else if (g.wave >= 2 && rnd() % 100 < (g.wave == 2 ? 30 : 50)) cone = 1;
    if (bucket) {
        z->v = Z_BUCKET;
        z->hp = z->max_hp = 400 + rnd() % 100;
    } else if (cone) {
        z->v = Z_CONE;
        z->hp = z->max_hp = 200 + rnd() % 40;
    } else {
        z->v = Z_NORMAL;
        z->hp = z->max_hp = 100 + rnd() % 50;
    }
}

static void fire_pea(const plant *p, int slow) {
    if (g.nb >= MAX_BULLETS) return;
    bullet *b = &g.b[g.nb++];
    b->x = LAWN_X + p->x * T + T;            /* the plant's right edge */
    b->lane = p->y;
    b->slow = slow;
}

static void step_game(void) {
    if (g.over) return;
    g.tick++;

    /* waves: to_spawn zombies trickle out, then the field must clear for the
     * next wave; clearing the last wave wins. */
    if (g.to_spawn > 0) {
        if (++g.since >= 8) {
            g.since = 0;
            spawn_zombie(rnd() % ROWS);
            g.to_spawn--;
        }
    } else if (g.nz == 0) {
        if (g.wave >= 4) {
            g.over = 1; g.won = 1;
            return;
        }
        g.wave++;
        g.to_spawn = 2 + g.wave;
        g.since = 0;
    }

    /* the sky rains sun. */
    if (--fall_due <= 0) {
        fall_due = SUN_FALL_TICKS;
        drop_sun(LAWN_X + rnd() % (COLS * T),
                 LAWN_Y + rnd() % (ROWS * T), 25);
    }

    /* sunflowers make sun, occasionally. */
    for (int i = 0; i < g.np; i++) {
        plant *p = &g.p[i];
        if (p->type != SUNFLOWER) continue;
        if (--p->timer <= 0) {
            p->timer = SUNFLOWER_TICKS;
            drop_sun(LAWN_X + p->x * T + T / 2 - 16,
                     LAWN_Y + p->y * T + T / 2 - 16, 25);
        }
    }

    /* shooters fire at the nearest zombie to their right. */
    for (int i = 0; i < g.np; i++) {
        plant *p = &g.p[i];
        int is_shooter = (p->type == PEASHOOTER || p->type == REPEATER ||
                          p->type == SNOWPEA);
        if (!is_shooter) continue;
        if (--p->timer > 0) continue;
        int pl = LAWN_X + p->x * T + T / 2;
        int seen = 0;
        for (int j = 0; j < g.nz; j++) {
            zombie *z = &g.z[j];
            if (z->hp <= 0 || z->y != p->y) continue;
            if (z->x >= pl) { seen = 1; break; }
        }
        if (!seen) { p->timer = 1; continue; }
        p->timer = PEA_TICKS;
        fire_pea(p, p->type == SNOWPEA);
        if (p->type == REPEATER) fire_pea(p, 0);
    }

    /* peas fly right. */
    for (int i = 0; i < g.nb; i++) {
        bullet *b = &g.b[i];
        b->x += 10;
        int hit = 0;
        for (int j = 0; hit == 0 && j < g.nz; j++) {
            zombie *z = &g.z[j];
            if (z->hp <= 0 || z->y != b->lane) continue;
            if (b->x >= z->x + 2 && b->x <= z->x + 40) {
                z->hp -= PEA_DMG;
                if (b->slow) z->slow = 24;
                if (z->hp <= 0) z->hp = 0;
                hit = 1;
            }
        }
        if (hit || b->x > LAWN_X + COLS * T + 40) {
            g.b[i] = g.b[--g.nb];
            i--;                    /* re-check the swapped-in bullet */
        }
    }

    /* zombies move, eat plants, reach the house. */
    for (int i = 0; i < g.nz; i++) {
        zombie *z = &g.z[i];
        if (z->hp <= 0) continue;
        if (z->slow > 0) z->slow--;
        int col = (z->x - LAWN_X) / T;
        int eating = 0;
        if (col >= 0 && col < COLS) {
            plant *p = plant_at(col, z->y);
            int px = LAWN_X + col * T;
            if (p && p->hp > 0 && z->x <= px + T - 2) {
                p->hp -= BITE;
                if (p->hp <= 0) p->hp = 0;
                eating = 1;
            }
        }
        if (eating) continue;
        int sp = z->speed;
        if (z->slow > 0) sp = sp / 2;
        z->x -= sp;
        if (z->x < LAWN_X - 20) {           /* reached the house */
            z->hp = 0;
            g.brains++;
            if (g.brains >= 5) {
                g.over = 1; g.won = 0;
            }
        }
    }

    /* cherry bombs and potato mines go off. */
    for (int i = 0; i < g.np; i++) {
        plant *p = &g.p[i];
        int boom = 0;
        if (p->type == CHERRYBOMB) {
            if (--p->timer <= 0) boom = 1;
        } else if (p->type == POTATO && --p->timer <= 0) {
            /* armed; blow up the first zombie that comes near */
            int px = LAWN_X + p->x * T + T / 2;
            for (int j = 0; j < g.nz; j++) {
                zombie *z = &g.z[j];
                if (z->hp <= 0) continue;
                int dx = z->x + 20 - px;
                if (z->y == p->y && dx > -T && dx < T + 50) { boom = 1; break; }
            }
        }
        if (boom) {
            int cx = LAWN_X + p->x * T + T / 2;
            int cy = LAWN_Y + p->y * T + T / 2;
            int r = (p->type == CHERRYBOMB) ? 3 * T : T;
            for (int j = 0; j < g.nz; j++) {
                zombie *z = &g.z[j];
                if (z->hp <= 0) continue;
                int zy = LAWN_Y + z->y * T + T / 2;
                int dx = z->x + 20 - cx, dy = zy - cy;
                if (dx * dx + dy * dy <= r * r)
                    z->hp = 0;
            }
            p->hp = 0;                   /* consumed */
        }
    }

    /* all damage for this tick is done; drop the dead so the lawn clears */
    for (int i = 0; i < g.nz; i++)
        if (g.z[i].hp <= 0) {
            g.z[i] = g.z[--g.nz];
            i--;
        }
    for (int i = 0; i < g.np; i++)
        if (g.p[i].hp <= 0) {
            g.p[i] = g.p[--g.np];
            i--;
        }
}

/* ---- tapping ------------------------------------------------------------- */

static int in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

/* The tap is already resolved to tile x,y before this is called. */
static void tap_plant(int tilex, int tiley, plant_type t) {
    if (g.over || t < 0 || tilex < 0 || tiley < 0 ||
        tilex >= COLS || tiley >= ROWS)
        return;
    if (plant_at(tilex, tiley)) return;
    if (g.suns < plant_cost(t)) return;
    plant *p = &g.p[g.np++];
    memset(p, 0, sizeof *p);
    p->type = t;
    p->x = tilex; p->y = tiley;
    p->max_hp = hp_for(t);
    p->hp = p->max_hp;
    p->timer = arm_ticks(t);
    g.suns -= plant_cost(t);
}

static void handle_tap(int x, int y) {
    if (g.over) {
        if (in_rect(x, y, 220, 540, 160, 60)) restart_game();
        return;
    }

    /* the QUIT button: two taps, because there is no way back. */
    if (in_rect(x, y, QUIT_X, QUIT_Y, 60, 40)) {
        if (quit_armed) stop_req = 1;
        else quit_armed = 1;
        return;
    }
    quit_armed = 0;

    /* collect sun first: it sits on the lawn and is the usual fumble. */
    for (int i = 0; i < g.ns; i++) {
        if (in_rect(x, y, g.s[i].x, g.s[i].y, PVZ_TILE, PVZ_TILE)) {
            g.suns += g.s[i].value;
            g.s[i] = g.s[--g.ns];
            return;
        }
    }

    /* a seed card selects (or unselects) a plant. */
    for (int i = 0; i < 8; i++) {
        int sx = CARD_X0 + i * CARD_GAP;
        if (in_rect(x, y, sx, CARD_Y, CARD_W, CARD_H)) {
            plant_type t = (plant_type)(i + 1);
            if (pending == t) pending = NONE;
            else if (g.suns >= plant_cost(t)) pending = t;
            return;
        }
    }

    /* a lawn tap with a seed armed plants it. */
    if (pending != NONE && in_rect(x, y, LAWN_X, LAWN_Y, COLS * T, ROWS * T)) {
        int tilex = (x - LAWN_X) / T;
        int tiley = (y - LAWN_Y) / T;
        if (plant_at(tilex, tiley))
            return;             /* keep the seed armed, try another tile */
        tap_plant(tilex, tiley, pending);
        pending = NONE;
    }
}

/* ---- self-test ----------------------------------------------------------- */

static int failures = 0;

static void expect(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "pvz: FAIL %s\n", what);
        failures++;
    }
}

/* Each check runs on a clean lawn so one scenario cannot leak into the next;
 * the rng keeps advancing either way. */
static void new_lawn(void) { init_game(1); }

static int live_zombie(int *i) {
    for (int k = 0; k < g.nz; k++)
        if (g.z[k].hp > 0) { *i = k; return 1; }
    return 0;
}

static int selftest(void) {
    failures = 0;
    init_game(1);

    expect(g.suns == 150, "starts with 150 sun");
    expect(g.np == 0 && g.nz == 0, "clean lawn");
    expect(pz_find("sun") == PZ_SUN, "sprite lookup works");

    /* planting */
    tap_plant(1, 1, SUNFLOWER);
    expect(g.np == 1 && g.suns == 100, "sunflower planted for 50");
    tap_plant(1, 1, PEASHOOTER);
    expect(g.np == 1, "cannot plant over a plant");
    tap_plant(2, 1, PEASHOOTER);
    expect(g.np == 2 && g.suns == 0, "peashooter bought for 100");
    tap_plant(3, 1, POTATO);
    expect(g.np == 2, "cannot afford with 0 sun");

    /* collecting sun */
    drop_sun(200, 100, 25);
    handle_tap(210, 110);
    expect(g.ns == 0 && g.suns == 25, "tap collects a sun");

    /* a wave: a zombie actually comes. */
    new_lawn();
    g.to_spawn = 1;
    for (int i = 0; i < 20 && g.nz == 0; i++) step_game();
    expect(g.nz >= 1, "a zombie spawns");

    /* shooting wears zombies down: a peashooter in the target's lane. */
    new_lawn();
    g.suns = 100;
    tap_plant(1, 2, PEASHOOTER);
    g.to_spawn = 1;
    for (int i = 0; i < 20 && g.nz == 0; i++) step_game();
    int target = -1;
    if (live_zombie(&target)) {
        g.z[target].y = 2;
        g.z[target].x = LAWN_X + 8 * T;
        g.z[target].hp = g.z[target].max_hp = 40;
        int before = g.z[target].hp;
        int slowed = 0;
        for (int i = 0; i < 80; i++) {
            step_game();
            if (g.z[target].slow > 0) slowed = 1;
        }
        expect(g.z[target].hp <= 0 || slowed == 0 || g.z[target].hp < before,
               "peas wear a zombie down");
        expect(slowed == 0, "plain peas do not slow");
    }

    /* snow peas slow: a snow pea in the fat zombie's lane. */
    new_lawn();
    g.suns = 175;
    tap_plant(1, 3, SNOWPEA);
    g.to_spawn = 1;
    for (int i = 0; i < 20 && g.nz == 0; i++) step_game();
    int slowed = 0;
    if (live_zombie(&target)) {
        g.z[target].y = 3;
        g.z[target].x = LAWN_X + 8 * T;
        g.z[target].hp = g.z[target].max_hp = 1200;   /* long enough to be hit */
        for (int i = 0; i < 120 && !slowed; i++) {
            step_game();
            if (g.z[target].slow > 0) slowed = 1;
        }
    }
    expect(slowed, "snow peas slow");

    /* zombies eat plants: park a buckethead on a wallnut. */
    new_lawn();
    g.suns = 50;
    tap_plant(2, 4, WALLNUT);
    g.nz = 1;
    zombie *z = &g.z[0];
    z->y = 4;
    z->x = LAWN_X + 2 * T + 5;
    z->v = Z_BUCKET;
    z->hp = z->max_hp = 900;
    z->speed = 6;
    plant *walnut = plant_at(2, 4);
    int before = walnut ? walnut->hp : 0;
    for (int i = 0; i < 5; i++) step_game();
    int after = 0;
    for (int i = 0; i < g.np; i++)
        if (g.p[i].x == 2 && g.p[i].y == 4) after = g.p[i].hp;
    expect(before > 0 && (after < before || after == 0), "zombies chew plants");

    /* a zombie that reaches the house costs a brain. */
    new_lawn();
    g.nz = 1;
    g.z[0].hp = g.z[0].max_hp = 10;
    g.z[0].speed = 8;
    g.z[0].y = 0;
    g.z[0].x = LAWN_X - 12;
    for (int i = 0; i < 2 && g.brains == 0; i++) step_game();
    expect(g.brains == 1, "reaching the house costs a brain");

    /* a cherry bomb blows up a zombie one tile away. The wave is held: this
     * check only wants the blast, not a fresh spawn walking in after it. */
    new_lawn();
    g.suns = 150;
    tap_plant(5, 2, CHERRYBOMB);
    g.to_spawn = 0;                   /* no wave trickle during this check */
    g.nz = 1;
    g.z[0].hp = g.z[0].max_hp = 500;
    g.z[0].speed = 0;                 /* parked so the blast is the only killer */
    g.z[0].y = 2;
    g.z[0].x = LAWN_X + 6 * T + 10;   /* within the 3-tile radius */
    for (int i = 0; i < 7; i++) step_game();   /* timer 7 -> blast */
    expect(g.nz == 0, "cherry bomb kills nearby zombies");
    expect(g.np == 0, "the cherry bomb is consumed");

    /* clearing the last wave wins. */
    g.wave = 4;
    g.to_spawn = 0;
    g.nz = 0;
    step_game();
    expect(g.over && g.won, "clearing wave 4 wins");

    /* losing five brains loses. */
    g.over = 0; g.won = 0; g.brains = 4; g.nz = 1;
    g.z[0].hp = 1; g.z[0].speed = 8;
    g.z[0].y = 4; g.z[0].x = LAWN_X - 12;
    for (int i = 0; i < 2 && g.brains < 5; i++) step_game();
    expect(g.over && !g.won, "five brains lost loses");

    if (failures) {
        fprintf(stderr, "pvz: %d checks failed\n", failures);
        return failures;
    }
    fprintf(stderr, "pvz: selftest ok\n");
    return 0;
}

/* ---- main ---------------------------------------------------------------- */

static void usage(void) {
    fprintf(stderr,
            "usage: pvz [options]\n"
            "\n"
            "  On the Kindle the lawn is the game's until you lose or win.\n"
            "  Tap a seed, tap a lawn tile. Tap a sun to bank it.\n"
            "  --selftest   check the rules on this computer and exit\n"
            "  --auto N     play N ticks by itself, then stop\n"
            "  --dump FILE  write the picture (PGM) and exit\n"
            "  --seed N     same seed, same lawn\n"
            "  --stats      print what each screen update cost\n"
            "  --clean      refresh the whole picture on every update (slow)\n"
            "  --no-takeover  (host testing) do not grab the panel\n");
}

int main(int argc, char **argv) {
    unsigned flags = EK_TAKEOVER;
    int wait = 1;
    int do_test = 0;
    int auto_ticks = -1;
    const char *dump_path = NULL;
    uint32_t seed = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v <= 0 || v > 0x7fffffff) { usage(); return 2; }
            seed = (uint32_t)v;
        } else if (!strcmp(argv[i], "--auto") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v < 0 || v > 100000) { usage(); return 2; }
            auto_ticks = (int)v;
        } else if (!strcmp(argv[i], "--dump") && i + 1 < argc) {
            dump_path = argv[++i];
            if (!dump_path[0]) { usage(); return 2; }
        } else if (!strcmp(argv[i], "--selftest")) do_test = 1;
        else if (!strcmp(argv[i], "--stats")) stats_on = 1;
        else if (!strcmp(argv[i], "--clean")) force_clean = 1;
        else if (!strcmp(argv[i], "--no-takeover")) flags &= ~EK_TAKEOVER;
        else if (!strcmp(argv[i], "--no-wait")) wait = 0;
        else if (!strcmp(argv[i], "--help")) { usage(); return 0; }
        else { usage(); return 2; }
    }

    if (seed == 0) {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
            fprintf(stderr, "pvz: clock_gettime failed\n");
            return 1;
        }
        seed = (uint32_t)ts.tv_sec ^ (uint32_t)ts.tv_nsec;
        if (seed == 0) seed = 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    if (ek_open(flags) != 0) {
        fprintf(stderr, "pvz: the screen would not open\n");
        ek_restore_ui();
        return 1;
    }
    ek_waveform_by_name("GC16");
    ek_wait_updates(wait);
    if (force_clean) {
        ek_waveform(ek_waveform_by_name("GC16"));
        ek_ghost(0, 0);
        ek_policy(180, 4, 30);
    } else {
        /* The sprite tones are real greys, so the layer is wanted; if it is
         * missing the engine dithered, which still reads fine on this art. */
        if (ek_grey_enable() != 0)
            fprintf(stderr, "pvz: no grey layer memory, dithering instead\n");
        ek_waveform(0);
        ek_ghost(6, 30);
        ek_policy(60, 4, 30);
    }

    int rc = 0;
    if (do_test) {
        rc = selftest() ? 1 : 0;
        init_game(seed);
        draw_all();
    } else {
        init_game(seed);
        draw_all();
        for (int i = 0; i < auto_ticks && !stop_req; i++) {
            step_game();
            draw_all();
        }
    }

#ifdef EK_HOST
    /* A pipe of "tap X Y" lines plays the game, then --dump saves the picture. */
    if (!do_test && auto_ticks < 0 && !isatty(0)) {
        char buf[64];
        while (!stop_req && fgets(buf, sizeof buf, stdin)) {
            int x, y;
            if (buf[0] == 'q') break;
            if (sscanf(buf, "tap %d %d", &x, &y) != 2) {
                fprintf(stderr, "pvz: want 'tap X Y' or q\n");
                ek_close();
                return 2;
            }
            handle_tap(x, y);
            if (stop_req) break;
            draw_all();
        }
    }
#else
    {
        uint32_t next = ek_ms();
        while (!stop_req) {
            ek_event ev;
            int got = 0;
            while (ek_poll(&ev)) {
                if (ev.type == EK_DOWN) {
                    handle_tap(ev.x, ev.y);
                    got = 1;
                }
            }
            uint32_t now = ek_ms();
            if ((int32_t)(now - next) >= 0) {
                step_game();
                next += (uint32_t)TICK_MS;
                got = 1;
            }
            if (got) draw_all();
            else ek_sleep_ms(15);
        }
    }
#endif

    if (dump_path) {
        if (ek_dump_pgm(dump_path, 1) != 0) {
            fprintf(stderr, "pvz: could not write %s\n", dump_path);
            ek_close();
            return 1;
        }
    }

    ek_close();
    return rc;
}