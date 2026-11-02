#define _GNU_SOURCE
/* xcom.c -- a one-mission turn-based fight for the Kindle Touch.
 *
 * Screen is 600x800, one bit, touch. Drawing goes through the e-ink engine
 * in jailbreak-kindle (ek_open / ek_flush). This file does not change that
 * engine. Sprites come from src/sprites.h, cut from the sheet in the repo.
 *
 * You get two actions: walk, and shoot. A long run spends both, so you
 * cannot shoot after it. Aliens move when you tap END. QUIT gives the
 * Kindle its book screen back.
 */
#include "eink.h"
#include "sprites.h"
#include "audio.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#define COLS 12
#define ROWS 13
#define TILE SP_TILE
#define MAP_X 60
#define MAP_Y 36

#define INF 999

enum { ST_PLAY, ST_FIRE, ST_HEAL, ST_QUIT };

static const char MAP[ROWS][COLS + 1] = {
    "##.FF..R..F#",
    "#..F..r....#",
    "..R..C...T..",
    ".F..R.....F.",
    "FF...~~~~..F",
    "...r.~~~~C..",
    ".T..T=====..",
    "....F=====FR",
    "C......r....",
    "..F......T..",
    "..FF.C......",
    ".r.......FF.",
    "............",
};

enum { TEAM_YOU, TEAM_THEM, MAX_YOU = 4, MAX_THEM = 5 };

typedef struct {
    int sp;
    int x, y;
    int hp, max_hp;
    int aim, budget, armor, dlo, dhi;
    int range, sight, steady_bonus;
    int heal_hp, heal_left;
    int flying, melee, suppress, panic_pct;
    int can_move, can_shoot, moved;
    int overwatch, hunker, suppressed, skip_turn;
    int alive, team;
    char name[16];
} unit;

static unit you[MAX_YOU];
static unit them[MAX_THEM];
static int n_you, n_them;

static int gdist[ROWS][COLS];
static int gpar[ROWS][COLS];
static unsigned char seen[ROWS][COLS];
static unsigned char vis[ROWS][COLS];
static unsigned char reach[ROWS][COLS]; /* 1 walk, 2 run */
static int spr_of[128];

static int turn_no = 1;
static uint32_t game_seed = 1;
static int sel = -1;
static int mode = ST_PLAY;
static int fire_at = -1;
static int battle = 0; /* 0 playing, 1 you win, 2 you lose */
static uint32_t rng_state = 1;
static volatile sig_atomic_t stop_req;
static int wfm_clean;    /* the GC16 number, for a frame that must look clean */
static int stats_on;     /* --stats: say what each flush cost */
static int force_clean;  /* --clean: every frame a full GC16 refresh */
static int clean_next;   /* this frame is a big change, so clean it */

static char logs[8][80];
static int nlog;

#ifndef EK_HOST
static int have_down, down_x, down_y;
#endif

static void say(const char *fmt, ...);
static void check_end(void);
static void recompute_vision(void);
static void resolve_shot(unit *a, unit *t);
static int walk_to(unit *u, int tx, int ty, int block_hidden);

static void on_signal(int sig) {
    (void)sig;
    stop_req = 1;
}

static uint32_t rnd_u(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (x == 0) {
        fprintf(stderr, "xcom: random number generator hit 0\n");
        exit(1);
    }
    rng_state = x;
    return x;
}

static int rnd_int(int lo, int hi) {
    if (hi < lo) {
        fprintf(stderr, "xcom: bad dice %d..%d\n", lo, hi);
        exit(1);
    }
    if (hi == lo) return lo;
    return lo + (int)(rnd_u() % (uint32_t)(hi - lo + 1));
}

static void say(const char *fmt, ...) {
    char buf[80];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) {
        fprintf(stderr, "xcom: could not format a message\n");
        exit(1);
    }
    if (nlog < 8) {
        memcpy(logs[nlog], buf, sizeof buf);
        nlog++;
    } else {
        memmove(logs, logs + 1, 7 * 80);
        memcpy(logs[7], buf, sizeof buf);
    }
    fprintf(stderr, "xcom: %s\n", buf);
    audio_line(buf);   /* the same words, out of the speaker */
}

static int in_map(int x, int y) {
    return x >= 0 && y >= 0 && x < COLS && y < ROWS;
}

static int cheb(int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    return dx > dy ? dx : dy;
}

/* What it costs to step onto this square. 0 means you cannot.
 * A flier ignores the ground. A bad map character is a bug, not a wall. */
static int step_cost(const unit *u, int x, int y) {
    if (!in_map(x, y)) return 0;
    if (u->flying) return 1;
    char t = MAP[y][x];
    switch (t) {
    case '.':
    case '=':
    case 'B':
        return 1;
    case 'F':
    case 'r':
        return 2;
    case '#':
    case '~':
    case 'R':
    case 'T':
    case 'C':
    case 'M':
        return 0;
    default:
        fprintf(stderr, "xcom: bad map square '%c' at %d,%d\n", t, x, y);
        exit(1);
    }
}

static int blocks_sight(char t) {
    return t == '#' || t == 'R' || t == 'T' || t == 'M';
}

static int los(int x0, int y0, int x1, int y1) {
    if (!in_map(x0, y0) || !in_map(x1, y1)) return 0;
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int x = x0, y = y0;
    if (x == x1 && y == y1) return 1;
    if (dx >= dy) {
        int err = 0;
        while (x != x1) {
            x += sx;
            err += dy;
            if (err >= dx) {
                y += sy;
                err -= dx;
            }
            if (x == x1 && y == y1) return 1;
            if (!in_map(x, y) || blocks_sight(MAP[y][x])) return 0;
        }
    } else {
        int err = 0;
        while (y != y1) {
            y += sy;
            err += dx;
            if (err >= dy) {
                x += sx;
                err -= dy;
            }
            if (x == x1 && y == y1) return 1;
            if (!in_map(x, y) || blocks_sight(MAP[y][x])) return 0;
        }
    }
    return 1;
}

static unit *unit_index(int i) {
    if (i < 0 || i >= n_you + n_them) {
        fprintf(stderr, "xcom: unit %d is out of range\n", i);
        exit(1);
    }
    return i < n_you ? &you[i] : &them[i - n_you];
}

static int occupied(int x, int y, const unit *self, int block_hidden) {
    int n = n_you + n_them;
    for (int i = 0; i < n; i++) {
        unit *o = unit_index(i);
        if (!o->alive || o == self) continue;
        if (o->x != x || o->y != y) continue;
        if (!block_hidden && o->team == TEAM_THEM && !vis[y][x]) return 0;
        return 1;
    }
    return 0;
}

static void route_from(const unit *u, int x0, int y0, int block_hidden) {
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) {
            gdist[y][x] = INF;
            gpar[y][x] = -1;
        }
    if (!in_map(x0, y0)) return;
    gdist[y0][x0] = 0;
    static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    unsigned char done[ROWS][COLS];
    memset(done, 0, sizeof done);
    int steps = 0;
    for (;;) {
        int bx = -1, by = -1, bd = INF;
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < COLS; x++)
                if (!done[y][x] && gdist[y][x] < bd) {
                    bd = gdist[y][x];
                    bx = x;
                    by = y;
                }
        if (bx < 0) break;
        if (++steps > COLS * ROWS) {
            fprintf(stderr, "xcom: path search did not finish\n");
            exit(1);
        }
        done[by][bx] = 1;
        for (int k = 0; k < 8; k++) {
            int nx = bx + DX[k], ny = by + DY[k];
            int c = step_cost(u, nx, ny);
            if (c == 0) continue;
            if (occupied(nx, ny, u, block_hidden)) continue;
            int nd = gdist[by][bx] + c;
            if (nd < gdist[ny][nx]) {
                gdist[ny][nx] = nd;
                gpar[ny][nx] = by * COLS + bx;
            }
        }
    }
}

static int unwind(int x0, int y0, int x1, int y1, int *xs, int *ys) {
    if (gdist[y1][x1] >= INF) return -1;
    int n = 0;
    int x = x1, y = y1;
    int guard = COLS * ROWS + 1;
    while (!(x == x0 && y == y0)) {
        if (n >= COLS * ROWS || --guard < 0 || gpar[y][x] < 0) {
            fprintf(stderr, "xcom: broken path to %d,%d\n", x1, y1);
            exit(1);
        }
        xs[n] = x;
        ys[n] = y;
        n++;
        int p = gpar[y][x];
        x = p % COLS;
        y = p / COLS;
    }
    for (int i = 0; i < n / 2; i++) {
        int tx = xs[i], ty = ys[i];
        xs[i] = xs[n - 1 - i];
        ys[i] = ys[n - 1 - i];
        xs[n - 1 - i] = tx;
        ys[n - 1 - i] = ty;
    }
    return n;
}

/* Cover the square gives you. A wall helps only when it sits between you
 * and the shooter. Standing in trees always helps. A flier has no cover. */
static int cover_of(const unit *t, int ax, int ay) {
    if (t->flying) return 0;
    char here = MAP[t->y][t->x];
    int innate = (here == 'F') ? 40 : (here == 'r' || here == 'B') ? 20 : 0;
    if (cheb(t->x, t->y, ax, ay) <= 1) return innate;
    int sx = ax > t->x ? 1 : ax < t->x ? -1 : 0;
    int sy = ay > t->y ? 1 : ay < t->y ? -1 : 0;
    int adj = 0;
    if (sx && in_map(t->x + sx, t->y)) {
        char c = MAP[t->y][t->x + sx];
        if (c == '#' || c == 'R' || c == 'T' || c == 'C' || c == 'M') adj = 40;
    }
    if (sy && in_map(t->x, t->y + sy)) {
        char c = MAP[t->y + sy][t->x];
        if ((c == '#' || c == 'R' || c == 'T' || c == 'C' || c == 'M') && 40 > adj)
            adj = 40;
    }
    return innate > adj ? innate : adj;
}

static int hit_chance_at(const unit *a, int ax, int ay, int moved, const unit *t) {
    if (!a->alive || !t->alive) return 0;
    int dist = cheb(ax, ay, t->x, t->y);
    if (dist < 1 || dist > a->range) return 0;
    if (!a->flying && !los(ax, ay, t->x, t->y)) return 0;
    int aim = a->aim;
    if (!moved) aim += a->steady_bonus;
    int pen = dist > 2 ? (dist - 2) * 6 : 0;
    int def = cover_of(t, ax, ay);
    if (t->hunker) def += 40;
    int h = aim - pen - def;
    if (h < 5) h = 5;
    if (h > 95) h = 95;
    return h;
}

static void recompute_vision(void) {
    memset(vis, 0, sizeof vis);
    for (int i = 0; i < n_you; i++) {
        unit *u = &you[i];
        if (!u->alive) continue;
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < COLS; x++) {
                if (cheb(u->x, u->y, x, y) > u->sight) continue;
                if (!los(u->x, u->y, x, y)) continue;
                vis[y][x] = 1;
                seen[y][x] = 1;
            }
    }
}

static void check_end(void) {
    int ys = 0, ts = 0;
    for (int i = 0; i < n_you; i++) if (you[i].alive) ys++;
    for (int i = 0; i < n_them; i++) if (them[i].alive) ts++;
    int was = battle;
    if (ys == 0) battle = 2;
    else if (ts == 0) battle = 1;
    if (battle && battle != was) {
        audio_effect(battle == 1 ? "win" : "lose");
        clean_next = 1;
    }
}

static void on_downed(unit *t) {
    t->alive = 0;
    t->hp = 0;
    audio_effect("down");
    say("%s is down", t->name);
    check_end();
}

static void resolve_shot(unit *a, unit *t) {
    int chance = hit_chance_at(a, a->x, a->y, a->moved, t);
    if (chance <= 0) {
        fprintf(stderr, "xcom: %s shot %s with no chance\n", a->name, t->name);
        exit(1);
    }
    audio_effect("shot");
    int roll = rnd_int(1, 100);
    if (roll > chance) {
        audio_effect("miss");
        say("%s missed %s %d%% (%d)", a->name, t->name, chance, roll);
        return;
    }
    int crit = rnd_int(1, 100) <= 10;
    int dmg = rnd_int(a->dlo, a->dhi) + (crit ? 2 : 0) - t->armor;
    if (dmg < 0) dmg = 0;
    if (dmg == 0) {
        audio_effect("hit");
        say("%s soaked %s %d%% (%d)", t->name, a->name, chance, roll);
    } else {
        t->hp -= dmg;
        if (t->hp < 0) t->hp = 0;
        audio_effect("hit");
        say("%s %s %s %d%% (%d) for %d", a->name, crit ? "crit" : "hit",
            t->name, chance, roll, dmg);
        if (t->hp == 0) on_downed(t);
    }
    if (!t->alive) return;
    if (a->panic_pct && rnd_int(1, 100) <= a->panic_pct) {
        t->skip_turn = 1;
        say("%s panics", t->name);
    }
    if (a->suppress) {
        t->suppressed = 1;
        int n = n_you + n_them;
        for (int i = 0; i < n; i++) {
            unit *o = unit_index(i);
            if (!o->alive || o == t || o->team != t->team) continue;
            if (cheb(o->x, o->y, t->x, t->y) <= 1) o->suppressed = 1;
        }
        say("%s is pinned", t->name);
    }
}

static int overwatch_fire(unit *mover) {
    for (int i = 0; i < n_you; i++) {
        unit *s = &you[i];
        if (!s->alive || !s->overwatch) continue;
        if (hit_chance_at(s, s->x, s->y, s->moved, mover) <= 0) continue;
        s->overwatch = 0;
        resolve_shot(s, mover);
        if (!mover->alive) return 0;
    }
    return 1;
}

static int walk_to(unit *u, int tx, int ty, int block_hidden) {
    route_from(u, u->x, u->y, block_hidden);
    int xs[COLS * ROWS], ys[COLS * ROWS];
    int n = unwind(u->x, u->y, tx, ty, xs, ys);
    if (n < 0) return 0;
    int sx = u->x, sy = u->y;
    for (int i = 0; i < n; i++) {
        /* A hidden alien still stops you. The picture hid them; the square did not. */
        if (occupied(xs[i], ys[i], u, 1)) {
            say("something is in the way");
            break;
        }
        u->x = xs[i];
        u->y = ys[i];
        if (u->team == TEAM_THEM && !overwatch_fire(u)) return 0;
    }
    if (u->x == sx && u->y == sy) return 0;
    int cost = gdist[u->y][u->x];
    u->can_move = 0;
    u->moved = 1;
    if (cost > u->budget) u->can_shoot = 0;
    recompute_vision();
    return 1;
}

static void ready_unit(unit *u) {
    u->moved = 0;
    u->hunker = 0;
    u->overwatch = 0;
    if (!u->alive) return;
    if (u->skip_turn) {
        u->skip_turn = 0;
        u->can_move = 0;
        u->can_shoot = 0;
        say("%s freezes", u->name);
        return;
    }
    u->can_move = 1;
    u->can_shoot = 1;
    if (u->suppressed) {
        u->suppressed = 0;
        u->can_shoot = 0;
        say("%s is pinned", u->name);
    }
}

static int nearest_foe(int x, int y, unit *foes, int nf) {
    int best = INF;
    for (int i = 0; i < nf; i++) {
        if (!foes[i].alive) continue;
        int d = cheb(x, y, foes[i].x, foes[i].y);
        if (d < best) best = d;
    }
    return best;
}

static void act_ai(unit *u) {
    if (!u->alive || battle) return;
    if (!u->can_move && !u->can_shoot) return;
    if (u->team == TEAM_YOU) recompute_vision();
    unit *foes = u->team == TEAM_YOU ? them : you;
    int nf = u->team == TEAM_YOU ? n_them : n_you;
    int block = u->team == TEAM_YOU ? 0 : 1;
    route_from(u, u->x, u->y, block);
    int dash = u->budget + u->budget / 2;
    int best = -1000000;
    int bx = u->x, by = u->y, bt = -1;
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            int d = gdist[y][x];
            if (d >= INF) continue;
            int actions;
            if (d == 0) actions = 0;
            else if (d <= u->budget && u->can_move) actions = 1;
            else if (d <= dash && u->can_move && u->can_shoot) actions = 2;
            else continue;
            int can_fire = u->can_shoot && actions < 2;
            int local = -1, local_h = 0;
            if (can_fire) {
                for (int fi = 0; fi < nf; fi++) {
                    if (!foes[fi].alive) continue;
                    if (u->team == TEAM_YOU && !vis[foes[fi].y][foes[fi].x]) continue;
                    int h = hit_chance_at(u, x, y, d > 0, &foes[fi]);
                    if (h > local_h) {
                        local_h = h;
                        local = fi;
                    }
                }
            }
            int score;
            if (local >= 0) score = local_h * 100 - actions * 40 - d;
            else {
                if (d > 0 && !u->can_move) continue;
                int nd = nearest_foe(x, y, foes, nf);
                if (nd >= INF) continue;
                score = 300 - nd * 20 - d;
            }
            if (score > best) {
                best = score;
                bx = x;
                by = y;
                bt = local;
            }
        }
    }
    if (bx != u->x || by != u->y) {
        if (!walk_to(u, bx, by, block) || !u->alive) return;
    }
    if (bt < 0 || !u->alive || !u->can_shoot || battle) return;
    unit *target = &foes[bt];
    if (!target->alive) return;
    if (hit_chance_at(u, u->x, u->y, u->moved, target) <= 0) return;
    u->can_shoot = 0;
    resolve_shot(u, target);
}

static void end_turn(void) {
    if (battle) return;
    audio_effect("turn");
    sel = -1;
    mode = ST_PLAY;
    fire_at = -1;
    for (int i = 0; i < n_them && !battle; i++) {
        ready_unit(&them[i]);
        act_ai(&them[i]);
    }
    if (!battle) {
        turn_no++;
        for (int i = 0; i < n_you; i++) ready_unit(&you[i]);
    }
    recompute_vision();
    clean_next = 1;   /* most of the map moved; give it a full refresh */
}

static void tune(unit *u) {
    const sp_info_t *s = &sp_info[u->sp];
    const char *id = sp_id[u->sp];
    u->hp = u->max_hp = s->hp;
    u->aim = s->aim;
    u->budget = s->mv;
    u->armor = s->armor;
    u->dlo = s->dlo;
    u->dhi = s->dhi;
    u->heal_hp = s->heal_hp;
    u->heal_left = s->heal_n;
    u->flying = (s->flags & SPF_FLY) ? 1 : 0;
    u->melee = (s->flags & SPF_MELEE) ? 1 : 0;
    /* The sheet says a stationary shot is better. It does not say by how
     * much. +20 aim is this mission's number. */
    u->steady_bonus = (s->flags & SPF_STEADY) ? 20 : 0;
    /* The sheet does not list weapon range. These are this mission's. */
    u->range = 8;
    u->sight = 8;
    u->panic_pct = 0;
    u->suppress = 0;
    if (strstr(id, "/sniper")) u->range = 11;
    else if (strstr(id, "/heavy")) {
        u->range = 7;
        u->suppress = 1;
    } else if (strstr(id, "/medic")) u->range = 7;
    else if (strstr(id, "/mutant")) u->range = 1;
    else if (strstr(id, "/drone")) {
        u->range = 6;
        u->sight = 9;
    } else if (strstr(id, "/assault")) u->range = 7;
    else if (strstr(id, "/sectoid")) {
        u->range = 7;
        u->panic_pct = 20;
    }
    if (s->hp <= 0 || s->dhi < s->dlo || u->budget <= 0 || u->range <= 0) {
        fprintf(stderr, "xcom: bad stats for %s\n", id);
        exit(1);
    }
}

static void name_from(char *dst, const char *label) {
    int i = 0;
    for (; label[i] && i < 15; i++) {
        char c = label[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        dst[i] = c;
    }
    dst[i] = 0;
}

static const struct { const char *id; int x, y; } SQUAD[] = {
    {"player_units/soldier", 1, 12},
    {"player_units/sniper", 4, 12},
    {"player_units/heavy", 7, 12},
    {"player_units/medic", 10, 12},
};
static const struct { const char *id; int x, y; } ALIENS[] = {
    {"enemies/sectoid", 3, 1},
    {"enemies/sniper", 4, 0},
    {"enemies/mutant", 1, 2},
    {"enemies/assault", 10, 5},
    {"enemies/drone", 10, 2},
};

static void spawn(unit *u, const char *id, int x, int y, int team) {
    memset(u, 0, sizeof *u);
    u->sp = sp_find(id);
    if (u->sp < 0) {
        fprintf(stderr, "xcom: missing sprite %s\n", id);
        exit(1);
    }
    u->x = x;
    u->y = y;
    u->alive = 1;
    u->can_move = 1;
    u->can_shoot = 1;
    u->team = team;
    name_from(u->name, sp_label[u->sp]);
    tune(u);
    if (!u->flying && step_cost(u, x, y) == 0) {
        fprintf(stderr, "xcom: %s starts on a blocked square %d,%d\n", id, x, y);
        exit(1);
    }
}

static void load_pictures(void) {
    static const struct { char c; const char *id; } tab[] = {
        {'.', "terrain_tiles/grass"},
        {'F', "terrain_tiles/forest"},
        {'#', "terrain_tiles/city"},
        {'=', "terrain_tiles/road"},
        {'~', "terrain_tiles/water"},
        {'R', "objects_decor/rocks"},
        {'r', "terrain_tiles/rubble"},
        {'T', "objects_decor/tree"},
        {'C', "objects_decor/crates"},
        {'B', "objects_decor/bush"},
        {'M', "terrain_tiles/mountains"},
    };
    for (int i = 0; i < 128; i++) spr_of[i] = -1;
    for (unsigned i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        int s = sp_find(tab[i].id);
        if (s < 0) {
            fprintf(stderr, "xcom: missing sprite %s\n", tab[i].id);
            exit(1);
        }
        spr_of[(unsigned char)tab[i].c] = s;
    }
}

static void init_game(uint32_t seed) {
    if (seed == 0) {
        fprintf(stderr, "xcom: seed 0 is not allowed\n");
        exit(1);
    }
    rng_state = seed;
    game_seed = seed;
    nlog = 0;
    sel = -1;
    mode = ST_PLAY;
    fire_at = -1;
    battle = 0;
    turn_no = 1;
    memset(seen, 0, sizeof seen);
    memset(vis, 0, sizeof vis);
    memset(reach, 0, sizeof reach);
    load_pictures();
    for (int y = 0; y < ROWS; y++) {
        if (MAP[y][COLS] != 0) {
            fprintf(stderr, "xcom: map row %d is not %d wide\n", y, COLS);
            exit(1);
        }
        for (int x = 0; x < COLS; x++) {
            unit probe;
            memset(&probe, 0, sizeof probe);
            (void)step_cost(&probe, x, y);
        }
    }
    n_you = (int)(sizeof SQUAD / sizeof SQUAD[0]);
    n_them = (int)(sizeof ALIENS / sizeof ALIENS[0]);
    if (n_you > MAX_YOU || n_them > MAX_THEM) {
        fprintf(stderr, "xcom: too many units\n");
        exit(1);
    }
    for (int i = 0; i < n_you; i++)
        spawn(&you[i], SQUAD[i].id, SQUAD[i].x, SQUAD[i].y, TEAM_YOU);
    for (int i = 0; i < n_them; i++)
        spawn(&them[i], ALIENS[i].id, ALIENS[i].x, ALIENS[i].y, TEAM_THEM);
    int n = n_you + n_them;
    for (int i = 0; i < n; i++) {
        unit *a = unit_index(i);
        for (int j = i + 1; j < n; j++) {
            unit *b = unit_index(j);
            if (a->x == b->x && a->y == b->y) {
                fprintf(stderr, "xcom: %s and %s start on the same square\n", a->name, b->name);
                exit(1);
            }
        }
    }
    recompute_vision();
    clean_next = 1;
    say("FIRST CONTACT. wipe them out.");
    fprintf(stderr, "xcom: seed %u\n", seed);
}

static void rebuild_reach(void) {
    memset(reach, 0, sizeof reach);
    if (sel < 0 || battle || mode != ST_PLAY) return;
    unit *u = &you[sel];
    if (!u->alive || !u->can_move) return;
    route_from(u, u->x, u->y, 0);
    int dash = u->budget + u->budget / 2;
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) {
            int d = gdist[y][x];
            if (d <= 0 || d >= INF) continue;
            if (d <= u->budget) reach[y][x] = 1;
            else if (d <= dash && u->can_shoot) reach[y][x] = 2;
        }
}

static int sprite_on(int id, int x, int y) {
    if (x < 0 || y < 0 || x >= TILE || y >= TILE) return 0;
    return sp_at(id, x, y) != 0;
}

/* A tile pixel is 0 transparent, 1 paper, 2..9 a tone whose value minus 1
 * is the ink level 0..8 (see sprites.h). The grey layer takes 0..15, so
 * ink15() rescales it: 0 stays paper, 8 stays solid ink, and everything
 * between is a real grey on the panel instead of a dither pattern laid
 * over the pixels. Round-to-nearest going up, and exact the other way
 * through ek_grey_rect's own dither fallback, so a build with no grey
 * layer still lays down precisely the old 4x4 Bayer pattern. */
static int ink15(int level8) { return (level8 * 15 + 4) / 8; }

/* Draw one sprite row by row, running equal tones together: that is what
 * keeps a whole sprite to a few dozen calls a row instead of forty. */
static void blit_sprite(int x, int y, int id) {
    if (id < 0 || id >= SP_COUNT) {
        fprintf(stderr, "xcom: sprite %d is not real\n", id);
        exit(1);
    }
    for (int r = 0; r < TILE; r++) {
        int c = 0;
        while (c < TILE) {
            int v = sp_at(id, c, r);
            if (v == 0) {
                c++;
                continue;
            }
            int len = 1;
            while (c + len < TILE && sp_at(id, c + len, r) == v)
                len++;
            ek_grey_rect(x + c, y + r, len, 1, ink15(v - 1));
            c += len;
        }
    }
}

/* A paper rim, then the picture. On black trees a black soldier would
 * disappear without it. On pale grass the rim matches the ground. */
static void blit_unit(int x, int y, int id) {
    for (int r = 0; r < TILE; r++)
        for (int c = 0; c < TILE; c++) {
            if (sprite_on(id, c, r)) continue;
            if (sprite_on(id, c - 1, r) || sprite_on(id, c + 1, r) ||
                sprite_on(id, c, r - 1) || sprite_on(id, c, r + 1))
                ek_px(x + c, y + r, EK_PAPER);
        }
    blit_sprite(x, y, id);
}

static void invert_frame(int x, int y, int w, int h, int t) {
    ek_invert_rect(x, y, w, t);
    ek_invert_rect(x, y + h - t, w, t);
    if (h > t * 2) {
        ek_invert_rect(x, y + t, t, h - 2 * t);
        ek_invert_rect(x + w - t, y + t, t, h - 2 * t);
    }
}

static void button(int x, int y, int w, int h, const char *label) {
    ek_fill_rect(x, y, w, h, EK_PAPER);
    ek_rect(x, y, w, h, EK_INK);
    ek_rect(x + 3, y + 3, w - 6, h - 6, EK_INK);
    int tw = (int)strlen(label) * 8;
    if (tw > w - 8) {
        fprintf(stderr, "xcom: label '%s' does not fit\n", label);
        exit(1);
    }
    ek_text(x + (w - tw) / 2, y + (h - 16) / 2, label, EK_INK);
}

static int in_box(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx && y >= by && x < bx + bw && y < by + bh;
}

#define BTN_W 140
#define BTN_H 60
#define BTN_Y 664
#define QUIT_X 500
#define QUIT_Y 2
#define QUIT_W 92
#define QUIT_H 30

static int btn_x(int i) { return 8 + i * (BTN_W + 8); }

static void draw_hp(int ox, int oy, const unit *u) {
    int bw = TILE - 6;
    int filled = u->max_hp ? bw * u->hp / u->max_hp : 0;
    int hy = oy + TILE - 6;
    ek_fill_rect(ox + 3, hy, bw, 4, EK_PAPER);
    if (filled > 0) ek_fill_rect(ox + 3, hy, filled, 4, EK_INK);
    ek_rect(ox + 3, hy, bw, 4, EK_INK);
}

static void draw_badge(int ox, int oy, int team) {
    int x = ox + TILE - 11, y = oy + 2;
    if (team == TEAM_YOU) {
        ek_fill_rect(x, y, 8, 8, EK_PAPER);
        ek_rect(x, y, 8, 8, EK_INK);
        ek_rect(x + 2, y + 2, 4, 4, EK_INK);
    } else {
        ek_fill_rect(x, y, 8, 8, EK_INK);
        ek_rect(x + 2, y + 2, 4, 4, EK_PAPER);
    }
}

static void draw(void) {
    ek_clear(EK_PAPER);
    char line[96];
    snprintf(line, sizeof line, "XCOM   TURN %d", turn_no);
    ek_text(8, 2, line, EK_INK);
    const char *phase = "YOUR TURN";
    if (battle == 1) phase = "YOU WIN";
    else if (battle == 2) phase = "SQUAD LOST";
    ek_text(8, 18, phase, EK_INK);
    int see = 0;
    for (int i = 0; i < n_them; i++)
        if (them[i].alive && vis[them[i].y][them[i].x]) see++;
    snprintf(line, sizeof line, "SEE %d", see);
    ek_text(160, 18, line, EK_INK);
    snprintf(line, sizeof line, "SEED %u", game_seed);
    ek_text(280, 18, line, EK_INK);
    button(QUIT_X, QUIT_Y, QUIT_W, QUIT_H, mode == ST_QUIT ? "SURE?" : "QUIT");

    ek_rect(MAP_X - 2, MAP_Y - 2, COLS * TILE + 4, ROWS * TILE + 4, EK_INK);
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            int ox = MAP_X + x * TILE;
            int oy = MAP_Y + y * TILE;
            if (!seen[y][x]) {
                ek_grey_rect(ox, oy, TILE, TILE, ink15(6));
                continue;
            }
            unsigned char ch = (unsigned char)MAP[y][x];
            if (ch >= 128 || spr_of[ch] < 0) {
                fprintf(stderr, "xcom: no picture for '%c'\n", MAP[y][x]);
                exit(1);
            }
            blit_sprite(ox, oy, spr_of[ch]);
        }
    }
    int n = n_you + n_them;
    for (int i = 0; i < n; i++) {
        unit *u = unit_index(i);
        if (!u->alive) continue;
        if (u->team == TEAM_THEM && !vis[u->y][u->x]) continue;
        int ox = MAP_X + u->x * TILE;
        int oy = MAP_Y + u->y * TILE;
        blit_unit(ox, oy, u->sp);
        draw_badge(ox, oy, u->team);
        draw_hp(ox, oy, u);
    }
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (!reach[y][x]) continue;
            int ox = MAP_X + x * TILE;
            int oy = MAP_Y + y * TILE;
            invert_frame(ox, oy, TILE, TILE, reach[y][x] == 2 ? 3 : 1);
        }
    }
    if (sel >= 0 && you[sel].alive) {
        unit *u = &you[sel];
        int ox = MAP_X + u->x * TILE;
        int oy = MAP_Y + u->y * TILE;
        invert_frame(ox + 2, oy + 2, TILE - 4, TILE - 4, 2);
        if (u->can_shoot) {
            for (int i = 0; i < n_them; i++) {
                unit *e = &them[i];
                if (!e->alive || !vis[e->y][e->x]) continue;
                if (hit_chance_at(u, u->x, u->y, u->moved, e) <= 0) continue;
                int ex = MAP_X + e->x * TILE;
                int ey = MAP_Y + e->y * TILE;
                invert_frame(ex + 1, ey + 1, TILE - 2, TILE - 2, 2);
            }
        }
    }

    ek_hline(0, 558, EK_W, EK_INK);
    int start = nlog > 3 ? nlog - 3 : 0;
    for (int i = 0; i < 3; i++) {
        if (start + i < nlog) ek_text(8, 562 + i * 16, logs[start + i], EK_INK);
    }
    if (sel >= 0 && you[sel].alive) {
        unit *u = &you[sel];
        int ap = (u->can_move ? 1 : 0) + (u->can_shoot ? 1 : 0);
        snprintf(line, sizeof line, "%s  HP %d/%d  AP %d%s%s", u->name, u->hp, u->max_hp, ap,
                 u->hunker ? "  HUNKER" : "", u->overwatch ? "  WATCH" : "");
        ek_text(8, 614, line, EK_INK);
        snprintf(line, sizeof line, "AIM %d  DMG %d-%d  ARM %d  MOV %d  RNG %d",
                 u->aim, u->dlo, u->dhi, u->armor, u->budget, u->range);
        ek_text(8, 632, line, EK_INK);
        if (u->heal_hp)
            ek_printf(360, 632, EK_INK, "HEAL %d", u->heal_left);
    } else {
        ek_text(8, 614, "TAP A SOLDIER", EK_INK);
    }

    if (mode == ST_FIRE && fire_at >= 0 && sel >= 0) {
        int chance = hit_chance_at(&you[sel], you[sel].x, you[sel].y, you[sel].moved, &them[fire_at]);
        char lab[16];
        snprintf(lab, sizeof lab, "FIRE %d%%", chance);
        button(8, BTN_Y, 288, BTN_H, "BACK");
        button(304, BTN_Y, 288, BTN_H, lab);
    } else if (mode == ST_HEAL) {
        button(8, BTN_Y, 288, BTN_H, "BACK");
        button(304, BTN_Y, 288, BTN_H, "PICK FRIEND");
    } else {
        button(btn_x(0), BTN_Y, BTN_W, BTN_H, "END");
        button(btn_x(1), BTN_Y, BTN_W, BTN_H, "WATCH");
        button(btn_x(2), BTN_Y, BTN_W, BTN_H, "HUNKER");
        button(btn_x(3), BTN_Y, BTN_W, BTN_H, "HEAL");
    }

    const char *hint = "tap one of your soldiers.";
    const char *hint2 = "a thick box is a long run. you cannot shoot after it.";
    if (battle == 1) {
        hint = "you win. tap QUIT, then SURE, to leave.";
        hint2 = "";
    } else if (battle == 2) {
        hint = "your squad is lost. tap QUIT, then SURE.";
        hint2 = "";
    } else if (mode == ST_FIRE) hint = "tap FIRE to shoot. tap BACK to stop.";
    else if (mode == ST_HEAL) hint = "tap a hurt friend next to the medic.";
    else if (mode == ST_QUIT) {
        hint = "tap SURE to leave.";
        hint2 = "tap anywhere else to stay.";
    } else if (sel >= 0 && you[sel].alive && you[sel].can_move) {
        hint = "tap a box to walk. tap an alien to shoot.";
    } else if (sel >= 0 && you[sel].alive && you[sel].can_shoot) {
        hint = "tap an alien to shoot, or tap END.";
        hint2 = "";
    } else if (sel >= 0) {
        hint = "tap END. the aliens go next.";
        hint2 = "";
    }
    ek_text(8, 732, hint, EK_INK);
    ek_text(8, 750, hint2, EK_INK);
}

static int try_move(unit *u, int x, int y) {
    if (battle || !u->alive || !u->can_move) return 0;
    recompute_vision();
    route_from(u, u->x, u->y, 0);
    int d = gdist[y][x];
    int dash = u->budget + u->budget / 2;
    if (d <= 0 || d >= INF || occupied(x, y, u, 0)) {
        say("cannot go there");
        return 0;
    }
    if (d > u->budget && !(d <= dash && u->can_shoot)) {
        say("too far");
        return 0;
    }
    return walk_to(u, x, y, 0);
}

static unit *you_at(int x, int y) {
    for (int i = 0; i < n_you; i++)
        if (you[i].alive && you[i].x == x && you[i].y == y) return &you[i];
    return NULL;
}

static int them_at(int x, int y) {
    for (int i = 0; i < n_them; i++)
        if (them[i].alive && vis[them[i].y][them[i].x] && them[i].x == x && them[i].y == y)
            return i;
    return -1;
}

static void do_fire(void) {
    if (sel < 0 || fire_at < 0) return;
    unit *u = &you[sel];
    unit *e = &them[fire_at];
    if (!u->alive || !u->can_shoot || !e->alive) {
        say("that shot is gone");
        mode = ST_PLAY;
        return;
    }
    if (hit_chance_at(u, u->x, u->y, u->moved, e) <= 0) {
        fprintf(stderr, "xcom: fire button had no shot\n");
        exit(1);
    }
    u->can_shoot = 0;
    resolve_shot(u, e);
    mode = ST_PLAY;
    fire_at = -1;
}

static void do_heal(unit *m, unit *t) {
    if (!m->can_shoot || m->heal_left <= 0 || m->heal_hp <= 0) {
        say("cannot heal");
        return;
    }
    if (!t->alive || t == m || t->team != TEAM_YOU || t->hp >= t->max_hp) {
        say("not a hurt friend");
        return;
    }
    if (cheb(m->x, m->y, t->x, t->y) > 1) {
        say("too far to heal");
        return;
    }
    int before = t->hp;
    t->hp += m->heal_hp;
    if (t->hp > t->max_hp) t->hp = t->max_hp;
    m->heal_left--;
    m->can_shoot = 0;
    say("%s heals %s for %d", m->name, t->name, t->hp - before);
    mode = ST_PLAY;
}

static void tap(int x, int y) {
    if (sel >= 0 && !you[sel].alive) sel = -1;
    if (in_box(x, y, QUIT_X, QUIT_Y, QUIT_W, QUIT_H)) {
        if (mode == ST_QUIT) stop_req = 1;
        else {
            mode = ST_QUIT;
            say("tap SURE to leave");
        }
        return;
    }
    if (mode == ST_QUIT) {
        mode = ST_PLAY;
        return;
    }
    if (battle) return;

    if (mode == ST_FIRE) {
        if (in_box(x, y, 8, BTN_Y, 288, BTN_H)) {
            mode = ST_PLAY;
            fire_at = -1;
            return;
        }
        if (in_box(x, y, 304, BTN_Y, 288, BTN_H)) {
            do_fire();
            return;
        }
    }
    if (mode == ST_HEAL) {
        if (in_box(x, y, 8, BTN_Y, 288, BTN_H)) {
            mode = ST_PLAY;
            return;
        }
    }

    if (mode == ST_PLAY && in_box(x, y, 8, BTN_Y, 592, BTN_H)) {
        int which = -1;
        for (int i = 0; i < 4; i++)
            if (in_box(x, y, btn_x(i), BTN_Y, BTN_W, BTN_H)) which = i;
        if (which == 0) {
            end_turn();
            return;
        }
        if (sel < 0) {
            say("pick a soldier first");
            return;
        }
        unit *u = &you[sel];
        if (which == 1) {
            if (!u->can_shoot) {
                say("cannot watch");
                return;
            }
            u->overwatch = 1;
            u->can_shoot = 0;
            u->can_move = 0;
            say("%s is watching", u->name);
            return;
        }
        if (which == 2) {
            if (!u->can_move && !u->can_shoot) {
                say("cannot hunker");
                return;
            }
            u->hunker = 1;
            u->can_move = 0;
            u->can_shoot = 0;
            say("%s hunkers down", u->name);
            return;
        }
        if (which == 3) {
            if (u->heal_hp <= 0 || u->heal_left <= 0) {
                say("only the medic heals, and only twice");
                return;
            }
            if (!u->can_shoot) {
                say("no action left to heal");
                return;
            }
            int found = 0;
            unit *only = NULL;
            for (int i = 0; i < n_you; i++) {
                unit *f = &you[i];
                if (!f->alive || f == u || f->hp >= f->max_hp) continue;
                if (cheb(u->x, u->y, f->x, f->y) > 1) continue;
                found++;
                only = f;
            }
            if (found == 0) {
                say("no hurt friend beside you");
                return;
            }
            if (found == 1) {
                do_heal(u, only);
                return;
            }
            mode = ST_HEAL;
            say("tap who to heal");
            return;
        }
        return;
    }

    int mx = x - MAP_X, my = y - MAP_Y;
    if (mx < 0 || my < 0 || mx >= COLS * TILE || my >= ROWS * TILE) return;
    int cx = mx / TILE, cy = my / TILE;

    if (mode == ST_HEAL && sel >= 0) {
        unit *f = you_at(cx, cy);
        if (!f) {
            say("tap a friend");
            return;
        }
        do_heal(&you[sel], f);
        return;
    }
    if (mode == ST_FIRE) {
        int ei = them_at(cx, cy);
        if (ei == fire_at) do_fire();
        else {
            mode = ST_PLAY;
            fire_at = -1;
        }
        return;
    }

    int yi = -1;
    for (int i = 0; i < n_you; i++)
        if (you[i].alive && you[i].x == cx && you[i].y == cy) yi = i;
    if (yi >= 0) {
        sel = yi;
        mode = ST_PLAY;
        return;
    }
    if (sel < 0) {
        say("pick a soldier first");
        return;
    }
    unit *u = &you[sel];
    int ei = them_at(cx, cy);
    if (ei >= 0) {
        int chance = hit_chance_at(u, u->x, u->y, u->moved, &them[ei]);
        if (!u->can_shoot || chance <= 0) {
            say("no shot");
            return;
        }
        fire_at = ei;
        mode = ST_FIRE;
        say("%s at %s, %d%%", u->name, them[ei].name, chance);
        return;
    }
    if (!seen[cy][cx]) {
        say("you cannot see there");
        return;
    }
    if (u->can_move) try_move(u, cx, cy);
    else say("already walked");
}

#ifndef EK_HOST
static void handle_event(const ek_event *ev) {
    if (ev->type == EK_DOWN) {
        have_down = 1;
        down_x = ev->x;
        down_y = ev->y;
        return;
    }
    if (ev->type == EK_MOVE) return;
    if (ev->type != EK_UP) return;
    if (have_down && (abs(ev->x - down_x) > 40 || abs(ev->y - down_y) > 40)) {
        have_down = 0;
        return;
    }
    have_down = 0;
    tap(ev->x, ev->y);
}
#endif

static void present(void) {
    recompute_vision();
    rebuild_reach();
    draw();
    /* A tap changes a tenth of the screen, so the default is whatever the
     * engine's ghosting policy picks (GL16_FAST for partials). A whole-turn
     * redraw, the opening and the result screen go out as clean GC16. */
    int wfm = (force_clean || clean_next) ? wfm_clean : 0;
    clean_next = 0;
    ek_waveform(wfm);
    ek_stats st = ek_flush();
    ek_waveform(0);
    if (stats_on)
        fprintf(stderr, "xcom: flush wfm=%d rects=%d tiles=%d px=%ld full=%d us=%u\n",
                st.wfm, st.rects, st.tiles, st.px, st.full, st.us);
}

static int failures;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "xcom: FAIL %s\n", msg);
        failures++;
    }
}

static void expect_bounds(void) {
    int n = n_you + n_them;
    for (int i = 0; i < n; i++) {
        unit *u = unit_index(i);
        if (!u->alive) continue;
        expect(in_map(u->x, u->y), "unit left the map");
        expect(u->hp > 0 && u->hp <= u->max_hp, "hp out of range");
        if (!u->flying) expect(step_cost(u, u->x, u->y) > 0, "standing on a wall");
        for (int j = i + 1; j < n; j++) {
            unit *o = unit_index(j);
            if (!o->alive) continue;
            expect(!(u->x == o->x && u->y == o->y), "two units on one square");
        }
    }
}

static int selftest(void) {
    failures = 0;
    init_game(1);
    expect(n_you == 4 && n_them == 5, "squad size");
    expect(you[1].range == 11 && you[1].steady_bonus == 20, "sniper");
    expect(them[2].range == 1 && them[2].melee, "mutant");
    expect(them[4].flying && them[4].range == 6, "drone");
    expect(you[3].heal_left == 2 && you[3].heal_hp == 3, "medic");
    expect(you[2].suppress, "heavy pins");
    expect(them[0].panic_pct == 20, "sectoid panic");

    unit probe;
    memset(&probe, 0, sizeof probe);
    route_from(&probe, 2, 11, 1);
    expect(gdist[10][2] == 2, "forest costs 2");
    expect(gdist[11][3] == 1, "grass costs 1");
    expect(gdist[4][5] >= INF, "water is blocked");
    expect(los(2, 1, 2, 3) == 0, "rock blocks sight");
    expect(los(0, 12, 11, 12) == 1, "open row");

    them[0].x = 5;
    them[0].y = 3;
    them[0].flying = 0;
    expect(cover_of(&them[0], 3, 3) == 40, "rock between shooter and target");
    them[0].x = 2;
    them[0].y = 10;
    expect(cover_of(&them[0], 2, 11) == 40, "trees you stand in");

    them[0].x = 4;
    them[0].y = 11;
    them[0].alive = 1;
    them[0].hp = them[0].max_hp;
    int steady = hit_chance_at(&you[1], 4, 12, 0, &them[0]);
    int after = hit_chance_at(&you[1], 4, 12, 1, &them[0]);
    expect(steady == 95 && after == 85, "steady aim");

    init_game(1);
    expect(try_move(&you[0], you[1].x, you[1].y) == 0, "no stacking");
    expect(you[0].x == 1 && you[0].y == 12 && you[0].can_move, "did not move onto a friend");
    expect(try_move(&you[0], 0, 11) == 1, "one step");
    expect(you[0].x == 0 && you[0].y == 11, "landed");
    expect(you[0].can_move == 0 && you[0].can_shoot == 1, "walk spends one action");

    init_game(1);
    them[0].x = 2;
    them[0].y = 12;
    them[0].hunker = 0;
    them[0].flying = 0;
    int saw_hit = 0, saw_miss = 0;
    for (int seed = 1; seed < 400 && (!saw_hit || !saw_miss); seed++) {
        them[0].hp = them[0].max_hp;
        them[0].alive = 1;
        them[0].x = 2;
        them[0].y = 12;
        rng_state = (uint32_t)seed;
        int before = them[0].hp;
        resolve_shot(&you[0], &them[0]);
        if (!them[0].alive || them[0].hp < before) saw_hit = 1;
        else saw_miss = 1;
    }
    expect(saw_hit && saw_miss, "shots can hit and miss");

    init_game(1);
    end_turn();
    expect(turn_no == 2, "turn advanced");
    expect_bounds();
    expect(battle == 0, "nobody has won yet");

    init_game(1);
    for (int i = 0; i < n_them; i++) them[i].alive = 0;
    check_end();
    expect(battle == 1, "wipe is a win");
    init_game(1);
    for (int i = 0; i < n_you; i++) you[i].alive = 0;
    check_end();
    expect(battle == 2, "a dead squad is a loss");

    if (failures) {
        fprintf(stderr, "xcom: %d checks failed\n", failures);
        return failures;
    }
    fprintf(stderr, "xcom: selftest ok\n");
    return 0;
}

/* The PC has neither aplay nor a speaker, so sound is opt-in there with
 * --soundcheck (which only reports). The Kindle speaks by default and --mute
 * turns it off. */
static const char *sound_default(void) {
#ifdef EK_HOST
    return "sound";
#else
    return "/mnt/us/xcom-sound";
#endif
}

static int sound_default_mode(void) {
#ifdef EK_HOST
    return AUDIO_OFF;
#else
    return AUDIO_SPEAK;
#endif
}

static void usage(void) {
    fprintf(stderr,
            "usage: xcom [--seed N] [--dump file.pgm] [--auto N] [--selftest]\n"
            "            [--no-takeover] [--no-wait] [--stats] [--clean]\n"
            "            [--mute] [--sound DIR] [--soundcheck]\n"
            "\n"
            "  On the Kindle the screen is taken until you tap QUIT, then SURE.\n"
            "  --selftest   check the rules on this computer and exit\n"
            "  --auto N     play N turns by itself, then exit\n"
            "  --dump FILE  write the picture (PGM) and exit\n"
            "  --seed N     same seed, same dice\n"
            "  --stats      print what each screen update cost\n"
            "  --clean      refresh the whole picture on every update (slow)\n"
            "  --mute       no sound\n"
            "  --sound DIR  clips live in DIR (default %s)\n"
            "  --soundcheck print the words that would be spoken, and any\n"
            "               clip DIR is missing, then exit\n",
            sound_default());
}

int main(int argc, char **argv) {
    unsigned flags = EK_TAKEOVER;
    int wait = 1;
    int do_test = 0;
    int auto_turns = -1;
    const char *dump_path = NULL;
    const char *sound_dir = sound_default();
    int sound = sound_default_mode();
    uint32_t seed = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v <= 0 || v > 0x7fffffff) {
                usage();
                return 2;
            }
            seed = (uint32_t)v;
        } else if (!strcmp(argv[i], "--auto") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v < 0 || v > 100) {
                usage();
                return 2;
            }
            auto_turns = (int)v;
        } else if (!strcmp(argv[i], "--dump") && i + 1 < argc) {
            dump_path = argv[++i];
            if (!dump_path[0]) {
                usage();
                return 2;
            }
        } else if (!strcmp(argv[i], "--selftest")) do_test = 1;
        else if (!strcmp(argv[i], "--stats")) stats_on = 1;
        else if (!strcmp(argv[i], "--clean")) force_clean = 1;
        else if (!strcmp(argv[i], "--mute")) sound = AUDIO_OFF;
        else if (!strcmp(argv[i], "--soundcheck")) sound = AUDIO_CHECK;
        else if (!strcmp(argv[i], "--sound") && i + 1 < argc) {
            sound_dir = argv[++i];
            if (!sound_dir[0]) {
                usage();
                return 2;
            }
        } else if (!strcmp(argv[i], "--no-takeover")) flags &= ~EK_TAKEOVER;
        else if (!strcmp(argv[i], "--no-wait")) wait = 0;
        else if (!strcmp(argv[i], "--help")) {
            usage();
            return 0;
        } else {
            usage();
            return 2;
        }
    }
    if (seed == 0) {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
            fprintf(stderr, "xcom: clock_gettime failed\n");
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
        fprintf(stderr, "xcom: the screen would not open\n");
        ek_restore_ui();
        audio_stop();
        return 1;
    }
    if (sound == AUDIO_SPEAK || sound == AUDIO_CHECK) {
        if (audio_start(sound, sound_dir) != 0)
            fprintf(stderr, "xcom: carrying on without sound\n");
    }
    wfm_clean = ek_waveform_by_name("GC16");
    if (!wfm_clean) {
        fprintf(stderr, "xcom: GC16 waveform is missing\n");
        ek_close();
        audio_stop();
        return 1;
    }
    /* Real greyscale: the sprites carry 16 tones and a dither would throw
     * half of them away. If the layer cannot be allocated we fall back to
     * dither, which is byte for byte what this game did before. */
    if (ek_grey_enable() != 0)
        fprintf(stderr, "xcom: no memory for the grey layer, dithering instead\n");
    ek_wait_updates(wait);
    if (force_clean) {
        /* The old behaviour: every update a clean GC16 refresh. */
        ek_waveform(wfm_clean);
        ek_ghost(0, 0);
        ek_policy(180, 4, 30);
    } else {
        /* A tap touches a few hundred tiles, not a page: let the engine pick
         * GL16_FAST for those and clean up the area it keeps re-inking. */
        ek_waveform(0);
        ek_ghost(4, 30);
        ek_policy(180, 6, 35);
    }

    int rc = 0;
    if (do_test) {
        rc = selftest() ? 1 : 0;
        present();
    } else {
        init_game(seed);
        if (auto_turns >= 0) {
            for (int t = 0; t < auto_turns && !battle; t++) {
                for (int i = 0; i < n_you && !battle; i++)
                    if (you[i].alive && (you[i].can_move || you[i].can_shoot))
                        act_ai(&you[i]);
                if (!battle) end_turn();
            }
        }
        present();
    }

#ifdef EK_HOST
    /* A pipe of "tap X Y" lines plays the game, then --dump saves that picture.
     * A terminal with --dump saves the opening and does not wait for typing. */
    if (!do_test && auto_turns < 0 && !isatty(0)) {
        char buf[64];
        while (!stop_req && fgets(buf, sizeof buf, stdin)) {
            int x, y;
            if (buf[0] == 'q') break;
            if (sscanf(buf, "tap %d %d", &x, &y) != 2) {
                fprintf(stderr, "xcom: want 'tap X Y' or q\n");
                audio_stop();
                ek_close();
                return 2;
            }
            tap(x, y);
            if (stop_req) break;
            present();
        }
    }
#endif

    if (dump_path) {
        if (ek_dump_pgm(dump_path, 1) != 0) {
            fprintf(stderr, "xcom: could not write %s\n", dump_path);
            audio_stop();
            ek_close();
            return 1;
        }
    }

    if (sound == AUDIO_CHECK)
        fprintf(stderr, "xcom: soundcheck done, %d clips missing\n", audio_misses());

#ifdef EK_HOST
    if (do_test || auto_turns >= 0 || dump_path != NULL || !isatty(0)) {
        audio_stop();
        ek_close();
        return rc;
    }
    {
        char buf[64];
        while (!stop_req && fgets(buf, sizeof buf, stdin)) {
            int x, y;
            if (buf[0] == 'q') break;
            if (sscanf(buf, "tap %d %d", &x, &y) != 2) {
                fprintf(stderr, "xcom: want 'tap X Y' or q\n");
                audio_stop();
                ek_close();
                return 2;
            }
            tap(x, y);
            if (stop_req) break;
            present();
        }
    }
#else
    /* --auto is the way to try the game on the device without touching it:
     * it plays those turns by itself, shows the result and quits. */
    while (!stop_req && auto_turns < 0) {
        ek_event ev;
        int got = 0;
        while (ek_poll(&ev)) {
            handle_event(&ev);
            got = 1;
        }
        if (stop_req) break;
        if (got) present();
        else ek_sleep_ms(20);
    }
#endif
    audio_stop();
    ek_close();
    return rc;
}
