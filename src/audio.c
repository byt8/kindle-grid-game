#define _GNU_SOURCE
/* audio.c -- the Kindle's speaker, kept out of the drawing loop.
 *
 * The game never waits for sound. audio_line() and audio_effect() write a
 * one-line request into a pipe and return; one child process owns the PCM
 * device, the mixer route and /usr/bin/aplay, and does the waiting.
 *
 * A request is a text line:
 *     S <log line>     speak it, one baked clip per word
 *     E <effect>       play one of the seven sound effects
 *
 * The child closes aplay after three seconds of quiet so the device is not
 * held open while the player is reading the screen.
 *
 * Clips are read from <dir>/words/<word>.pcm and <dir>/fx/<name>.pcm, mono
 * signed 16-bit at RATE. A missing clip is named on stderr (the first few of
 * them) and counted; it is never dropped in silence.
 */
#include "audio.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define RATE 22050
#define MAX_TOK 128
#define MAXW 24
#define MAX_REPORTS 8

typedef struct {
    char w[MAXW];
    int pause;   /* 0 word, 1 comma, 2 end of sentence */
} tok;

static int   g_mode = AUDIO_OFF;
static char  g_dir[256];
static int   g_wfd = -1;
static pid_t g_pid = -1;
static int   g_miss;      /* clips that were wanted and not found */
static int   g_reported;  /* how many of those were named on stderr */
static int   g_pipe_gone;
static int   g_muted;    /* the mute button; stays set across missions */

static const char *UNITS[10] = {"zero", "one", "two", "three", "four",
                                "five", "six", "seven", "eight", "nine"};
static const char *TEENS[10] = {"ten", "eleven", "twelve", "thirteen", "fourteen",
                                "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
static const char *TENS[8] = {"twenty", "thirty", "forty", "fifty",
                              "sixty", "seventy", "eighty", "ninety"};

/* The effects tools/make_sound.py bakes. A name outside this list is a bug in
 * the game, so it is named on stderr rather than silently played as nothing. */
static const char *FX[] = {"shot", "hit", "miss", "down", "turn", "win", "lose"};

static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_dig(int c) { return c >= '0' && c <= '9'; }
static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

/* ---- text to clips ------------------------------------------------------ */

static int put(tok *out, int n, int max, const char *w) {
    if (n >= max) return -1;
    snprintf(out[n].w, MAXW, "%s", w);
    out[n].pause = 0;
    return n + 1;
}

/* A chance ("65%") and the dice roll in brackets ("(42)") are on the screen
 * only: they are decisions, not sentences, and reading them aloud gets in the
 * way. Everything else that the log says is spoken, including damage numbers,
 * which are spelled out so that "for 4" is two clips and not one. */
static int tokens(const char *text, tok *out, int max) {
    int n = 0, paren = 0;
    const char *p = text;
    while (*p) {
        if (*p == '(') {
            paren++;
            p++;
            continue;
        }
        if (*p == ')') {
            if (paren > 0) paren--;
            p++;
            continue;
        }
        if (is_alpha(*p)) {
            char w[MAXW];
            int k = 0;
            while (is_alpha(*p) || is_dig(*p) || *p == '\'') {
                if (k < MAXW - 1) w[k++] = (char)lower(*p);
                p++;
            }
            w[k] = 0;
            if (paren > 0) continue;   /* inside the roll */
            n = put(out, n, max, w);
            if (n < 0) return -1;
            continue;
        }
        if (is_dig(*p)) {
            long v = 0;
            int over = 0;
            while (is_dig(*p)) {
                if (v <= 999) v = v * 10 + (*p - '0');
                if (++over > 3) v = 1000;
                p++;
            }
            const char *q = p;
            while (*q == ' ') q++;
            if (*q == '%' || paren > 0) continue;   /* a chance, or the roll */
            if (v > 99) {
                fprintf(stderr, "sound: %ld is too big to speak\n", v);
                continue;
            }
            if (v < 10) {
                n = put(out, n, max, UNITS[v]);
                if (n < 0) return -1;
            } else if (v < 20) {
                n = put(out, n, max, TEENS[v - 10]);
                if (n < 0) return -1;
            } else {
                n = put(out, n, max, TENS[v / 10 - 2]);
                if (n < 0) return -1;
                if (v % 10) {
                    n = put(out, n, max, UNITS[v % 10]);
                    if (n < 0) return -1;
                }
            }
            continue;
        }
        if (*p == ',' ) {
            if (n > 0) out[n - 1].pause = 1;
            p++;
            continue;
        }
        if ((*p == '.' || *p == '!' || *p == '?') && (p[1] == 0 || p[1] == ' ')) {
            if (n > 0) out[n - 1].pause = 2;
            p++;
            continue;
        }
        p++;
    }
    if (n > 0) out[n - 1].pause = 2;
    return n;
}

static void clip_path(char *buf, size_t cap, const char *kind, const char *name) {
    snprintf(buf, cap, "%s/%s/%s.pcm", g_dir, kind, name);
}

static void miss(const char *kind, const char *name) {
    g_miss++;
    if (g_reported < MAX_REPORTS) {
        g_reported++;
        fprintf(stderr, "sound: no clip %s/%s.pcm (run 'make sound')\n", kind, name);
        if (g_reported == MAX_REPORTS)
            fprintf(stderr, "sound: further missing clips will be counted, not named\n");
    }
}

/* ---- the child that owns the speaker ------------------------------------ */

static int route_one(const char *ctl, const char *val) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execl("/usr/bin/amixer", "amixer", "sset", ctl, val, (char *)NULL);
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) != pid) return -1;
    return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -1;
}

/* The WM8962 boots with the DAC-to-speaker mixers off, which is why a test
 * tone is silent on a fresh Kindle. These five settings are the whole path. */
static int speaker_route(void) {
    static const char *CTL[5][2] = {
        {"SPKOUTL Mixer DACL", "on"}, {"SPKOUTR Mixer DACR", "on"},
        {"Speaker Mixer", "on"},      {"Speaker", "100"},
        {"Speaker Boost", "6"},
    };
    int bad = 0;
    for (int i = 0; i < 5; i++)
        if (route_one(CTL[i][0], CTL[i][1]) != 0) {
            fprintf(stderr, "sound: could not set the mixer control '%s'\n", CTL[i][0]);
            bad = 1;
        }
    if (bad) fprintf(stderr, "sound: the speaker may stay quiet\n");
    return bad ? -1 : 0;
}

static pid_t spawn_aplay(int *wfd) {
    int p[2];
    if (pipe(p) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    if (pid == 0) {
        if (dup2(p[0], 0) < 0) _exit(127);
        close(p[0]);
        close(p[1]);
        execl("/usr/bin/aplay", "aplay", "-q", "-D", "plughw:0,0", "-t", "raw",
              "-f", "S16_LE", "-c", "1", "-r", "22050", "-", (char *)NULL);
        fprintf(stderr, "sound: cannot run /usr/bin/aplay: %s\n", strerror(errno));
        _exit(127);
    }
    close(p[0]);
    *wfd = p[1];
    return pid;
}

static int play_clip(int fd, const char *kind, const char *name) {
    char path[400];
    clip_path(path, sizeof path, kind, name);
    int in = open(path, O_RDONLY);
    if (in < 0) {
        if (errno == ENOENT) {
            miss(kind, name);
            return 0;
        }
        fprintf(stderr, "sound: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    char buf[4096];
    int bad = 0;
    for (;;) {
        ssize_t got = read(in, buf, sizeof buf);
        if (got < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "sound: cannot read %s: %s\n", path, strerror(errno));
            bad = 1;
            break;
        }
        if (got == 0) break;
        ssize_t off = 0;
        while (off < got) {
            ssize_t w = write(fd, buf + off, (size_t)(got - off));
            if (w < 0) {
                if (errno == EINTR) continue;
                bad = 1;
                break;
            }
            off += w;
        }
        if (bad) {
            fprintf(stderr, "sound: aplay stopped taking audio\n");
            break;
        }
    }
    close(in);
    return bad ? -1 : 0;
}

static int quiet(int fd, int ms) {
    static const char zero[512];
    int bytes = (RATE / 1000) * 2 * ms;
    for (int done = 0; done < bytes; done += (int)sizeof zero) {
        int n = bytes - done;
        if (n > (int)sizeof zero) n = (int)sizeof zero;
        ssize_t w = write(fd, zero, (size_t)n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) return -1;
    }
    return 0;
}

static void speak(int fd, const char *text) {
    static tok t[MAX_TOK];
    int n = tokens(text, t, MAX_TOK);
    if (n < 0) {
        fprintf(stderr, "sound: line is too long to speak: %s\n", text);
        return;
    }
    for (int i = 0; i < n; i++) {
        if (play_clip(fd, "words", t[i].w) != 0) return;
        int ms = t[i].pause == 2 ? 400 : t[i].pause == 1 ? 180 : 60;
        if (quiet(fd, ms) != 0) return;
    }
}

static int fx_known(const char *name) {
    for (size_t i = 0; i < sizeof FX / sizeof FX[0]; i++)
        if (!strcmp(FX[i], name)) return 1;
    return 0;
}

static char g_rbuf[1024];
static int  g_rlen;

static int next_request(int rfd, char *out, int cap) {
    for (;;) {
        for (int i = 0; i < g_rlen; i++)
            if (g_rbuf[i] == '\n') {
                int n = i < cap - 1 ? i : cap - 1;
                memcpy(out, g_rbuf, (size_t)n);
                out[n] = 0;
                memmove(g_rbuf, g_rbuf + i + 1, (size_t)(g_rlen - i - 1));
                g_rlen -= i + 1;
                return 1;
            }
        if (g_rlen == (int)sizeof g_rbuf) {
            fprintf(stderr, "sound: a request line was too long and was dropped\n");
            g_rlen = 0;
        }
        ssize_t got = read(rfd, g_rbuf + g_rlen, sizeof g_rbuf - (size_t)g_rlen);
        if (got < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "sound: cannot read a request: %s\n", strerror(errno));
            return -1;
        }
        if (got == 0) return 0;
        g_rlen += (int)got;
    }
}

static int buffered_line(void) {
    for (int i = 0; i < g_rlen; i++)
        if (g_rbuf[i] == '\n') return 1;
    return 0;
}

static void player(int rfd) {
    /* Nothing from the game reaches aplay: no framebuffer, no touch device. */
    if (rfd != 3) {
        if (dup2(rfd, 3) < 0) _exit(1);
        close(rfd);
        rfd = 3;
    }
    for (int fd = 4; fd < 64; fd++) close(fd);

    speaker_route();

    int aw = -1;
    pid_t apid = -1;
    char req[512];
    for (;;) {
        if (!buffered_line()) {
            struct pollfd pf;
            pf.fd = rfd;
            pf.events = POLLIN;
            pf.revents = 0;
            int r = poll(&pf, 1, aw >= 0 ? 3000 : -1);
            if (r < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (r == 0) {           /* quiet: let go of the PCM device */
                close(aw);
                if (apid > 0) {
                    int st = 0;
                    while (waitpid(apid, &st, 0) < 0 && errno == EINTR) {}
                }
                aw = -1;
                apid = -1;
                continue;
            }
        }
        int got = next_request(rfd, req, sizeof req);
        if (got <= 0) break;        /* the game is gone */

        if (aw < 0) {
            apid = spawn_aplay(&aw);
            if (aw < 0) {
                fprintf(stderr, "sound: no player available; the rest of this game is silent\n");
                return;
            }
        }
        char *arg = req;
        while (*arg && *arg != ' ') arg++;
        while (*arg == ' ') arg++;
        if (req[0] == 'E') {
            if (!fx_known(arg)) {
                fprintf(stderr, "sound: '%s' is not a sound effect\n", arg);
                continue;
            }
            play_clip(aw, "fx", arg);
            quiet(aw, 40);
        } else if (req[0] == 'S') {
            speak(aw, arg);
        } else {
            fprintf(stderr, "sound: unknown request '%s'\n", req);
        }
    }
    if (aw >= 0) close(aw);
    if (apid > 0) {
        int st = 0;
        while (waitpid(apid, &st, 0) < 0 && errno == EINTR) {}
    }
}

/* ---- what the game calls ------------------------------------------------ */

int audio_start(int mode, const char *dir) {
    if (mode == AUDIO_OFF) return 0;
    if (!dir || !dir[0]) {
        fprintf(stderr, "sound: no clip folder was given\n");
        return -1;
    }
    if (strlen(dir) >= sizeof g_dir) {
        fprintf(stderr, "sound: the clip folder path is too long\n");
        return -1;
    }
    snprintf(g_dir, sizeof g_dir, "%s", dir);
    if (mode == AUDIO_CHECK) {
        g_mode = AUDIO_CHECK;
        return 0;
    }

    char probe[400];
    struct stat st;
    snprintf(probe, sizeof probe, "%s/words", g_dir);
    if (stat(probe, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "sound: %s has no words/ folder; running without sound\n", g_dir);
        return -1;
    }

    /* A dead player must not kill the game through SIGPIPE. */
    signal(SIGPIPE, SIG_IGN);

    int p[2];
    if (pipe(p) != 0) {
        fprintf(stderr, "sound: cannot make the request pipe: %s\n", strerror(errno));
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "sound: cannot start the player: %s\n", strerror(errno));
        close(p[0]);
        close(p[1]);
        return -1;
    }
    if (pid == 0) {
        close(p[1]);
        player(p[0]);
        _exit(0);
    }
    close(p[0]);
    int fl = fcntl(p[1], F_GETFL);
    if (fl >= 0) fcntl(p[1], F_SETFL, fl | O_NONBLOCK);
    g_wfd = p[1];
    g_pid = pid;
    g_mode = AUDIO_SPEAK;
    return 0;
}

static void send(const char *rec) {
    if (g_mode != AUDIO_SPEAK || g_wfd < 0) return;
    size_t len = strlen(rec);
    for (;;) {
        ssize_t w = write(g_wfd, rec, len);
        if (w == (ssize_t)len) return;
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            fprintf(stderr, "sound: the player is behind; one request was dropped\n");
            return;
        }
        break;
    }
    if (!g_pipe_gone) {
        g_pipe_gone = 1;
        fprintf(stderr, "sound: the player has stopped; the rest of this game is silent\n");
    }
    close(g_wfd);
    g_wfd = -1;
}

/* Muting has to cut a sentence that is already on its way to the speaker, so
 * two things happen: the flag stops every new request, and the Speaker mixer
 * is turned down to stop the one in flight. The mixer value on the way back
 * is 100, the same speaker_route() sets at start-up. */
void audio_mute(int on) {
    on = !!on;
    if (on == g_muted) return;
    g_muted = on;
    if (g_mode != AUDIO_SPEAK) return;
    if (route_one("Speaker", on ? "0" : "100") != 0)
        fprintf(stderr, "sound: could not %s the speaker\n", on ? "mute" : "unmute");
}

int audio_ismuted(void) { return g_muted; }

void audio_line(const char *text) {
    if (g_muted) return;   /* the log keeps the line; the speaker does not */
    if (g_mode == AUDIO_SPEAK) {
        char rec[512];
        snprintf(rec, sizeof rec, "S %s\n", text);
        send(rec);
        return;
    }
    if (g_mode != AUDIO_CHECK) return;
    static tok t[MAX_TOK];
    int n = tokens(text, t, MAX_TOK);
    if (n < 0) {
        fprintf(stderr, "sound: line is too long to speak: %s\n", text);
        return;
    }
    for (int i = 0; i < n; i++) printf("%s\n", t[i].w);
}

void audio_effect(const char *name) {
    if (!fx_known(name)) {
        fprintf(stderr, "sound: '%s' is not a sound effect\n", name);
        return;
    }
    if (g_muted || g_mode != AUDIO_SPEAK) return;
    char rec[64];
    snprintf(rec, sizeof rec, "E %s\n", name);
    send(rec);
}

int audio_misses(void) { return g_miss; }

int audio_stop(void) {
    if (g_wfd >= 0) {
        close(g_wfd);   /* the child reads end-of-file and leaves */
        g_wfd = -1;
    }
    g_mode = AUDIO_OFF;
    if (g_pid > 0) {
        int st = 0;
        int reaped = 0;
        /* Quitting should not cut the last sentence short, and it should not
         * hang either: give the queued words 2.5 s to drain, then stop it. */
        for (int i = 0; i < 125 && !reaped; i++) {
            pid_t r = waitpid(g_pid, &st, WNOHANG);
            if (r == g_pid) {
                reaped = 1;
                break;
            }
            if (r < 0 && errno != EINTR) break;
            usleep(20000);
        }
        if (!reaped) {
            kill(g_pid, SIGTERM);
            while (waitpid(g_pid, &st, 0) < 0 && errno == EINTR) {}
            fprintf(stderr, "sound: stopped the player\n");
        }
        g_pid = -1;
    }
    if (g_miss)
        fprintf(stderr, "sound: %d clips were missing from %s\n", g_miss, g_dir);
    return 0;
}
