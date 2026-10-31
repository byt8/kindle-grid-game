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
 * bar on top, the model's remarks underneath, three buttons at the bottom.
 */
#include "eink.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
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

/* ---- the power button --------------------------------------------------
 * The power button is not a touch event and it is not on event2 or event3: it
 * is a one-line status file, /proc/whitney_button, that reads "whitney button
 * is locked" while the key is held and "... is unlocked" the rest of the time.
 * Turning that into a sleep is powerd's job. powerd is running here and does
 * react to the press -- probed on the device, its state goes Active -> Screen
 * Saver for a few seconds on every press -- but it never suspends: after
 * Screen Saver it waits for pillowd to paint the screen saver, and pillowd is
 * part of the reader UI the game deliberately stops. So the game does the rest
 * itself (see sleep_now() below).
 *
 * pwr_latch is set when a press has been seen and not yet acted on; the event
 * loop is the only place that acts on it.
 */
#define PWR_BUTTON "/proc/whitney_button"
#define PWR_STATE  "/sys/power/state"

static int pwr_latch;

/* 1 when the status line says the key is held. The two states are "whitney
 * button is locked" and "whitney button is unlocked" -- note that the second
 * contains the first, so match the phrase, not the word. --selftest checks
 * both strings. */
static int pwr_says_down(const char *s) { return strstr(s, "is locked") != NULL; }

/* 1 while the key is held, 0 while it is not, -1 if the file cannot be read. */
static int pwr_down(void) {
    char b[80];
    int fd = open(PWR_BUTTON, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, b, sizeof b - 1);
    close(fd);
    if (n <= 0) return -1;
    b[n] = 0;
    return pwr_says_down(b);
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
     * a three-minute wait must not be swallowed, and a reply is worthless if
     * the machine is going to sleep. Watching the file rather than sitting in
     * read() is what makes that possible. */
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
            if (pwr_down() == 1) { cancelled = 1; break; }
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
        pwr_latch = 1;
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
#define BTN_Y     744
#define BTN_H     42

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
static char g_wifi_ssid[64];      /* "" when wlan0 is not joined */
static char g_wifi_ip[32];
static char g_wifi_err[240];      /* the last wi-fi failure, shown on screen */
static int  over;                 /* 0 playing, otherwise game_state() */
static int  promo_from = -1, promo_to = -1;
static int  api_busy;
static int  stop_req;
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
    if (pwr_latch) { api_busy = 0; return -2; }    /* the loop sleeps, then asks again */
    if (rc != 0) {
        char first[200];
        snprintf(first, sizeof first, "%s", err);
        char hist2[2400];
        snprintf(hist2, sizeof hist2, "%.2000s (your last answer was refused: %.180s. "
                 "Answer only with a legal move from the list.)", hist_uci, first);
        rc = api_move(&P, hist2, &m, say, sizeof say, err, sizeof err);
        if (pwr_latch) { api_busy = 0; return -2; }
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

/* ---- buttons ---------------------------------------------------------- */

static const struct { const char *label; int x, w; } BTNS[3] = {
    { "NEW",   20, 175 },
    { "RETRY", 212, 175 },
    { "QUIT",  404, 175 },
};

static void button(int i) {
    int x = BTNS[i].x;
    ek_fill_rect(x, BTN_Y, BTNS[i].w, BTN_H, EK_PAPER);
    ek_rect(x, BTN_Y, BTNS[i].w, BTN_H, EK_INK);
    ek_rect(x + 1, BTN_Y + 1, BTNS[i].w - 2, BTN_H - 2, EK_INK);
    int tw = (int)strlen(BTNS[i].label) * 8;
    ek_text(x + (BTNS[i].w - tw) / 2, BTN_Y + (BTN_H - 16) / 2, BTNS[i].label, EK_INK);
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
    ek_clear(EK_PAPER);

    ek_text(12, 8, "CHESS", EK_INK);
    ek_text(64, 8, "- against", EK_INK);
    ek_text(136, 8, CFG.model, EK_INK);
    ek_text(12, 28, status, EK_INK);
    /* Wi-fi lives in the corner: tapping it brings up the picker again. */
    ek_rect(496, 2, 96, 22, EK_INK);
    ek_text(508, 5, g_wifi_ssid[0] ? "WI-FI *" : "NO WI-FI", EK_INK);
    if (over) {
        ek_text(12, 46, "tap NEW for another game", EK_INK);
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
    ek_text(12, BTN_Y - 24, "tap the squares to move", EK_INK);
    for (int i = 0; i < 3; i++) button(i);
    ek_flush();
}

/* ---- wi-fi setup ------------------------------------------------------
 * A gifted unit has no way into the stock Wi-Fi dialog: the game owns the
 * panel and the reader UI is deliberately not running. So the game brings its
 * own: scan, tap a network, type the password on a drawn keyboard. The joining
 * itself is done by /mnt/us/chess-wifi.sh through wpa_cli (see
 * tools/chess-wifi.sh), which also remembers the network -- so the next boot
 * joins by itself and this screen never appears again.
 */

#ifndef WIFI_SCRIPT
#define WIFI_SCRIPT "/mnt/us/chess-wifi.sh"
#endif

#define W_MAX    12          /* networks listed on one screen */
#define W_KB_X   22          /* first key column */
#define W_KEY    56          /* key pitch, and the key's own size */
#define W_KB_Y   150         /* first key row */
#define W_ROW_H  50          /* height of a network row in the list */

typedef struct { char ssid[64]; int sig; int secure; } wnet;
static wnet g_nets[W_MAX];
static int  g_nnets;

/* Run a shell command, capture stdout+stderr, return its exit status. */
static int run_sh(const char *cmd, char *out, size_t n) {
    int op[2];
    if (pipe(op) != 0) { snprintf(out, n, "could not make a pipe"); return -1; }
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(out, n, "could not start a shell");
        close(op[0]); close(op[1]);
        return -1;
    }
    if (pid == 0) {
        close(op[0]);
        dup2(op[1], 1);
        dup2(op[1], 2);
        close(op[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(op[1]);
    size_t got = 0;
    ssize_t r;
    while ((r = read(op[0], out + got, n - 1 - got)) > 0) {
        got += (size_t)r;
        if (got >= n - 1) break;
    }
    out[got] = 0;
    close(op[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st)) return -1;
    return WEXITSTATUS(st);
}

/* Wrap a string for /bin/sh: single quotes, with ' as '\''. */
static void shq(char *out, size_t n, const char *in) {
    size_t o = 0;
    if (o + 1 < n) out[o++] = '\'';
    for (const char *p = in; *p && o + 5 < n; p++) {
        if (*p == '\'') {
            out[o++] = '\''; out[o++] = '\\'; out[o++] = '\''; out[o++] = '\'';
        } else {
            out[o++] = *p;
        }
    }
    if (o + 1 < n) out[o++] = '\'';
    out[o] = 0;
}

/* 0 when wlan0 is joined and has an address. Fills g_wifi_ssid / g_wifi_ip. */
static int wifi_status(void) {
    char buf[512];
    g_wifi_ssid[0] = 0;
    g_wifi_ip[0] = 0;
    int rc = run_sh("sh " WIFI_SCRIPT " status", buf, sizeof buf);
    for (char *line = buf; line && *line; ) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (!strncmp(line, "ssid=", 5)) snprintf(g_wifi_ssid, sizeof g_wifi_ssid, "%s", line + 5);
        if (!strncmp(line, "ip=", 3))   snprintf(g_wifi_ip, sizeof g_wifi_ip, "%s", line + 3);
        line = nl ? nl + 1 : NULL;
    }
    int ok = (rc == 0);
    if (!ok) g_wifi_ssid[0] = 0;
    return ok ? 0 : -1;
}

/* Fill g_nets, strongest first. -1 when the scan failed (g_wifi_err says why). */
static int wifi_scan(void) {
    char buf[4096];
    g_nnets = 0;
    int rc = run_sh("sh " WIFI_SCRIPT " scan", buf, sizeof buf);
    if (rc != 0) {
        snprintf(g_wifi_err, sizeof g_wifi_err, "%.200s",
                 buf[0] ? buf : "the scan failed (is wpa_supplicant running?)");
        return -1;
    }
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        char *sig = tab + 1;
        char *tab2 = strchr(sig, '\t');
        if (tab2) *tab2 = 0;
        if (g_nnets >= W_MAX) break;
        snprintf(g_nets[g_nnets].ssid, sizeof g_nets[0].ssid, "%s", line);
        g_nets[g_nnets].sig = atoi(sig);
        g_nets[g_nnets].secure = tab2 ? strcmp(tab2 + 1, "open") != 0 : 1;
        g_nnets++;
    }
    if (!g_nnets) {
        snprintf(g_wifi_err, sizeof g_wifi_err, "no networks found - tap RESCAN");
        return -1;
    }
    return 0;
}

/* 0 when the link is up and has an address. On failure g_wifi_err says why. */
static int wifi_join(const char *ssid, const char *psk) {
    char qs[160], qp[160], cmd[420], buf[1024];
    shq(qs, sizeof qs, ssid);
    shq(qp, sizeof qp, psk);
    snprintf(cmd, sizeof cmd, "sh " WIFI_SCRIPT " join %s %s", qs, qp);
    fprintf(stderr, "chess: joining %s\n", ssid);
    g_wifi_err[0] = 0;
    int rc = run_sh(cmd, buf, sizeof buf);
    if (rc != 0) {
        /* the script's last "error: ..." line is the useful one */
        char *last = strstr(buf, "error: ");
        snprintf(g_wifi_err, sizeof g_wifi_err, "%.200s",
                 last ? last + 7 : (buf[0] ? buf : "the join failed"));
        char *nl = strchr(g_wifi_err, '\n');
        if (nl) *nl = 0;
        fprintf(stderr, "chess: wi-fi: %s\n", g_wifi_err);
        return -1;
    }
    return wifi_status();
}

/* One tap, ignoring the DOWN repeats the panel sends while a finger rests. */
static int wait_tap(int *x, int *y) {
    static int seen, when;
#ifdef EK_HOST
    /* The host build has no touch panel: it is driven by 'tap X Y' lines on
     * stdin instead, the same script the board tests use. */
    if (!isatty(0)) {
        char buf[64];
        if (fgets(buf, sizeof buf, stdin) && sscanf(buf, "tap %d %d", x, y) == 2) return 1;
        return 0;
    }
#endif
    while (!stop_req) {
        if (shot_req) { shot_req = 0; ek_dump_pgm(SHOT_PATH, 1); }
        ek_event ev;
        {
            static uint32_t hb;
            if (ek_ms() - hb > 5000) {
                hb = ek_ms();
                fprintf(stderr, "chess: heartbeat: waiting for a tap elsewhere");
                fprintf(stderr, " (button %d)\n", pwr_down());
            }
        }
        while (ek_poll(&ev)) {
            if (ev.type == EK_UP) { seen = 0; continue; }
            if (ev.type != EK_DOWN) continue;
            uint32_t now = ek_ms();
            if (seen && now - (uint32_t)when < 400) continue;
            seen = 1;
            when = (int)now;
            *x = ev.x;
            *y = ev.y;
            return 1;
        }
        ek_sleep_ms(20);
    }
    return 0;
}

static void wbox(int x, int y, int w, int h, const char *label, int scale) {
    int tw = (int)strlen(label) * 8 * scale;
    ek_fill_rect(x, y, w, h, EK_PAPER);
    ek_rect(x, y, w, h, EK_INK);
    ek_rect(x + 1, y + 1, w - 2, h - 2, EK_INK);
    if (scale > 1) ek_text_scale(x + (w - tw) / 2, y + (h - 16 * scale) / 2, label, EK_INK, scale);
    else           ek_text(x + (w - tw) / 2, y + (h - 16) / 2, label, EK_INK);
}

static const char *KB[4] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };

/* The typed-so-far line, with the cursor, in a framed box. */
static void w_entry(const char *value, int y) {
    ek_rect(20, y, 560, 40, EK_INK);
    ek_rect(21, y + 1, 558, 38, EK_INK);
    ek_text(30, y + 12, value, EK_INK);
    int n = (int)strlen(value);
    ek_text(30 + n * 8, y + 12, "_", EK_INK);
}

static void w_keyboard(const char *bottom_right) {
    for (int r = 0; r < 4; r++) {
        int len = (int)strlen(KB[r]);
        for (int i = 0; i < len; i++) {
            int x = W_KB_X + i * W_KEY;
            int y = W_KB_Y + r * 60;
            char label[2] = { KB[r][i], 0 };
            ek_fill_rect(x, y, W_KEY - 4, 54, EK_PAPER);
            ek_rect(x, y, W_KEY - 4, 54, EK_INK);
            ek_text(x + (W_KEY - 4 - 8) / 2, y + 19, label, EK_INK);
        }
    }
    wbox(20,  W_KB_Y + 4 * 60, 130, 54, "SHIFT", 1);
    wbox(160, W_KB_Y + 4 * 60, 120, 54, "DEL", 1);
    wbox(290, W_KB_Y + 4 * 60, 140, 54, "CLEAR", 1);
    wbox(20,  W_KB_Y + 4 * 60 + 64, 280, 54, "BACK", 1);
    wbox(310, W_KB_Y + 4 * 60 + 64, 270, 54, bottom_right, 1);
}

/* Which keyboard cell, if any, was tapped. Returns 0 for none. */
static int w_key_at(int x, int y, char *out) {
    for (int r = 0; r < 4; r++) {
        int len = (int)strlen(KB[r]);
        for (int i = 0; i < len; i++) {
            int kx = W_KB_X + i * W_KEY, ky = W_KB_Y + r * 60;
            if (x >= kx && x < kx + W_KEY - 4 && y >= ky && y < ky + 54) {
                *out = KB[r][i];
                return 1;
            }
        }
    }
    return 0;
}

#define W_BOT (W_KB_Y + 4 * 60)
#define W_BOT2 (W_KB_Y + 4 * 60 + 64)

/* Draw the list of networks. Returns nothing; the tap loop decides. */
static void w_draw_list(void) {
    ek_clear(EK_PAPER);
    ek_text_scale(12, 6, "WI-FI", EK_INK, 2);
    if (g_wifi_ssid[0]) {
        char line[128];
        snprintf(line, sizeof line, "on %s   %s", g_wifi_ssid, g_wifi_ip);
        ek_text(140, 16, line, EK_INK);
    } else {
        ek_text(140, 16, "not connected - tap a network", EK_INK);
    }
    if (g_wifi_err[0]) ek_text(12, 52, g_wifi_err, EK_INK);
    else if (!g_nnets) ek_text(12, 52, "looking for networks...", EK_INK);

    for (int i = 0; i < g_nnets; i++) {
        int y = 84 + i * W_ROW_H;
        if (y + W_ROW_H > 600) break;
        ek_rect(20, y, 560, W_ROW_H - 6, EK_INK);
        char name[40];
        snprintf(name, sizeof name, "%.30s", g_nets[i].ssid);
        ek_text(30, y + (W_ROW_H - 6 - 16) / 2, name, EK_INK);
        ek_text(470, y + (W_ROW_H - 6 - 16) / 2, g_nets[i].secure ? "WPA2" : "open", EK_INK);
    }
    wbox(20,  660, 170, 50, "RESCAN", 1);
    wbox(210, 660, 190, 50, "TYPE NAME", 1);
    wbox(420, 660, 160, 50, "SKIP", 1);
    ek_flush();
}

static void w_draw_text(const char *title, const char *value, const char *bottom_right) {
    ek_clear(EK_PAPER);
    char head[120];
    snprintf(head, sizeof head, "%.60s", title);
    ek_text(12, 12, head, EK_INK);
    ek_text(12, 34, "tap the letters, then the button at the bottom", EK_INK);
    w_entry(value, 70);
    w_keyboard(bottom_right);
    ek_flush();
}

static void w_draw_msg(const char *title, const char *body) {
    ek_clear(EK_PAPER);
    ek_text(12, 12, title, EK_INK);
    wrap(body, 12, 44, 72, 8);
    ek_flush();
}

static void w_draw_fail(void) {
    w_draw_msg("that did not work", g_wifi_err);
    wbox(20, 660, 250, 50, "TRY AGAIN", 1);
    wbox(300, 660, 280, 50, "OTHER NETWORKS", 1);
    ek_flush();
}

/* The whole setup, modal. 0 when the unit ends up online. */
static int wifi_setup(void) {
    enum { M_LIST, M_SSID, M_PASS, M_DONE };
    int mode = M_LIST;
    char ssid[64] = "", pw[64] = "";
    int shift = 0;
    int secure = 1;

    g_wifi_err[0] = 0;
    wifi_scan();

    for (;;) {
        if (stop_req) return -1;

        if (mode == M_LIST) {
            w_draw_list();
            int x, y;
            if (!wait_tap(&x, &y)) return -1;
            if (y >= 660 && y < 710) {
                if (x >= 20 && x < 190) { g_wifi_err[0] = 0; wifi_scan(); continue; }
                if (x >= 210 && x < 400) { mode = M_SSID; ssid[0] = 0; pw[0] = 0; secure = 1; continue; }
                if (x >= 420 && x < 580) return -1;        /* SKIP: play anyway */
            }
            for (int i = 0; i < g_nnets; i++) {
                int ry = 84 + i * W_ROW_H;
                if (y >= ry && y < ry + W_ROW_H - 6 && x >= 20 && x < 580) {
                    snprintf(ssid, sizeof ssid, "%s", g_nets[i].ssid);
                    secure = g_nets[i].secure;
                    pw[0] = 0;
                    if (secure) {
                        mode = M_PASS;
                    } else {
                        /* nothing to type on an open network: join now */
                        w_draw_msg("joining", ssid);
                        if (wifi_join(ssid, "") == 0) return 0;
                        mode = M_DONE;
                    }
                    break;
                }
            }
            continue;
        }

        if (mode == M_SSID || mode == M_PASS) {
            char *buf = (mode == M_SSID) ? ssid : pw;
            size_t cap = (mode == M_SSID) ? sizeof ssid : sizeof pw;
            char title[120];
            if (mode == M_SSID) snprintf(title, sizeof title, "NETWORK NAME");
            else                snprintf(title, sizeof title, "PASSWORD FOR %.50s", ssid);
            w_draw_text(title, buf, mode == M_SSID ? "NEXT" : "JOIN");

            int x, y;
            if (!wait_tap(&x, &y)) return -1;
            size_t len = strlen(buf);

            if (x >= 20 && x < 150 && y >= W_BOT && y < W_BOT + 54) { shift = !shift; continue; }
            if (x >= 160 && x < 280 && y >= W_BOT && y < W_BOT + 54) {
                if (len) buf[len - 1] = 0;
                continue;
            }
            if (x >= 290 && x < 430 && y >= W_BOT && y < W_BOT + 54) { buf[0] = 0; continue; }
            if (y >= W_BOT2 && y < W_BOT2 + 54) {
                if (x >= 20 && x < 300) { mode = M_LIST; continue; }
                if (x >= 310 && x < 580) {
                    if (mode == M_SSID) {
                        if (!ssid[0]) { snprintf(g_wifi_err, sizeof g_wifi_err, "type the network name first"); continue; }
                        pw[0] = 0;
                        mode = M_PASS;
                        continue;
                    }
                    w_draw_msg("joining", ssid);
                    if (wifi_join(ssid, pw) == 0) return 0;
                    mode = M_DONE;
                    continue;
                }
            }
            char ch;
            if (w_key_at(x, y, &ch)) {
                if (len + 1 < cap) {
                    buf[len] = shift ? (char)toupper((unsigned char)ch) : ch;
                    buf[len + 1] = 0;
                }
            }
            continue;
        }

        if (mode == M_DONE) {
            w_draw_fail();
            int x, y;
            if (!wait_tap(&x, &y)) return -1;
            if (y >= 660 && y < 710) {
                if (x >= 20 && x < 270) { mode = M_PASS; continue; }
                if (x >= 300 && x < 580) { mode = M_LIST; g_wifi_err[0] = 0; wifi_scan(); continue; }
            }
            continue;
        }
    }
}

/* ---- sleeping ---------------------------------------------------------
 * What powerd would have done, done here: paint a sleep screen, ask the kernel
 * to suspend, and put the board back when the machine wakes. Writing "mem" to
 * /sys/power/state is the whole suspend; the write returns when a wake source
 * -- the power button, among others -- has fired.
 */

#ifndef EK_HOST
/* Wait for the key to come up, for at most max_ms. 1 if it came up. */
static int pwr_wait_up(int max_ms) {
    int waited = 0;
    while (pwr_down() == 1) {
        if (waited >= max_ms) return 0;
        ek_sleep_ms(20);
        waited += 20;
    }
    return 1;
}

/* Seconds since boot, from /proc/uptime; -1 if it cannot be read. */
static double up_secs(void) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return -1;
    double v = -1;
    if (fscanf(f, "%lf", &v) != 1) v = -1;
    fclose(f);
    return v;
}

static int pwr_suspend(char *err, size_t n) {
    int fd = open(PWR_STATE, O_WRONLY);
    if (fd < 0) {
        snprintf(err, n, "could not open %s: %s", PWR_STATE, strerror(errno));
        return -1;
    }
    errno = 0;
    ssize_t w = write(fd, "mem", 3);
    int e = errno;
    close(fd);
    if (w != 3) {
        snprintf(err, n, "could not suspend: %s",
                 e ? strerror(e) : "the write was short");
        return -1;
    }
    return 0;
}

static void draw_sleep(void) {
    ek_clear(EK_PAPER);
    ek_text_scale(196, 300, "ASLEEP", EK_INK, 4);
    ek_text(132, 420, "press the power button to wake it up", EK_INK);
    ek_text(156, 452, "the game carries on where it was", EK_INK);
}

/* Sleep until woken, then redraw. The caller redraws the board afterwards. */
static void sleep_now(void) {
    char err[200] = "";

    if (!pwr_wait_up(3000))
        fprintf(stderr, "chess: power still held after 3 s; sleeping anyway\n");

    draw_sleep();
    ek_force_full();
    ek_flush();

    double t0 = up_secs();
    if (pwr_suspend(err, sizeof err) != 0) {
        fprintf(stderr, "chess: %s\n", err);
        set_status("could not sleep - %s", err);
        ek_force_full();
        present();
        return;
    }
    double t1 = up_secs();
    if (t0 >= 0 && t1 >= 0 && t1 > t0)
        fprintf(stderr, "chess: asleep for %.1f s\n", t1 - t0);

    /* The press that woke the machine is still down for a moment; read it as
     * a wake-up, not as a new press, or the game goes straight back to sleep. */
    if (!pwr_wait_up(2000))
        fprintf(stderr, "chess: power still held after waking\n");

    /* The panel still holds the sleep screen: one full pass wipes it. */
    ek_force_full();
    present();

    /* The radio is down while the machine is asleep and the association does
     * not always come back by itself. Rejoin before the next question goes to
     * the model, rather than losing a move to a dead network. */
    if (wifi_status() != 0) {
        char buf[256] = "";
        set_status("back - rejoining wi-fi");
        present();
        if (run_sh("sh " WIFI_SCRIPT " auto", buf, sizeof buf) != 0)
            fprintf(stderr, "chess: wi-fi did not come back: %.200s\n", buf);
        else
            fprintf(stderr, "chess: wi-fi rejoined\n");
    }
}

/* Called from the event loop. 1 when a press has just been handled. */
static int pwr_poll(void) {
    /* One line per change of state, so the log shows whether this is being
     * polled at all and what the button file says. */
    static int last = -2;
    int now = pwr_down();
    if (now != last) {
        fprintf(stderr, "chess: power button %s\n",
                now == 1 ? "down" : now == 0 ? "up" : "unreadable");
        last = now;
    }
    if (!pwr_latch && now == 1) pwr_latch = 1;
    if (!pwr_latch) return 0;
    pwr_latch = 0;
    sleep_now();
    return 1;
}
#endif  /* !EK_HOST */

/* ---- taps ------------------------------------------------------------- */

static int square_at(int x, int y, int *out) {
    if (x < BOARD_X || x >= BOARD_X + 8 * SQ_PX) return 0;
    if (y < BOARD_Y || y >= BOARD_END) return 0;
    int f = (x - BOARD_X) / SQ_PX;
    int r = 7 - (y - BOARD_Y) / SQ_PX;
    *out = SQ(f, r);
    return 1;
}

static int in_box(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
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

/* Returns 1 when the screen should be drawn again. */
static int handle_tap(int x, int y) {
    if (in_box(x, y, 496, 2, 96, 22)) {
        wifi_setup();
        return 1;
    }
    if (over) {
        if (in_box(x, y, BTNS[0].x, BTN_Y, BTNS[0].w, BTN_H)) { new_game(); return 1; }
        if (in_box(x, y, BTNS[2].x, BTN_Y, BTNS[2].w, BTN_H)) { stop_req = 1; return 1; }
        return 0;
    }

    if (in_box(x, y, BTNS[2].x, BTN_Y, BTNS[2].w, BTN_H)) { stop_req = 1; return 1; }
    if (in_box(x, y, BTNS[0].x, BTN_Y, BTNS[0].w, BTN_H)) { new_game(); return 1; }
    if (in_box(x, y, BTNS[1].x, BTN_Y, BTNS[1].w, BTN_H)) {
        if (P.stm == BLACK && !api_busy) return -2;   /* ask again */
        set_status("the computer does not owe you a move");
        return 1;
    }

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

    /* the power button's status line: "unlocked" contains "locked" */
    want(pwr_says_down("whitney button is locked\n"), 1, "held key reads as down");
    want(pwr_says_down("whitney button is unlocked\n"), 0, "released key reads as up");

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
            "  --no-wifi     do not check wi-fi on start\n"
            "  --wifi-shot F write a wi-fi screen (PGM) and exit: F then\n"
            "                list|name|pass|fail, default list\n"
            "\n"
            "The power button puts the Kindle to sleep and wakes it again; the\n"
            "position is kept. A press during the model's turn cancels that\n"
            "question and asks it again after the wake.\n",
            conf_default());
}

int main(int argc, char **argv) {
    unsigned flags = EK_TAKEOVER;
    int do_test = 0, api_check = 0, fake = 0, autoplay = -1;
    int no_wifi = 0, force_wifi = 0;
    const char *dump_path = NULL, *wifi_shot = NULL;
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
        else if (!strcmp(argv[i], "--no-wifi")) no_wifi = 1;
        else if (!strcmp(argv[i], "--wifi-shot") && i + 1 < argc) {
            wifi_shot = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') wifi_shot_what = argv[++i];
        }
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

    /* The `x` job (Xorg, awesome and blanket) still owns the panel on this
     * firmware. blanket paints the start-up picture AND the boot progress bar,
     * and it does not stop when `framework` is stopped: it keeps repainting its
     * splash window over whoever holds the screen, so a bar frozen halfway was
     * left on the glass on top of the board (the "black blob"). Killing blanket
     * on its own is worse -- lxinit exits abnormally, x_monitor restarts the
     * whole GUI (five times, then it reboots the device into repair mode). So
     * stop the job properly: the stop is clean, so x_monitor leaves it down,
     * and restore-kindle.sh starts it again when the game exits. */
    if (flags & EK_TAKEOVER) {
        if (system("/sbin/initctl stop x") != 0)
            fprintf(stderr, "chess: could not stop the x job (Xorg/blanket);"
                            " the boot splash may draw over the game\n");
    }

    /* One forced full-panel white pass before anything is drawn. E-ink keeps
     * the last image, and the framework's boot screen (splash + progress bar)
     * is still on the glass when the game takes over; the game's partial
     * updates would never touch those pixels. A full-mode GC16 wipes it. */
    ek_clear(EK_PAPER);
    ek_force_full();
    ek_flush();

    int rc = 0;
    new_game();

    /* A picture of a wi-fi screen, for checking the layout. */
    if (wifi_shot) {
        if (!strcmp(wifi_shot_what, "list")) {
            wifi_scan();
            w_draw_list();
        } else if (!strcmp(wifi_shot_what, "name")) {
            w_draw_text("NETWORK NAME", "garden", "NEXT");
        } else if (!strcmp(wifi_shot_what, "pass")) {
            w_draw_text("PASSWORD FOR PRNet84", "Alotbsol1", "JOIN");
        } else if (!strcmp(wifi_shot_what, "fail")) {
            snprintf(g_wifi_err, sizeof g_wifi_err,
                     "could not join PRNet84 (out of range, or the password was refused)");
            w_draw_fail();
        } else {
            fprintf(stderr, "chess: --wifi-shot wants list, name, pass or fail\n");
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
        if (!need) need = wifi_status() != 0;
#endif
        if (need && wifi_setup() != 0)
            fprintf(stderr, "chess: still offline (%s)\n",
                    g_wifi_err[0] ? g_wifi_err : "nothing chosen");
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
                fprintf(stderr, "chess: heartbeat: board loop, button %d\n",
                        pwr_down());
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
        if (pwr_poll()) {
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
