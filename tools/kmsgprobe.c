/* kmsgprobe: how /dev/kmsg behaves on this Kindle.
 *
 * Written because the chess game's power-button reader (it looks for the
 * "pmic: PWRON1 event" record the kernel prints on every press) got EINVAL on
 * every read after lseek(fd, 0, SEEK_END). This tries the variants in order and
 * prints what each does, so the reader can be written against the real answer
 * instead of a guess.
 *
 *   kmsgprobe [seconds]
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/klog.h>

#ifndef SEEK_DATA
#define SEEK_DATA 3
#endif

static void nap100ms(void) {
    struct timespec ts = { 0, 100000000 };
    nanosleep(&ts, NULL);
}

static void try_read(const char *label, int fd, size_t bufsz) {
    char buf[4096];
    if (bufsz > sizeof buf) bufsz = sizeof buf;
    int n = 0;
    for (;;) {
        ssize_t r = read(fd, buf, bufsz - 1);
        if (r < 0) {
            printf("  %-22s read: %zd errno=%d (%s) after %d record(s)\n",
                   label, r, errno, strerror(errno), n);
            return;
        }
        if (r == 0) {
            printf("  %-22s read: 0 (eof) after %d record(s)\n", label, n);
            return;
        }
        buf[r] = 0;
        n++;
        if (n <= 3) {
            char *nl = strchr(buf, '\n');
            if (nl) *nl = 0;
            printf("  %-22s rec %d: %.120s\n", label, n, buf);
        }
        if (n >= 200) {
            printf("  %-22s stopped after 200 records\n", label);
            return;
        }
    }
}

int main(int argc, char **argv) {
    size_t bufsz = 2048;

    printf("== klogctl: size of the ring buffer\n");
    int sz = klogctl(10, NULL, 0);
    printf("  SIZE_BUFFER -> %d (errno=%d %s)\n", sz, errno, strerror(errno));
    if (sz > 0) {
        char *all = malloc((size_t)sz + 1);
        if (!all) { printf("  out of memory\n"); return 1; }
        int n = klogctl(3, all, sz);
        printf("  READ_ALL -> %d (errno=%d %s)\n", n, errno, strerror(errno));
        if (n > 0) {
            all[n] = 0;
            int hits = 0;
            for (char *p = strstr(all, "PWRON1"); p; p = strstr(p + 6, "PWRON1")) hits++;
            printf("  PWRON1 lines in the buffer: %d\n", hits);
            const char *tail = n > 400 ? all + n - 400 : all;
            printf("  tail:\n%s\n", tail);
        }
        free(all);
    }

    printf("== plain open of /dev/kmsg, read with %zu byte buffer\n", bufsz);
    int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
    if (fd < 0) { printf("  open: %s\n", strerror(errno)); return 1; }
    try_read("no seek", fd, bufsz);
    close(fd);

    printf("== open, lseek SEEK_END, then read\n");
    fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
    if (fd < 0) { printf("  open: %s\n", strerror(errno)); return 1; }
    off_t off = lseek(fd, 0, SEEK_END);
    printf("  lseek END -> %ld (errno=%d %s)\n", (long)off, errno, strerror(errno));
    try_read("after SEEK_END", fd, bufsz);
    close(fd);

    printf("== open, lseek SEEK_DATA, then read\n");
    fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
    if (fd < 0) { printf("  open: %s\n", strerror(errno)); return 1; }
    off = lseek(fd, 0, SEEK_DATA);
    printf("  lseek DATA -> %ld (errno=%d %s)\n", (long)off, errno, strerror(errno));
    try_read("after SEEK_DATA", fd, bufsz);
    close(fd);

    printf("== small buffer (128 bytes), no seek\n");
    fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
    if (fd < 0) { printf("  open: %s\n", strerror(errno)); return 1; }
    try_read("128 byte buffer", fd, 128);
    close(fd);

    int secs = argc > 1 ? atoi(argv[1]) : 0;
    if (secs > 0) {
        printf("== watching for %d s (no seek) -- press power now\n", secs);
        fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
        if (fd < 0) { printf("  open: %s\n", strerror(errno)); return 1; }
        char buf[2048];
        for (int i = 0; i < secs * 10; i++) {
            ssize_t r = read(fd, buf, sizeof buf - 1);
            if (r > 0) {
                buf[r] = 0;
                char *nl = strchr(buf, '\n');
                if (nl) *nl = 0;
                printf("  t=%.1f %s\n", i / 10.0, buf);
            } else if (r < 0 && errno != EAGAIN) {
                printf("  read: %s\n", strerror(errno));
                break;
            }
            nap100ms();
        }
        close(fd);
    }
    printf("== done\n");
    return 0;
}
