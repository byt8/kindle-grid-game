/* keyprobe -- find which input device carries the power key, and whether it is
 * held. For each /dev/input/eventN it prints the device name and its key
 * capabilities once, then for SECONDS seconds prints a line whenever a key is
 * down according to EVIOCGKEY (the ioctl that reports *current* state, so a
 * short press cannot be missed by a slow reader) or whenever an event arrives.
 *
 *   keyprobe 30            # watch every event device for 30 seconds
 *   keyprobe 30 /dev/input/event2
 *
 * Printed codes are raw linux/input.h numbers (KEY_POWER is 116, EV_KEY is 1).
 */
#define _POSIX_C_SOURCE 200809L   /* nanosleep */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define EV_KEY_CODE 1
#define MAX_DEV 8

struct key_event {
    int32_t  sec, usec;
    uint16_t type, code;
    int32_t  value;
};

static double now_s(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

/* #define EVIOCGNAME / EVIOCGKEY live in <linux/input.h>, which this build avoids
 * on purpose: its BTN_* names collide with the games' own. The two numbers are
 * stable ABI. */
#define IOC_READ     2
#define IOC_NRBITS   8
#define IOC_TYPEBITS 8
#define IOC_SIZEBITS 14
#define IOC_DIRSHIFT 30
#define IOC_SIZESHIFT 16
#define IOC_TYPESHIFT 8
#define IOC_NRSHIFT  0
#define MY_IOC(dir, type, nr, size) \
    (((dir) << IOC_DIRSHIFT) | ((type) << IOC_TYPESHIFT) | \
     ((nr) << IOC_NRSHIFT) | ((size) << IOC_SIZESHIFT))
#define MY_EVIOCGNAME(len) MY_IOC(IOC_READ, 'E', 0x06, len)
#define MY_EVIOCGKEY(len)  MY_IOC(IOC_READ, 'E', 0x18, len)

int main(int argc, char **argv) {
    int secs = argc > 1 ? atoi(argv[1]) : 20;
    if (secs <= 0) secs = 20;

    const char *devs[MAX_DEV];
    int ndev = 0;
    if (argc > 2) {
        for (int i = 2; i < argc && ndev < MAX_DEV; i++) devs[ndev++] = argv[i];
    } else {
        static char names[MAX_DEV][32];
        for (int i = 0; i < 5; i++) {
            snprintf(names[i], sizeof names[i], "/dev/input/event%d", i);
            devs[ndev++] = names[i];
        }
    }

    int fd[MAX_DEV];
    unsigned long down[MAX_DEV];
    for (int i = 0; i < ndev; i++) {
        fd[i] = open(devs[i], O_RDONLY | O_NONBLOCK);
        down[i] = 0;
        if (fd[i] < 0) {
            printf("%-22s -- cannot open: %s\n", devs[i], strerror(errno));
            continue;
        }
        char nm[80] = "?";
        if (ioctl(fd[i], MY_EVIOCGNAME(sizeof nm - 1), nm) < 0) strcpy(nm, "?");
        nm[sizeof nm - 1] = 0;
        printf("%-22s %s\n", devs[i], nm);
    }
    printf("-- watching for %d s; press the key now (a held key prints every\n"
           "   tenth of a second it stays down)\n", secs);
    fflush(stdout);

    double t0 = now_s();
    while (now_s() - t0 < secs) {
        for (int i = 0; i < ndev; i++) {
            if (fd[i] < 0) continue;
            /* events first: they carry the code and the direction */
            struct key_event ev;
            while (read(fd[i], &ev, sizeof ev) == (ssize_t)sizeof ev) {
                if (ev.type != EV_KEY_CODE) continue;
                printf("%6.2f %-20s event: code %u value %d\n",
                       now_s() - t0, devs[i], ev.code, ev.value);
                fflush(stdout);
            }
            /* then the current-state bitmask, so a press between reads shows */
            unsigned char bits[64];
            memset(bits, 0, sizeof bits);
            if (ioctl(fd[i], MY_EVIOCGKEY(sizeof bits), bits) < 0) continue;
            for (unsigned j = 0; j < sizeof bits; j++) {
                if (!bits[j]) continue;
                for (int b = 0; b < 8; b++) {
                    if (!(bits[j] >> b & 1)) continue;
                    unsigned code = j * 8 + (unsigned)b;
                    printf("%6.2f %-20s held: code %u\n",
                           now_s() - t0, devs[i], code);
                    fflush(stdout);
                }
            }
            (void)down;
        }
        struct timespec tick = { 0, 100000000L };
        nanosleep(&tick, NULL);
    }
    for (int i = 0; i < ndev; i++) if (fd[i] >= 0) close(fd[i]);
    printf("-- done\n");
    return 0;
}
