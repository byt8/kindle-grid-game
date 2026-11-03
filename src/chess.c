#define _GNU_SOURCE
/* chess.c -- play chess against DeepSeek on a Kindle Touch.
 *
 * The human is White, the model is Black. There is no local chess engine:
 * every Black move comes from the chat completions API, and the model also
 * writes one line of trash talk per move. The rules, the board and the touch
 * handling are all here; nothing else knows how to play.
 *
 *   - The whole rule set is implemented (castling, en passant, promotion,
 *     check, mate, stalemate, 50-move, insufficient material) and --selftest
 *     checks it against perft numbers.
 *   - The API call is made by this process: it builds the JSON, runs the
 *     device's own curl (the Kindle's libcurl is not linkable against the
 *     static musl binary), and reads the reply back through a pipe.
 *   - A move the API cannot be reached for, or a move that is not legal, is
 *     reported on the screen. The game never invents a move to keep going.
 *
 * Screen is 600x800, one bit, touch: 8x8 board of 68 px squares, a status
 * bar on top, the model's remarks underneath. Quit, volume, wi-fi and the
 * power-key sleep live in the shared menu (src/kframe.c): the hamburger is
 * the top-right corner. NEW and RETRY are this game's own rows in that menu.
 */
#include "eink.h"
#include "kframe.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- the board --------------------------------------------------------- */

enum { PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING };
enum { WHITE = 0, BLACK = 1 };

#define SQ(f, r)     ((r) * 8 + (f))
#define FILE_OF(s)   ((s) & 7)
#define RANK_OF(s)   ((s) >> 3)

/* castling rights */
#define CR_WK 1
#define CR_WQ 2
#define CR_BK 4
#define CR_BQ 8

/* move flags */
#define F_EP     1
#define F_CASTLE 2
#define F_DOUBLE 3

typedef struct {
    int from, to, promo, flag;
} move;

typedef struct {
    int    sq[64];     /* +piece white, -piece black, 0 empty */
    int    stm;        /* side to move: WHITE or BLACK */
    int    castle;     /* CR_ bits still allowed */
    int    ep;         /* en passant target square, or -1 */
    int    half;       /* moves since last pawn move or capture */
    int    full;
} pos;

typedef struct {
    int cap, castle, ep, half, stm;
} undo;

static void pos_init(pos *p) {
    memset(p, 0, sizeof *p);
    for (int f = 0; f < 8; f++) {
        p->sq[SQ(f, 1)] = PAWN;
        p->sq[SQ(f, 6)] = -PAWN;
    }
    static const int back[8] = { ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK };
    for (int f = 0; f < 8; f++) {
        p->sq[SQ(f, 0)] = back[f];
        p->sq[SQ(f, 7)] = -back[f];
    }
    p->stm = WHITE;
    p->castle = CR_WK | CR_WQ | CR_BK | CR_BQ;
    p->ep = -1;
    p->half = 0;
    p->full = 1;
}

static int on_board(int f, int r) {
    return f >= 0 && f < 8 && r >= 0 && r < 8;
}

/* Is `sq` attacked by side `by`? Rays out from the square, not a scan of the
 * whole board: gen_legal() calls this once per candidate move. */
static int is_attacked(const pos *p, int sq, int by) {
    int f = FILE_OF(sq), r = RANK_OF(sq);
    int s = (by == WHITE) ? 1 : -1;      /* sign of that side's pieces */
    int pf = f, pr = r;

    /* pawns: a white pawn sits one rank below the square it attacks */
    for (int df = -1; df <= 1; df += 2) {
        int ff = f + df, rr = r - s;
        if (on_board(ff, rr) && p->sq[SQ(ff, rr)] == s * PAWN) return 1;
    }
    static const int kn[8][2] = { {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2} };
    for (int i = 0; i < 8; i++) {
        int ff = f + kn[i][0], rr = r + kn[i][1];
        if (on_board(ff, rr) && p->sq[SQ(ff, rr)] == s * KNIGHT) return 1;
    }
    for (int df = -1; df <= 1; df++)
        for (int dr = -1; dr <= 1; dr++) {
            if (!df && !dr) continue;
            int ff = f + df, rr = r + dr;
            if (on_board(ff, rr) && p->sq[SQ(ff, rr)] == s * KING) return 1;
        }
    static const int rd[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
    for (int i = 0; i < 4; i++) {
        int ff = f + rd[i][0], rr = r + rd[i][1];
        while (on_board(ff, rr)) {
            int pc = p->sq[SQ(ff, rr)];
            if (pc) {
                if (pc == s * ROOK || pc == s * QUEEN) return 1;
                break;
            }
            ff += rd[i][0];
            rr += rd[i][1];
        }
    }
    static const int bd[4][2] = { {1,1},{1,-1},{-1,1},{-1,-1} };
    for (int i = 0; i < 4; i++) {
        int ff = f + bd[i][0], rr = r + bd[i][1];
        while (on_board(ff, rr)) {
            int pc = p->sq[SQ(ff, rr)];
            if (pc) {
                if (pc == s * BISHOP || pc == s * QUEEN) return 1;
                break;
            }
            ff += bd[i][0];
            rr += bd[i][1];
        }
    }
    (void)pf; (void)pr;
    return 0;
}

static int king_sq(const pos *p, int side) {
    int want = side == WHITE ? KING : -KING;
    for (int s = 0; s < 64; s++)
        if (p->sq[s] == want) return s;
    fprintf(stderr, "chess: no king for side %d\n", side);
    exit(1);
}

static int in_check(const pos *p, int side) {
    return is_attacked(p, king_sq(p, side), !side);
}

static int gen_moves(const pos *p, move *out, int max) {
    int n = 0;
    int me = p->stm;
    int s = me == WHITE ? 1 : -1;
    int push = me == WHITE ? 8 : -8;

#define EMIT(A, B, C, D)                                         \
    do {                                                         \
        if (n < max) {                                           \
            out[n].from = (A); out[n].to = (B);                   \
            out[n].promo = (C); out[n].flag = (D); n++;           \
        }                                                        \
    } while (0)

    for (int sq = 0; sq < 64; sq++) {
        int pc = p->sq[sq];
        if (!pc || (pc > 0) != (me == WHITE)) continue;
        int type = pc > 0 ? pc : -pc;
        int f = FILE_OF(sq), r = RANK_OF(sq);

        if (type == PAWN) {
            int one = sq + push;
            if (on_board(f, r + (push > 0 ? 1 : -1)) && p->sq[one] == 0) {
                int last = (push > 0 && RANK_OF(one) == 7) || (push < 0 && RANK_OF(one) == 0);
                if (last) {
                    EMIT(sq, one, QUEEN, 0);
                    EMIT(sq, one, ROOK, 0);
                    EMIT(sq, one, BISHOP, 0);
                    EMIT(sq, one, KNIGHT, 0);
                } else {
                    EMIT(sq, one, 0, 0);
                    int two = sq + 2 * push;
                    int start = (push > 0 && r == 1) || (push < 0 && r == 6);
                    if (start && p->sq[two] == 0) EMIT(sq, two, 0, F_DOUBLE);
                }
            }
            for (int df = -1; df <= 1; df += 2) {
                int ff = f + df, rr = r + (push > 0 ? 1 : -1);
                if (!on_board(ff, rr)) continue;
                int to = SQ(ff, rr);
                int tgt = p->sq[to];
                int last = (push > 0 && rr == 7) || (push < 0 && rr == 0);
                if (tgt && (tgt > 0) != (me == WHITE)) {
                    if (last) {
                        EMIT(sq, to, QUEEN, 0);
                        EMIT(sq, to, ROOK, 0);
                        EMIT(sq, to, BISHOP, 0);
                        EMIT(sq, to, KNIGHT, 0);
                    } else {
                        EMIT(sq, to, 0, 0);
                    }
                } else if (!tgt && to == p->ep) {
                    EMIT(sq, to, 0, F_EP);
                }
            }
            continue;
        }

        if (type == KNIGHT || type == KING) {
            static const int kn[8][2] = { {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2} };
            static const int kg[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
            const int (*d)[2] = (type == KNIGHT) ? kn : kg;
            for (int i = 0; i < 8; i++) {
                int ff = f + d[i][0], rr = r + d[i][1];
                if (!on_board(ff, rr)) continue;
                int tgt = p->sq[SQ(ff, rr)];
                if (tgt && (tgt > 0) == (me == WHITE)) continue;
                EMIT(sq, SQ(ff, rr), 0, 0);
            }
            if (type == KING) {
                int home = me == WHITE ? 0 : 7;
                int can_k = me == WHITE ? CR_WK : CR_BK;
                int can_q = me == WHITE ? CR_WQ : CR_BQ;
                if (sq == SQ(4, home) && !is_attacked(p, sq, !me)) {
                    if ((p->castle & can_k) && !p->sq[SQ(5, home)] && !p->sq[SQ(6, home)] &&
                        !is_attacked(p, SQ(5, home), !me) && !is_attacked(p, SQ(6, home), !me))
                        EMIT(sq, SQ(6, home), 0, F_CASTLE);
                    if ((p->castle & can_q) && !p->sq[SQ(3, home)] && !p->sq[SQ(2, home)] &&
                        !p->sq[SQ(1, home)] &&
                        !is_attacked(p, SQ(3, home), !me) && !is_attacked(p, SQ(2, home), !me))
                        EMIT(sq, SQ(2, home), 0, F_CASTLE);
                }
            }
            continue;
        }

        /* sliders */
        static const int rd[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        static const int bd[4][2] = { {1,1},{1,-1},{-1,1},{-1,-1} };
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 0 && type != ROOK && type != QUEEN) continue;
            if (pass == 1 && type != BISHOP && type != QUEEN) continue;
            const int (*d)[2] = pass == 0 ? rd : bd;
            for (int i = 0; i < 4; i++) {
                int ff = f + d[i][0], rr = r + d[i][1];
                while (on_board(ff, rr)) {
                    int tgt = p->sq[SQ(ff, rr)];
                    if (tgt && (tgt > 0) == (me == WHITE)) break;
                    EMIT(sq, SQ(ff, rr), 0, 0);
                    if (tgt) break;
                    ff += d[i][0];
                    rr += d[i][1];
                }
            }
        }
    }
#undef EMIT
    (void)s;
    return n;
}

static void make_move(pos *p, const move *m, undo *u) {
    int pc = p->sq[m->from];
    int me = p->stm;
    int home = me == WHITE ? 0 : 7;

    u->cap = -1;
    u->castle = p->castle;
    u->ep = p->ep;
    u->half = p->half;
    u->stm = me;

    if (m->flag == F_EP) {
        int capsq = m->to + (me == WHITE ? -8 : 8);
        u->cap = p->sq[capsq];
        p->sq[capsq] = 0;
    } else if (p->sq[m->to]) {
        u->cap = p->sq[m->to];
    }

    p->sq[m->to] = m->promo ? (me == WHITE ? m->promo : -m->promo) : pc;
    p->sq[m->from] = 0;

    if (m->flag == F_CASTLE) {
        int rf = (m->to > m->from) ? 7 : 0;
        int rt = (m->to > m->from) ? 5 : 3;
        p->sq[SQ(rt, home)] = p->sq[SQ(rf, home)];
        p->sq[SQ(rf, home)] = 0;
    }

    if ((pc > 0 ? pc : -pc) == KING) p->castle &= me == WHITE ? ~(CR_WK | CR_WQ) : ~(CR_BK | CR_BQ);
    if (m->from == SQ(0, 0) || m->to == SQ(0, 0)) p->castle &= ~CR_WQ;
    if (m->from == SQ(7, 0) || m->to == SQ(7, 0)) p->castle &= ~CR_WK;
    if (m->from == SQ(0, 7) || m->to == SQ(0, 7)) p->castle &= ~CR_BQ;
    if (m->from == SQ(7, 7) || m->to == SQ(7, 7)) p->castle &= ~CR_BK;

    if ((pc > 0 ? pc : -pc) == PAWN && m->flag == F_DOUBLE) p->ep = (m->from + m->to) / 2;
    else p->ep = -1;

    p->half = ((pc > 0 ? pc : -pc) == PAWN || u->cap != -1) ? 0 : p->half + 1;
    p->stm = !me;
    if (me == BLACK) p->full++;
}

static void unmake_move(pos *p, const move *m, const undo *u) {
    int me = u->stm;
    int home = me == WHITE ? 0 : 7;
    int moved = p->sq[m->to];

    p->stm = me;
    p->castle = u->castle;
    p->ep = u->ep;
    p->half = u->half;
    if (me == BLACK) p->full--;

    if (m->promo) moved = me == WHITE ? PAWN : -PAWN;
    p->sq[m->from] = moved;
    p->sq[m->to] = 0;

    if (m->flag == F_EP) {
        int capsq = m->to + (me == WHITE ? -8 : 8);
        p->sq[capsq] = u->cap;
    } else if (u->cap != -1) {
        p->sq[m->to] = u->cap;
    }

    if (m->flag == F_CASTLE) {
        int rf = (m->to > m->from) ? 7 : 0;
        int rt = (m->to > m->from) ? 5 : 3;
        p->sq[SQ(rf, home)] = p->sq[SQ(rt, home)];
        p->sq[SQ(rt, home)] = 0;
    }
}

static int legal_moves(const pos *p, move *out, int max) {
    move buf[256];
    int n = gen_moves(p, buf, 256);
    int k = 0;
    for (int i = 0; i < n; i++) {
        undo u;
        pos c = *p;
        make_move(&c, &buf[i], &u);
        if (in_check(&c, p->stm)) continue;
        if (k < max) out[k++] = buf[i];
    }
    return k;
}

static void sq_str(int sq, char *out) {
    out[0] = (char)('a' + FILE_OF(sq));
    out[1] = (char)('1' + RANK_OF(sq));
    out[2] = 0;
}

static void uci_str(const move *m, char *out) {
    char a[3], b[3];
    sq_str(m->from, a);
    sq_str(m->to, b);
    out[0] = a[0];
    out[1] = a[1];
    out[2] = b[0];
    out[3] = b[1];
    out[4] = 0;
    if (m->promo) {
        const char *L = "  nbrq";
        out[4] = m->promo >= KNIGHT && m->promo <= QUEEN ? L[m->promo] : '?';
        out[5] = 0;
    }
}

/* A move written as e2e4 / e7e8q -> index into the legal list, or -1. */
static int find_uci(const pos *p, const char *s, move *out) {
    move list[256];
    int n = legal_moves(p, list, 256);
    if (strlen(s) < 4) return -1;
    for (int i = 0; i < n; i++) {
        char buf[8];
        uci_str(&list[i], buf);
        if (!strcmp(buf, s)) {
            *out = list[i];
            return i;
        }
    }
    return -1;
}

/* ---- notation ---------------------------------------------------------- */

static const char *piece_letter(int type) {
    switch (type) {
    case KNIGHT: return "N";
    case BISHOP: return "B";
    case ROOK:   return "R";
    case QUEEN:  return "Q";
    case KING:   return "K";
    default:     return "";
    }
}

static void san_of(const pos *p, const move *m, char *out) {
    int pc = p->sq[m->from];
    int type = pc > 0 ? pc : -pc;
    char to[3];
    sq_str(m->to, to);
    char *w = out;

    if (m->flag == F_CASTLE) {
        strcpy(out, m->to > m->from ? "O-O" : "O-O-O");
        w = out + strlen(out);
    } else if (type == PAWN) {
        if (p->sq[m->to] || m->flag == F_EP) *w++ = (char)('a' + FILE_OF(m->from));
    } else {
        const char *L = piece_letter(type);
        *w++ = L[0];
        /* disambiguation: another piece of the same type could reach the square */
        move list[256];
        int n = legal_moves(p, list, 256);
        int same_file = 0, same_rank = 0, ambiguous = 0;
        for (int i = 0; i < n; i++) {
            if (list[i].to != m->to || list[i].from == m->from) continue;
            int q = p->sq[list[i].from];
            int qt = q > 0 ? q : -q;
            if (qt != type) continue;
            ambiguous = 1;
            if (FILE_OF(list[i].from) == FILE_OF(m->from)) same_file = 1;
            if (RANK_OF(list[i].from) == RANK_OF(m->from)) same_rank = 1;
        }
        if (ambiguous) {
            if (!same_file) *w++ = (char)('a' + FILE_OF(m->from));
            else if (!same_rank) *w++ = (char)('1' + RANK_OF(m->from));
            else {
                *w++ = (char)('a' + FILE_OF(m->from));
                *w++ = (char)('1' + RANK_OF(m->from));
            }
        }
    }
    if (m->flag != F_CASTLE) {
        if (p->sq[m->to] || m->flag == F_EP) *w++ = 'x';
        strcpy(w, to);
        w += 2;
        if (m->promo) {
            *w++ = '=';
            *w++ = piece_letter(m->promo)[0];
        }
        *w = 0;
    }

    /* check and mate */
    pos c = *p;
    undo u;
    make_move(&c, m, &u);
    if (in_check(&c, c.stm)) {
        move reply[256];
        int n = legal_moves(&c, reply, 256);
        strcpy(w, n == 0 ? "#" : "+");
    }
}

static void fen_of(const pos *p, char *out, size_t n) {
    const char *sym = " pnbrqk";
    char *w = out;
    size_t used = 0;
    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            int pc = p->sq[SQ(f, r)];
            if (!pc) { empty++; continue; }
            if (empty) {
                if (used < n - 1) { *w++ = (char)('0' + empty); used++; }
                empty = 0;
            }
            char c = sym[pc > 0 ? pc : -pc];
            if (pc < 0) c = (char)toupper((unsigned char)c);
            if (used < n - 1) { *w++ = c; used++; }
        }
        if (empty) {
            if (used < n - 1) { *w++ = (char)('0' + empty); used++; }
        }
        if (r) {
            if (used < n - 1) { *w++ = '/'; used++; }
        }
    }
    *w = 0;
    w += strlen(w);
    *w++ = ' ';
    *w = 0;
    if (p->castle & CR_WK) *w++ = 'K';
    if (p->castle & CR_WQ) *w++ = 'Q';
    if (p->castle & CR_BK) *w++ = 'k';
    if (p->castle & CR_BQ) *w++ = 'q';
    if (!p->castle) *w++ = '-';
    *w++ = ' ';
    char ep[3];
    if (p->ep >= 0) { sq_str(p->ep, ep); *w++ = ep[0]; *w++ = ep[1]; }
    else *w++ = '-';
    snprintf(w, 24, " %d %d", p->half, p->full);
}

static void ascii_board(const pos *p, char *out, size_t n) {
    const char *sym = " pnbrqk";
    char *w = out;
    size_t used = 0;
    const char *hdr = "  +-----------------+\n";
    if (used + strlen(hdr) < n) { memcpy(w, hdr, strlen(hdr)); w += strlen(hdr); used += strlen(hdr); }
    for (int r = 7; r >= 0; r--) {
        char line[40];
        int k = snprintf(line, sizeof line, "%d |", r + 1);
        for (int f = 0; f < 8; f++) {
            int pc = p->sq[SQ(f, r)];
            char c = pc ? sym[pc > 0 ? pc : -pc] : '.';
            if (pc < 0) c = (char)toupper((unsigned char)c);
            line[k++] = ' ';
            line[k++] = c;
        }
        line[k++] = ' ';
        line[k++] = '|';
        line[k++] = '\n';
        line[k] = 0;
        size_t len = (size_t)k;
        if (used + len < n) { memcpy(w, line, len); w += len; used += len; }
    }
    const char *ftr = "  +-----------------+\n    a b c d e f g h\n";
    if (used + strlen(ftr) < n) { memcpy(w, ftr, strlen(ftr)); w += strlen(ftr); used += strlen(ftr); }
    *w = 0;
}

/* ---- rules that end a game -------------------------------------------- */

static int insufficient_material(const pos *p) {
    int knights = 0, bishops = 0, others = 0;
    for (int s = 0; s < 64; s++) {
        int pc = p->sq[s];
        if (!pc) continue;
        int t = pc > 0 ? pc : -pc;
        if (t == KING) continue;
        if (t == KNIGHT) knights++;
        else if (t == BISHOP) bishops++;
        else others++;
    }
    if (others) return 0;
    if (knights + bishops <= 1) return 1;
    return 0;
}

/* 0 = still playing, 1 = checkmate, 2 = stalemate, 3 = 50 move, 4 = material */
static int game_state(const pos *p) {
    move list[256];
    int n = legal_moves(p, list, 256);
    if (n == 0) return in_check(p, p->stm) ? 1 : 2;
    if (p->half >= 100) return 3;
    if (insufficient_material(p)) return 4;
    return 0;
}

/* ---- the API ----------------------------------------------------------- */

typedef struct {
    char key[256];
    char model[96];
    char url[256];
    char ca[256];
} conf;

static conf CFG;

static void conf_defaults(void) {
    memset(&CFG, 0, sizeof CFG);
    snprintf(CFG.model, sizeof CFG.model, "deepseek-flash");
    snprintf(CFG.url, sizeof CFG.url, "https://api.deepseek.com/v1/chat/completions");
#ifdef EK_HOST
    snprintf(CFG.ca, sizeof CFG.ca, "/etc/ssl/certs/ca-certificates.crt");
#else
    snprintf(CFG.ca, sizeof CFG.ca, "/mnt/us/chess-ca.pem");
#endif
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    return s;
}

/* One setting per line: key = value. Missing file is reported by the caller. */
static int conf_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line);
        if (!s[0] || s[0] == '#') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);
        if (!strcmp(k, "key")) snprintf(CFG.key, sizeof CFG.key, "%s", v);
        else if (!strcmp(k, "model")) snprintf(CFG.model, sizeof CFG.model, "%s", v);
        else if (!strcmp(k, "url")) snprintf(CFG.url, sizeof CFG.url, "%s", v);
        else if (!strcmp(k, "ca")) snprintf(CFG.ca, sizeof CFG.ca, "%s", v);
        else fprintf(stderr, "chess: unknown setting '%s' in %s\n", k, path);
    }
    fclose(f);
    return 0;
}

/* JSON string escaping: the prompt is all ASCII, so only these four matter. */
static void json_escape(const char *in, char *out, size_t n) {
    size_t o = 0;
    for (; *in && o + 8 < n; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
        else if (c == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else if (c == '\t') { out[o++] = '\\'; out[o++] = 't'; }
        else if (c < 0x20 || c > 0x7e) out[o++] = ' ';
        else out[o++] = (char)c;
    }
    out[o] = 0;
}

/* Pull choices[0].message.content out of the reply. "reasoning_content" does
 * not match this pattern (the quote before `content` is what is looked for),
 * so a thinking model's scratchpad is skipped. */
/* print the start of a reply on stderr, so a failure explains itself */
static void reveal_reply(const char *body) {
    int n = 0;
    const char *p = body;
    if ((p = strstr(p, "\r\n\r\n")) != NULL) p += 4;
    if (!p || !*p) p = body;
    fprintf(stderr, "chess: reply: ");
    for (; *p && n < 300; p++, n++)
        fputc(*p == '\n' || *p == '\r' ? ' ' : *p, stderr);
    fprintf(stderr, "\n");
}

static int json_content(const char *body, char *out, size_t n) {
    const char *pat = "\"content\":\"";
    const char *p = body;
    while ((p = strstr(p, pat)) != NULL) {
        p += strlen(pat);
        const char *q = p;
        size_t o = 0;
        while (*q && *q != '"' && o + 8 < n) {
            if (*q == '\\' && q[1]) {
                q++;
                switch (*q) {
                case 'n': out[o++] = '\n'; break;
                case 't': out[o++] = ' '; break;
                case 'r': break;
                case 'u': {
                    /* keep ASCII out of \u escapes; the 8x16 font has no use
                     * for anything else, and smart quotes read as noise */
                    unsigned v = 0;
                    for (int i = 1; i <= 4 && q[i]; i++) {
                        char h = q[i];
                        v <<= 4;
                        if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
                    }
                    q += 4;
                    if (v < 128) out[o++] = (char)v;
                    break;
                }
                default: out[o++] = *q; break;
                }
                q++;
            } else {
                out[o++] = *q++;
            }
        }
        out[o] = 0;
        if (o) return 0;
        p = q;
    }
    return -1;
}

/* Run curl with the JSON on its stdin and collect what it prints. */
static int run_curl(const char *json, char *out, size_t n, char *err, size_t en) {
    int in[2], op[2];
    if (pipe(in) != 0 || pipe(op) != 0) {
        snprintf(err, en, "could not make a pipe");
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(err, en, "could not start curl");
        return -1;
    }
    if (pid == 0) {
        dup2(in[0], 0);
        dup2(op[1], 1);
        close(in[0]); close(in[1]);
        close(op[0]); close(op[1]);
        char auth[320];
        snprintf(auth, sizeof auth, "Authorization: Bearer %s", CFG.key);
        execlp("curl", "curl", "-s", "-S", "-m", "180",
               "--cacert", CFG.ca,
               "-H", "Content-Type: application/json",
               "-H", auth,
               "--data-binary", "@-",
               "-w", "\n%{http_code}",
               CFG.url, (char *)NULL);
        fprintf(stderr, "chess: could not run curl\n");
        _exit(127);
    }
    close(in[0]);
    close(op[1]);

    size_t off = 0;
    size_t len = strlen(json);
    while (off < len) {
        ssize_t w = write(in[1], json + off, len - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
    close(in[1]);

    /* Wait for the reply, but keep an eye on the power button: a press during
     * a three-minute wait must not be swallowed. kframe_pwr_presses reads the
     * kernel log for PWRON1. The PMIC counter is the wrong signal. */
    size_t got = 0;
    int cancelled = 0;
    for (;;) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(op[0], &rd);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200 * 1000;
        int sel = select(op[0] + 1, &rd, NULL, NULL, &tv);
        if (sel < 0) {
            if (errno == EINTR) continue;
            snprintf(err, en, "the wait for curl failed: %s", strerror(errno));
            break;
        }
        if (sel == 0) {
            if (kframe_pwr_presses() != 0) { cancelled = 1; break; }
            continue;
        }
        ssize_t r = read(op[0], out + got, n - 1 - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            snprintf(err, en, "could not read curl's output: %s", strerror(errno));
            break;
        }
        if (r == 0) break;
        got += (size_t)r;
        if (got >= n - 1) break;
    }
    out[got] = 0;
    close(op[0]);

    int st = 0;
    if (cancelled) {
        kill(pid, SIGTERM);
        waitpid(pid, &st, 0);
        kframe_pwr_set_latch(1);
        snprintf(err, en, "cancelled by the power button");
        return -1;
    }
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        snprintf(err, en, "curl failed (status %d)", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
        return -1;
    }
    return 0;
}

/* Read the answer out of a reply body. g_transient says the failure is the
 * model's, not ours, so asking again is worth one round trip. */
static int g_transient;

static int parse_reply(const pos *p, char *reply, move *out, char *say, size_t sayn,
                       char *err, size_t en) {
    g_transient = 0;

    char *nlp = strrchr(reply, '\n');
    int code = 0;
    if (nlp) code = atoi(nlp + 1);
    else snprintf(err, en, "the reply had no status line");
    if (code != 200) {
        char bit[120];
        size_t bl = strlen(reply);
        if (bl > sizeof bit - 1) bl = sizeof bit - 1;
        memcpy(bit, reply, bl);
        bit[bl] = 0;
        char *e2 = strchr(bit, '\n');
        if (e2) *e2 = 0;
        snprintf(err, en, "http %d: %.100s", code, bit);
        return -1;
    }

    static char content[16384];
    if (json_content(reply, content, sizeof content) != 0) {
        /* show what did come back: an empty content with a reasoning_content is
         * the usual culprit, and without the body there is no way to tell */
        snprintf(err, en, "the reply had no message in it");
        g_transient = 1;
        reveal_reply(reply);
        return -1;
    }
    if (!content[0]) {
        snprintf(err, en, "the reply was empty (reasoning used up max_tokens?)");
        g_transient = 1;
        reveal_reply(reply);
        return -1;
    }

    /* MOVE: e7e5 / SAY: ... */
    char *mp = strstr(content, "MOVE:");
    char uci[16] = "";
    if (mp) {
        mp += 5;
        while (*mp == ' ' || *mp == '*') mp++;
        int i = 0;
        while (i < 7 && (isalnum((unsigned char)mp[i]))) uci[i] = (char)tolower((unsigned char)mp[i]), i++;
        uci[i] = 0;
    }
    if (!uci[0]) {
        snprintf(err, en, "the reply had no MOVE line");
        g_transient = 1;
        return -1;
    }
    if (find_uci(p, uci, out) < 0) {
        snprintf(err, en, "'%s' is not a legal move", uci);
        g_transient = 1;
        return -1;
    }
    char *sp = strstr(content, "SAY:");
    snprintf(say, sayn, "%s", sp ? trim(sp + 4) : "");
    if (!say[0]) snprintf(say, sayn, "(no comment)");
    return 0;
}

/* Ask for the next move. Returns 0 and fills both buffers, or -1 with err. */
static int api_move(const pos *p, const char *history, move *out, char *say, size_t sayn,
                    char *err, size_t en) {
    if (p->stm != BLACK) {
        snprintf(err, en, "asked the api out of turn");
        return -1;
    }
    if (!CFG.key[0]) {
        snprintf(err, en, "no api key: set key = ... in the config file");
        return -1;
    }
    char fen[128], board[512], legal[2048];
    fen_of(p, fen, sizeof fen);
    ascii_board(p, board, sizeof board);

    move list[256];
    int nl = legal_moves(p, list, 256);
    legal[0] = 0;
    for (int i = 0; i < nl; i++) {
        char u[8], buf[12];
        uci_str(&list[i], u);
        snprintf(buf, sizeof buf, "%s%s", i ? " " : "", u);
        if (strlen(legal) + strlen(buf) + 1 < sizeof legal) strcat(legal, buf);
    }

    const char *sys =
        "You are playing Black in a game of chess against a human on a Kindle. "
        "Answer with exactly two lines and nothing else. "
        "Line one: MOVE: <your move in UCI, for example e7e5 or e7e8q>. "
        "Line two: SAY: <one line of friendly trash talk, at most 18 words, no quotes>.";

    char prompt[4096];
    snprintf(prompt, sizeof prompt,
             "You are Black. It is your turn.\n"
             "FEN: %s\n"
             "Moves so far: %s\n"
             "%s"
             "Legal moves for Black (UCI): %s\n"
             "Pick one of those legal moves.",
             fen, history[0] ? history : "(none)", board, legal);

    char jsys[1024], jprompt[6144], json[8192];
    json_escape(sys, jsys, sizeof jsys);
    json_escape(prompt, jprompt, sizeof jprompt);
    int k = snprintf(json, sizeof json,
                     "{\"model\":\"%s\",\"messages\":["
                     "{\"role\":\"system\",\"content\":\"%s\"},"
                     "{\"role\":\"user\",\"content\":\"%s\"}],"
                     "\"max_tokens\":1200,\"temperature\":0.4}",
                     CFG.model, jsys, jprompt);
    if (k <= 0 || k >= (int)sizeof json) {
        snprintf(err, en, "the request did not fit in memory");
        return -1;
    }

    static char reply[65536];
    /* two goes: a reasoning model sometimes spends the whole budget
     * thinking and hands back an empty message */
    int ok = 0;
    for (int att = 0; att < 2 && !ok; att++) {
        if (run_curl(json, reply, sizeof reply, err, en) != 0) return -1;
        ok = parse_reply(p, reply, out, say, sayn, err, en) == 0;
        if (!ok && !g_transient) break;
    }
    return ok ? 0 : -1;
}

/* ---- drawing ----------------------------------------------------------- */

#define SQ_PX    68
#define BOARD_X  28
#define BOARD_Y  64
#define BOARD_END (BOARD_Y + 8 * SQ_PX)
#define COMMENT_Y (BOARD_END + 8)

/* shape helpers: everything is drawn in a 0..100 box and scaled */
static int g_ox, g_oy, g_size, g_ink;

static int sx(int v) { return g_ox + v * g_size / 100; }
static int sy(int v) { return g_oy + v * g_size / 100; }

static void s_rect(int x0, int y0, int x1, int y1) {
    ek_fill_rect(sx(x0), sy(y0), (x1 - x0) * g_size / 100, (y1 - y0) * g_size / 100, g_ink);
}

static void s_circle(int cx, int cy, int r) {
    ek_fill_circle(sx(cx), sy(cy), r * g_size / 100, g_ink);
}

static void s_poly(const int *xs, const int *ys, int n) {
    int top = 1000, bot = -1000;
    int px[16], py[16];
    for (int i = 0; i < n; i++) {
        px[i] = sx(xs[i]);
        py[i] = sy(ys[i]);
        if (py[i] < top) top = py[i];
        if (py[i] > bot) bot = py[i];
    }
    for (int y = top; y <= bot; y++) {
        int lo = 10000, hi = -10000;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            int y0 = py[i], y1 = py[j];
            if (y0 == y1) continue;
            if (y < (y0 < y1 ? y0 : y1) || y > (y0 < y1 ? y1 : y0)) continue;
            int x = px[i] + (y - y0) * (px[j] - px[i]) / (y1 - y0);
            if (x < lo) lo = x;
            if (x > hi) hi = x;
        }
        if (hi >= lo) ek_hline(lo, y, hi - lo + 1, g_ink);
    }
}

static void shape(int type) {
    static const int pawn_x[] = { 38, 62, 72, 28 };
    static const int pawn_y[] = { 62, 62, 86, 86 };
    static const int knight_x[] = { 28, 28, 38, 34, 42, 54, 66, 68, 74, 74 };
    static const int knight_y[] = { 88, 62, 44, 32, 18, 14, 22, 36, 50, 88 };
    static const int bishop_x[] = { 50, 62, 66, 34, 38 };
    static const int bishop_y[] = { 26, 48, 76, 76, 48 };
    static const int skirt_x[] = { 34, 66, 72, 28 };
    static const int skirt_y[] = { 50, 50, 86, 86 };

    switch (type) {
    case PAWN:
        s_circle(50, 30, 14);
        s_poly(pawn_x, pawn_y, 4);
        s_rect(24, 86, 76, 93);
        break;
    case ROOK:
        s_rect(22, 86, 78, 93);
        s_rect(34, 46, 66, 86);
        s_rect(30, 38, 70, 47);
        s_rect(30, 24, 42, 39);
        s_rect(44, 24, 56, 39);
        s_rect(58, 24, 70, 39);
        break;
    case KNIGHT:
        s_poly(knight_x, knight_y, 10);
        s_rect(26, 86, 74, 93);
        break;
    case BISHOP:
        s_rect(26, 86, 74, 93);
        s_rect(34, 76, 66, 86);
        s_poly(bishop_x, bishop_y, 5);
        s_circle(50, 22, 7);
        break;
    case QUEEN:
        s_rect(24, 86, 76, 93);
        s_poly(skirt_x, skirt_y, 4);
        s_rect(34, 38, 66, 52);
        s_rect(26, 32, 74, 40);
        s_circle(26, 30, 6);
        s_circle(38, 26, 6);
        s_circle(50, 24, 6);
        s_circle(62, 26, 6);
        s_circle(74, 30, 6);
        break;
    case KING:
        s_rect(24, 86, 76, 93);
        s_poly(skirt_x, skirt_y, 4);
        s_rect(34, 38, 66, 52);
        s_rect(38, 30, 62, 38);
        s_rect(46, 10, 54, 34);
        s_rect(38, 16, 62, 24);
        break;
    default:
        break;
    }
}

static void draw_piece(int type, int black, int x, int y, int size) {
    int fill = black ? EK_INK : EK_PAPER;
    int rim = black ? EK_PAPER : EK_INK;
    /* two rings of rim: a 1-pixel outline of a white piece on a white square
     * is nearly invisible on this panel, and e-ink ghosting eats thin lines */
    static const int off[12][2] = {
        {-1,0},{1,0},{0,-1},{0,1},
        {-2,0},{2,0},{0,-2},{0,2},
        {-1,-1},{1,-1},{-1,1},{1,1}
    };
    for (int i = 0; i < 12; i++) {
        g_ox = x + off[i][0];
        g_oy = y + off[i][1];
        g_size = size;
        g_ink = rim;
        shape(type);
    }
    g_ox = x;
    g_oy = y;
    g_size = size;
    g_ink = fill;
    shape(type);
}

/* ---- game state ------------------------------------------------------- */

static pos  P;
static int  sel = -1;             /* the square the human has tapped */
static move legal_now[256];
static int  nlegal;
static char remark[512];          /* what the model said */
static char status[160];
static char hist_uci[2048];       /* space separated, for the prompt */
static char hist_san[1024];       /* the last few moves, for the screen */
static int  over;                 /* 0 playing, otherwise game_state() */
static int  promo_from = -1, promo_to = -1;
static int  api_busy;
static volatile int stop_req;
static volatile int shot_req;      /* SIGUSR1: save a picture of the screen */
#define SHOT_PATH "/tmp/chess-shot.pgm"
#ifndef EK_HOST
static int  down_seen, down_ms;   /* touch debounce: DOWN repeats until UP */
#endif

static void on_signal(int sig) {
    (void)sig;
    stop_req = 1;
}

/* kill -USR1 <pid> writes the current screen to /tmp/chess-shot.pgm, which is
 * the only way to see what a game running on a Kindle nobody is holding looks
 * like. The write happens in the main loop, not here. */
static void on_shot_signal(int sig) {
    (void)sig;
    shot_req = 1;
}

static void set_status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status, sizeof status, fmt, ap);
    va_end(ap);
}

static void note_move(const pos *p, const move *m) {
    char u[8], s[24];
    uci_str(m, u);
    san_of(p, m, s);
    if (hist_uci[0]) strncat(hist_uci, " ", sizeof hist_uci - strlen(hist_uci) - 1);
    strncat(hist_uci, u, sizeof hist_uci - strlen(hist_uci) - 1);
    /* the screen shows the last six moves only */
    if (hist_san[0]) strncat(hist_san, " ", sizeof hist_san - strlen(hist_san) - 1);
    strncat(hist_san, s, sizeof hist_san - strlen(hist_san) - 1);
    if (strlen(hist_san) > 300) {
        char *sp = strchr(hist_san, ' ');
        if (sp) memmove(hist_san, sp + 1, strlen(sp));
    }
    (void)p;
}

static void check_over(void) {
    over = game_state(&P);
    switch (over) {
    case 1:
        set_status(P.stm == WHITE ? "checkmate: the computer wins" : "checkmate: you win");
        break;
    case 2: set_status("stalemate: a draw"); break;
    case 3: set_status("draw: fifty moves without a pawn move or a capture"); break;
    case 4: set_status("draw: not enough pieces left to mate"); break;
    default: break;
    }
}

static void new_game(void) {
    pos_init(&P);
    sel = -1;
    nlegal = 0;
    remark[0] = 0;
    hist_uci[0] = 0;
    hist_san[0] = 0;
    over = 0;
    promo_from = promo_to = -1;
    api_busy = 0;
    set_status("your turn - you are white");
}

static void present(void);

/* Ask the API for Black's move and play it. Nothing is played unless the API
 * answers with a legal move. */
static int play_ai(void) {
    char err[200] = "";
    char say[256] = "";
    move m;
    api_busy = 1;
    set_status("the computer is thinking ...");
    present();

    /* one retry: a model that wrote an illegal move is usually right the
     * second time once it is told what went wrong */
    int rc = api_move(&P, hist_uci, &m, say, sizeof say, err, sizeof err);
    if (kframe_pwr_latched()) { api_busy = 0; return -2; }    /* the loop sleeps, then asks again */
    if (rc != 0) {
        char first[200];
        snprintf(first, sizeof first, "%s", err);
        char hist2[2400];
        snprintf(hist2, sizeof hist2, "%.2000s (your last answer was refused: %.180s. "
                 "Answer only with a legal move from the list.)", hist_uci, first);
        rc = api_move(&P, hist2, &m, say, sizeof say, err, sizeof err);
        if (kframe_pwr_latched()) { api_busy = 0; return -2; }
        if (rc != 0) {
            api_busy = 0;
            set_status("computer: %.120s", err);
            return -1;
        }
    }
    api_busy = 0;
    note_move(&P, &m);
    undo u;
    make_move(&P, &m, &u);
    snprintf(remark, sizeof remark, "%s", say);
    sel = -1;
    check_over();
    if (!over) set_status("your turn");
    return 0;
}

/* ---- the screen ------------------------------------------------------- */

static void wrap(const char *s, int x, int y, int maxchars, int lines) {
    char buf[600];
    snprintf(buf, sizeof buf, "%s", s);
    char *p = buf;
    for (int n = 0; n < lines && *p; n++) {
        char *cut = p;
        if (strlen(p) > (size_t)maxchars) {
            cut = p + maxchars;
            while (cut > p && *cut != ' ') cut--;
            if (cut == p) cut = p + maxchars;
        } else {
            cut = p + strlen(p);
        }
        char save = *cut;
        *cut = 0;
        ek_text(x, y + n * 18, p, EK_INK);
        if (!save) break;
        p = cut + 1;
        while (*p == ' ') p++;
    }
}

static void draw_board(void) {
    for (int r = 0; r < 8; r++) {
        for (int f = 0; f < 8; f++) {
            int x = BOARD_X + f * SQ_PX;
            int y = BOARD_Y + (7 - r) * SQ_PX;
            if ((f + r) & 1) ek_dither_rect(x, y, SQ_PX, SQ_PX, 3);
            else ek_fill_rect(x, y, SQ_PX, SQ_PX, EK_PAPER);
            ek_rect(x, y, SQ_PX, SQ_PX, EK_INK);
        }
    }
    /* the human's legal targets, then the pieces over the top */
    if (sel >= 0) {
        for (int i = 0; i < nlegal; i++) {
            if (legal_now[i].from != sel) continue;
            int f = FILE_OF(legal_now[i].to), r = RANK_OF(legal_now[i].to);
            int x = BOARD_X + f * SQ_PX, y = BOARD_Y + (7 - r) * SQ_PX;
            if (P.sq[legal_now[i].to]) ek_rect(x + 2, y + 2, SQ_PX - 4, SQ_PX - 4, EK_INK);
            else ek_fill_rect(x + SQ_PX / 2 - 6, y + SQ_PX / 2 - 6, 12, 12, EK_INK);
        }
        int f = FILE_OF(sel), r = RANK_OF(sel);
        int x = BOARD_X + f * SQ_PX, y = BOARD_Y + (7 - r) * SQ_PX;
        ek_rect(x, y, SQ_PX, SQ_PX, EK_INK);
        ek_rect(x + 1, y + 1, SQ_PX - 2, SQ_PX - 2, EK_INK);
        ek_rect(x + 2, y + 2, SQ_PX - 4, SQ_PX - 4, EK_INK);
    }
    for (int s = 0; s < 64; s++) {
        int pc = P.sq[s];
        if (!pc) continue;
        int f = FILE_OF(s), r = RANK_OF(s);
        int x = BOARD_X + f * SQ_PX;
        int y = BOARD_Y + (7 - r) * SQ_PX;
        draw_piece(pc > 0 ? pc : -pc, pc < 0, x + 8, y + 8, SQ_PX - 16);
    }
}

static void draw_promo(void) {
    int y = COMMENT_Y;
    ek_fill_rect(0, y, EK_W, 120, EK_PAPER);
    ek_rect(0, y, EK_W, 120, EK_INK);
    ek_text(12, y + 8, "PROMOTE TO:", EK_INK);
    static const int types[4] = { QUEEN, ROOK, BISHOP, KNIGHT };
    static const char *names[4] = { "QUEEN", "ROOK", "BISHOP", "KNIGHT" };
    for (int i = 0; i < 4; i++) {
        int x = 20 + i * 142;
        ek_rect(x, y + 34, 130, 74, EK_INK);
        draw_piece(types[i], 0, x + 34, y + 36, 46);
        ek_text(x + 8, y + 96, names[i], EK_INK);
    }
}

static void present(void) {
    if (kframe_covering()) {
        kframe_paint();
        ek_flush();
        return;
    }
    ek_clear(EK_PAPER);

    ek_text(12, 8, "CHESS", EK_INK);
    ek_text(64, 8, "- against", EK_INK);
    ek_text(136, 8, CFG.model, EK_INK);
    ek_text(12, 28, status, EK_INK);
    if (over) {
        ek_text(12, 46, "open the menu (top right) and tap NEW", EK_INK);
    } else if (api_busy) {
        ek_text(12, 46, "waiting for the api (up to 3 minutes)", EK_INK);
    } else {
        ek_text(12, 46, hist_san[0] ? hist_san : "you are white: tap a piece, then where it goes", EK_INK);
    }

    draw_board();

    if (promo_from >= 0) {
        draw_promo();
    } else {
        if (remark[0]) wrap(remark, 12, COMMENT_Y, 72, 5);
        else ek_text(12, COMMENT_Y, "(the computer has not said anything yet)", EK_INK);
    }
    ek_text(12, 740, "top right opens quit, volume, wi-fi", EK_INK);
    kframe_paint();
    ek_flush();
}

/* ---- taps ------------------------------------------------------------- */

static int square_at(int x, int y, int *out) {
    if (x < BOARD_X || x >= BOARD_X + 8 * SQ_PX) return 0;
    if (y < BOARD_Y || y >= BOARD_END) return 0;
    int f = (x - BOARD_X) / SQ_PX;
    int r = 7 - (y - BOARD_Y) / SQ_PX;
    *out = SQ(f, r);
    return 1;
}

/* The human's move. Returns 1 if Black now has to answer. */
static int human_move(int from, int to) {
    move list[256];
    int n = legal_moves(&P, list, 256);
    for (int i = 0; i < n; i++) {
        if (list[i].from != from || list[i].to != to) continue;
        if (list[i].promo) {
            promo_from = from;
            promo_to = to;
            return 0;
        }
        note_move(&P, &list[i]);
        undo u;
        make_move(&P, &list[i], &u);
        sel = -1;
        nlegal = 0;
        check_over();
        if (!over) set_status("the computer is thinking ...");
        return 1;
    }
    return -1;
}

static void do_promo(int type) {
    move list[256];
    int n = legal_moves(&P, list, 256);
    for (int i = 0; i < n; i++) {
        if (list[i].from == promo_from && list[i].to == promo_to && list[i].promo == type) {
            note_move(&P, &list[i]);
            undo u;
            make_move(&P, &list[i], &u);
            promo_from = promo_to = -1;
            sel = -1;
            nlegal = 0;
            check_over();
            if (!over) set_status("the computer is thinking ...");
            return;
        }
    }
    fprintf(stderr, "chess: promotion to %d was not offered\n", type);
    promo_from = promo_to = -1;
}

/* Returns 1 when the screen should be drawn again.
 * -2 means Black has to answer (a move, or RETRY from the menu). */
static int handle_tap(int x, int y) {
    int kf = kframe_tap(x, y);
    if (kf != 0) return kf;
    if (over) return 0;

    if (promo_from >= 0) {
        static const int types[4] = { QUEEN, ROOK, BISHOP, KNIGHT };
        if (y >= COMMENT_Y + 34 && y <= COMMENT_Y + 108) {
            for (int i = 0; i < 4; i++) {
                int bx = 20 + i * 142;
                if (x >= bx && x < bx + 130) {
                    do_promo(types[i]);
                    return P.stm == BLACK ? -2 : 1;
                }
            }
        }
        return 0;
    }

    int sq;
    if (!square_at(x, y, &sq)) return 0;
    if (P.stm != WHITE) {
        set_status("wait for the computer");
        return 1;
    }

    if (sel < 0) {
        int pc = P.sq[sq];
        if (!pc || pc < 0) {
            set_status("tap one of your own pieces");
            return 1;
        }
        sel = sq;
        int n = legal_moves(&P, legal_now, 256);
        nlegal = n;
        set_status("tap where it goes");
        return 1;
    }

    if (sq == sel) {
        sel = -1;
        set_status("your turn - you are white");
        return 1;
    }

    int rc = human_move(sel, sq);
    if (rc < 0) {
        int pc = P.sq[sq];
        if (pc > 0) {
            sel = sq;                       /* another of your pieces */
            nlegal = legal_moves(&P, legal_now, 256);
            set_status("tap where it goes");
        } else {
            set_status("that piece cannot go there");
        }
        return 1;
    }
    if (promo_from >= 0) return 1;
    return rc == 1 ? -2 : 1;                /* -2: Black must answer */
}

/* ---- self test -------------------------------------------------------- */

static long perft(const pos *p, int depth) {
    move list[256];
    int n = legal_moves(p, list, 256);
    if (depth <= 1) return n;
    long total = 0;
    for (int i = 0; i < n; i++) {
        pos c = *p;
        undo u;
        make_move(&c, &list[i], &u);
        total += perft(&c, depth - 1);
    }
    return total;
}

static int fails;
static void want(long got, long expect, const char *what) {
    if (got == expect) {
        printf("  ok   %-28s %ld\n", what, got);
    } else {
        printf("  FAIL %-28s got %ld want %ld\n", what, got, expect);
        fails++;
    }
}

static int selftest(void) {
    pos p;
    pos_init(&p);
    printf("perft from the opening position:\n");
    want(perft(&p, 1), 20, "depth 1");
    want(perft(&p, 2), 400, "depth 2");
    want(perft(&p, 3), 8902, "depth 3");
    want(perft(&p, 4), 197281, "depth 4");

    /* a position with castling, en passant and a pin: "kiwipete" */
    pos k = p;
    printf("rules:\n");
    move mv[256];
    int n = legal_moves(&p, mv, 256);
    int found_oo = 0;
    for (int i = 0; i < n; i++) if (mv[i].flag == F_CASTLE) found_oo++;
    want(found_oo, 0, "no castling through pieces");

    /* a quick kiwipete by hand */
    const char *kf = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    /* build it: only the perft matters, so parse the FEN field by field */
    {
        memset(&k, 0, sizeof k);
        int r = 7, f = 0;
        const char *s = kf;
        for (; *s && *s != ' '; s++) {
            if (*s == '/') { r--; f = 0; continue; }
            if (*s >= '1' && *s <= '8') { f += *s - '0'; continue; }
            const char *sym = " pnbrqk";
            char lo = (char)tolower((unsigned char)*s);
            int type = (int)(strchr(sym, lo) - sym);
            k.sq[SQ(f, r)] = (*s >= 'A' && *s <= 'Z') ? type : -type;
            f++;
        }
        k.stm = WHITE;
        k.castle = CR_WK | CR_WQ | CR_BK | CR_BQ;
        k.ep = -1;
        k.half = 0;
        k.full = 1;
    }
    want(perft(&k, 1), 48, "kiwipete depth 1");
    want(perft(&k, 2), 2039, "kiwipete depth 2");
    want(perft(&k, 3), 97862, "kiwipete depth 3");

    /* checkmate detection */
    {
        const char *mf = "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3";
        memset(&k, 0, sizeof k);
        int r = 7, f = 0;
        const char *s = mf;
        for (; *s && *s != ' '; s++) {
            if (*s == '/') { r--; f = 0; continue; }
            if (*s >= '1' && *s <= '8') { f += *s - '0'; continue; }
            const char *sym = " pnbrqk";
            char lo = (char)tolower((unsigned char)*s);
            int type = (int)(strchr(sym, lo) - sym);
            k.sq[SQ(f, r)] = (*s >= 'A' && *s <= 'Z') ? type : -type;
            f++;
        }
        k.stm = WHITE;
        k.castle = CR_WK | CR_WQ | CR_BK | CR_BQ;
        k.ep = -1;
        want(game_state(&k), 1, "fool's mate is mate");
    }

    /* a move written the way the model writes it */
    pos_init(&p);
    move m;
    want(find_uci(&p, "e2e4", &m) >= 0, 1, "e2e4 is legal");
    want(find_uci(&p, "e2e5", &m) < 0, 1, "e2e5 is not");
    char san[24];
    find_uci(&p, "g1f3", &m);
    san_of(&p, &m, san);
    want(strcmp(san, "Nf3") == 0, 1, "g1f3 is Nf3");

    /* the power key: only KEY_POWER from the key device counts */
    want(kframe_count_pwr_records("<6>pmic: PWRON1 event\n<6>pmic: PWRON1 release\n"), 2,
         "both records of one press are counted");
    want(kframe_count_pwr_records("<6>pmic: CHGINS event\n<6>pmic: PWRON2 event\n"), 0,
         "charger and other pmic lines are not the button");
    want(kframe_count_pwr_records("<7>whitney-button: unknown key\n"), 0,
         "another kernel record is not the power button");
    {
        /* A ring buffer turning over: 300 bytes of old text, new text appended,
         * the oldest bytes dropped. pwr_log_grew must report exactly the number
         * of bytes written, and only those bytes may be searched for a press. */
        static char prev[1024], cur[1024];
        memset(prev, 'x', sizeof prev);
        memcpy(prev + 400, "<6>pmic: PWRON1 event\n", 22);
        memcpy(cur, prev + 300, 700);                       /* dropped 300 */
        memcpy(cur + 700, "<6>usb 1-1: new high speed USB device\n", 37);
        int grew = kframe_pwr_log_grew(prev, cur, 1000);
        want(grew, 300, "the log says exactly how many bytes it grew by");
        want(kframe_count_pwr_records(cur + 1000 - grew), 0,
             "a press from before the last look is not a new press");
        memcpy(prev, cur, 1000);
        memcpy(cur, prev + 100, 900);                        /* dropped 100 */
        memcpy(cur + 900, "<6>pmic: PWRON1 event\n", 22);
        grew = kframe_pwr_log_grew(prev, cur, 1000);
        want(grew, 100, "a press that scrolls an old line out still counts");
        want(kframe_count_pwr_records(cur + 1000 - grew), 1, "the new press is found");
    }

    /* SAN of a castle and of a capture */
    {
        const char *sf = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
        memset(&k, 0, sizeof k);
        int r = 7, f = 0;
        const char *s = sf;
        for (; *s && *s != ' '; s++) {
            if (*s == '/') { r--; f = 0; continue; }
            if (*s >= '1' && *s <= '8') { f += *s - '0'; continue; }
            const char *sym = " pnbrqk";
            char lo = (char)tolower((unsigned char)*s);
            int type = (int)(strchr(sym, lo) - sym);
            k.sq[SQ(f, r)] = (*s >= 'A' && *s <= 'Z') ? type : -type;
            f++;
        }
        k.stm = WHITE;
        k.castle = CR_WK | CR_WQ | CR_BK | CR_BQ;
        k.ep = -1;
        int ok = 0;
        for (int i = 0; i < legal_moves(&k, mv, 256); i++)
            if (mv[i].flag == F_CASTLE && mv[i].to == SQ(6, 0)) {
                san_of(&k, &mv[i], san);
                ok = strcmp(san, "O-O") == 0;
            }
        want(ok, 1, "e1g1 is O-O");
    }

    /* en passant: white pawn e5, black plays d7d5, white takes d6 */
    pos_init(&p);
    find_uci(&p, "e2e4", &m); { undo u; make_move(&p, &m, &u); }
    find_uci(&p, "a7a6", &m); { undo u; make_move(&p, &m, &u); }
    find_uci(&p, "e4e5", &m); { undo u; make_move(&p, &m, &u); }
    find_uci(&p, "d7d5", &m); { undo u; make_move(&p, &m, &u); }
    int ep_ok = find_uci(&p, "e5d6", &m) >= 0 && m.flag == F_EP;
    want(ep_ok, 1, "en passant e5d6");

    /* make/unmake has to put the position back exactly */
    {
        pos a = p;
        move m2;
        undo u;
        find_uci(&a, "e2e4", &m2);
        pos before = a;
        make_move(&a, &m2, &u);
        unmake_move(&a, &m2, &u);
        want(memcmp(&a.sq, &before.sq, sizeof a.sq) == 0 && a.stm == before.stm &&
             a.castle == before.castle && a.ep == before.ep, 1, "make then unmake");
    }

    printf("%s\n", fails ? "SELFTEST FAILED" : "selftest clean");
    return fails;
}

/* ---- the shared menu -------------------------------------------------- */

enum { CHESS_NEW = 1, CHESS_RETRY = 2 };

static const kframe_item CHESS_ITEMS[] = {
    { "NEW",   CHESS_NEW },
    { "RETRY", CHESS_RETRY },
};

static void kf_status(const char *msg) { set_status("%s", msg); }
static void kf_quit(void) { stop_req = 1; }

static int on_chess_item(int id) {
    if (id == CHESS_NEW) { new_game(); return 1; }
    if (id == CHESS_RETRY) {
        if (over) {
            set_status("the game is over - tap NEW");
            return 1;
        }
        if (P.stm == BLACK && !api_busy) return -2;
        set_status("the computer does not owe you a move");
        return 1;
    }
    fprintf(stderr, "chess: menu item %d is not one of ours\n", id);
    return 1;
}

static int open_frame(void) {
    kframe_cfg kf;
    memset(&kf, 0, sizeof kf);
    kf.title = "CHESS";
    kf.items = CHESS_ITEMS;
    kf.nitems = 2;
    kf.on_item = on_chess_item;
    kf.on_quit = kf_quit;
    kf.repaint = present;
    kf.status = kf_status;
    kf.stop = &stop_req;
    kf.shot_req = &shot_req;
    kf.shot_path = SHOT_PATH;
    if (kframe_init(&kf) != 0) {
        fprintf(stderr, "chess: the shared menu would not start\n");
        return -1;
    }
    return 0;
}

/* ---- main ------------------------------------------------------------- */

static const char *conf_default(void) {
#ifdef EK_HOST
    return "chess.conf";
#else
    return "/mnt/us/chess.conf";
#endif
}

static void usage(void) {
    fprintf(stderr,
            "chess -- you are white, deepseek is black, on the Kindle's screen.\n"
            "\n"
            "  --conf FILE   settings: key, model, url, ca (default %s)\n"
            "  --selftest    check the rules here and exit\n"
            "  --apicheck    ask the api for one move from the opening and print it\n"
            "  --auto N      play N half moves (white takes the first legal move)\n"
            "  --fake-ai     black answers without the api, for testing the screen\n"
            "  --dump FILE   write the picture (PGM) and exit\n"
            "  --no-takeover do not hold the Kindle's screen (for testing)\n"
            "  --wifi        open the wi-fi screen even when already online\n"
            "  --wifi-only   open the wi-fi screen, then exit without a board\n"
            "  --no-wifi     do not check wi-fi on start\n"
            "  --wifi-shot F write a wi-fi screen (PGM) and exit: F then\n"
            "                list|name|pass|fail, default list\n"
            "  --menu-shot F write the shared menu (PGM) and exit\n"
            "\n"
            "The top-right button opens the shared menu: QUIT (tap twice),\n"
            "VOLUME, WIFI, then this game's NEW and RETRY. The power button\n"
            "puts the Kindle to sleep and wakes it again; the position is kept.\n"
            "A press during the model's turn cancels that question and asks it\n"
            "again after the wake.\n",
            conf_default());
}

int main(int argc, char **argv) {
    unsigned flags = EK_TAKEOVER;
    int do_test = 0, api_check = 0, fake = 0, autoplay = -1;
    int no_wifi = 0, force_wifi = 0, wifi_only = 0;
    const char *dump_path = NULL, *wifi_shot = NULL, *menu_shot = NULL;
    const char *wifi_shot_what = "list";
    const char *conf_path = conf_default();

    conf_defaults();
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--conf") && i + 1 < argc) conf_path = argv[++i];
        else if (!strcmp(argv[i], "--selftest")) do_test = 1;
        else if (!strcmp(argv[i], "--apicheck")) api_check = 1;
        else if (!strcmp(argv[i], "--fake-ai")) fake = 1;
        else if (!strcmp(argv[i], "--auto") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v < 0 || v > 200) { usage(); return 2; }
            autoplay = (int)v;
        } else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump_path = argv[++i];
        else if (!strcmp(argv[i], "--wifi")) force_wifi = 1;
        else if (!strcmp(argv[i], "--wifi-only")) wifi_only = 1;
        else if (!strcmp(argv[i], "--no-wifi")) no_wifi = 1;
        else if (!strcmp(argv[i], "--wifi-shot") && i + 1 < argc) {
            wifi_shot = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') wifi_shot_what = argv[++i];
        }
        else if (!strcmp(argv[i], "--menu-shot") && i + 1 < argc) menu_shot = argv[++i];
        else if (!strcmp(argv[i], "--no-takeover")) flags &= ~EK_TAKEOVER;
        else if (!strcmp(argv[i], "--help")) { usage(); return 0; }
        else { usage(); return 2; }
    }

    if (conf_load(conf_path) != 0)
        fprintf(stderr, "chess: no config at %s (the api needs one)\n", conf_path);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sa.sa_handler = on_shot_signal;
    sigaction(SIGUSR1, &sa, NULL);

    if (open_frame() != 0) return 1;

    if (ek_open(flags) != 0) {
        fprintf(stderr, "chess: the screen would not open\n");
        ek_restore_ui();
        return 1;
    }
    ek_mono(1);
    ek_wait_updates(1);
    ek_waveform(0);
    ek_ghost(4, 30);
    ek_policy(180, 6, 35);

    /* X stays up (as it does for the menu and the other games): blanket
     * repainting its splash over the panel is a problem only when X was
     * stopped and restarted, which is exactly what restore-kindle.sh used to
     * do on every chess exit. Leaving X alone gives the same clean hand-off
     * XCOM has. powerd is stopped by tools/kindle-run.sh before every game,
     * so the power key cannot wake the Kindle screen saver over the board. */

    /* One forced full-panel white pass before anything is drawn. E-ink keeps
     * the last image, and the framework's boot screen (splash + progress bar)
     * is still on the glass when the game takes over; the game's partial
     * updates would never touch those pixels. A full-mode GC16 wipes it. */
    ek_clear(EK_PAPER);
    ek_force_full();
    ek_flush();

    int rc = 0;

    /* --wifi-only: the menu's WIFI button. Same screens, no board: join a
     * network (or skip), then hand the panel back. */
    if (wifi_only) {
        if (kframe_wifi_setup() != 0)
            fprintf(stderr, "chess: wifi setup left the unit offline\n");
        ek_close();
        return 0;
    }

    new_game();

    if (menu_shot) {
        kframe_show_menu();
        kframe_paint();
        ek_flush();
        if (ek_dump_pgm(menu_shot, 1) != 0) {
            fprintf(stderr, "chess: could not write %s\n", menu_shot);
            ek_close();
            return 1;
        }
        ek_close();
        return 0;
    }

    /* A picture of a wi-fi screen, for checking the layout. */
    if (wifi_shot) {
        if (kframe_wifi_shot(wifi_shot_what) != 0) {
            ek_close();
            return 2;
        }
        if (ek_dump_pgm(wifi_shot, 1) != 0) {
            fprintf(stderr, "chess: could not write %s\n", wifi_shot);
            ek_close();
            return 1;
        }
        ek_close();
        return 0;
    }

    /* No network means no opponent: get one before the first picture.
     * The host build has no wpa_supplicant, so there it only opens the screen
     * when asked for with --wifi. */
    if (!no_wifi) {
        int need = force_wifi;
#ifndef EK_HOST
        if (!need) need = kframe_wifi_status() != 0;
#endif
        if (need && kframe_wifi_setup() != 0)
            fprintf(stderr, "chess: still offline\n");
    }

    if (do_test) {
        rc = selftest() ? 1 : 0;
        present();
    } else if (api_check) {
        move m;
        char say[256], err[200];
        /* white opens, so the question is a real one */
        if (find_uci(&P, "e2e4", &m) >= 0) {
            note_move(&P, &m);
            undo u;
            make_move(&P, &m, &u);
        }
        int r = api_move(&P, hist_uci, &m, say, sizeof say, err, sizeof err);
        if (r == 0) {
            char u[8];
            uci_str(&m, u);
            printf("move: %s\nsay: %s\n", u, say);
        } else {
            fprintf(stderr, "api check failed: %s\n", err);
            rc = 1;
        }
    } else {
        if (autoplay >= 0 || fake) {
            for (int i = 0; i < (autoplay < 0 ? 6 : autoplay) && !over; i++) {
                if (P.stm == WHITE || fake) {
                    move list[256];
                    int n = legal_moves(&P, list, 256);
                    if (!n) { check_over(); break; }
                    note_move(&P, &list[0]);
                    undo u;
                    make_move(&P, &list[0], &u);
                    if (P.stm == WHITE && !fake) set_status("your turn");
                    check_over();
                } else {
                    if (play_ai() != 0) break;
                }
            }
        }
        present();
    }

#ifdef EK_HOST
    if (!do_test && !api_check && autoplay < 0 && !fake && !isatty(0)) {
        char buf[64];
        while (!stop_req && fgets(buf, sizeof buf, stdin)) {
            int x, y;
            if (buf[0] == 'q') break;
            if (sscanf(buf, "tap %d %d", &x, &y) != 2) {
                fprintf(stderr, "chess: want 'tap X Y' or q\n");
                ek_close();
                return 2;
            }
            int act = handle_tap(x, y);
            if (act == -2) play_ai();
            present();
        }
    }
#else
    /* a batch run still honours --dump: that is how the device gets a picture */
    if (!(do_test || api_check || autoplay >= 0 || fake))
    while (!stop_req) {
        ek_event ev;
        int got = 0;
        {
            static uint32_t hb;
            if (ek_ms() - hb > 5000) {
                hb = ek_ms();
                fprintf(stderr, "chess: heartbeat: board loop\n");
            }
        }
        while (ek_poll(&ev)) {
            if (ev.type == EK_UP) {
                down_seen = 0;
                continue;
            }
            if (ev.type != EK_DOWN) continue;
            uint32_t now = ek_ms();
            if (down_seen && now - (uint32_t)down_ms < 400) continue;
            down_seen = 1;
            down_ms = (int)now;
            int act = handle_tap(ev.x, ev.y);
            got = 1;
            if (act == -2) {
                present();
                play_ai();
                got = 1;
            }
        }
        if (shot_req) { shot_req = 0; ek_dump_pgm(SHOT_PATH, 1); }
        if (stop_req) break;
        if (kframe_pwr_poll()) {
            /* Back from sleep with Black still to move: ask again, because the
             * press cancelled the question that was in flight. */
            if (!over && P.stm == BLACK) play_ai();
            got = 1;
        }
        if (got) present();
        else ek_sleep_ms(20);
    }
#endif

    if (dump_path) {
        if (ek_dump_pgm(dump_path, 1) != 0) {
            fprintf(stderr, "chess: could not write %s\n", dump_path);
            ek_close();
            return 1;
        }
    }

    ek_close();
    return rc;
}
