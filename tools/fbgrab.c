/* fbgrab.c -- capture the Kindle framebuffer at a fixed cadence.
 *
 * Usage: fbgrab <outdir> <ms-between> <seconds>
 * Writes f_%04d.raw (608x800 8bpp, one frame per sample) into outdir.
 * Static-linked for the kindle; uses nanosleep so sub-second intervals work.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define W 608
#define H 800
#define FB "/dev/fb0"

static void nsleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0) { /* EINTR: keep sleeping */ }
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: fbgrab <outdir> <ms> <secs>\n");
        return 2;
    }
    const char *dir = argv[1];
    long ms = atol(argv[2]);
    long secs = atol(argv[3]);
    mkdir(dir, 0755);

    int fb = open(FB, O_RDONLY);
    if (fb < 0) { perror("open fb0"); return 1; }

    unsigned char *buf = malloc((size_t)W * H);
    if (!buf) return 1;

    long n_frames = (secs * 1000L) / ms;
    for (long i = 0; i < n_frames; i++) {
        if (lseek(fb, 0, SEEK_SET) < 0) { perror("lseek"); break; }
        ssize_t got = read(fb, buf, (size_t)W * H);
        if (got != W * H) {
            fprintf(stderr, "short read %zd at frame %ld\n", got, i);
            break;
        }
        char path[256];
        snprintf(path, sizeof path, "%s/f_%04ld.raw", dir, i);
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror("open out"); return 1; }
        ssize_t w = write(fd, buf, (size_t)W * H);
        if (w != W * H) { perror("write"); }
        close(fd);
        if (i + 1 < n_frames) nsleep_ms(ms);
    }
    close(fb);
    printf("grabbed %ld frames -> %s\n", n_frames, dir);
    return 0;
}
