/* grabtest.c -- does /dev/input/event3 accept an exclusive grab right now?
 *
 * A diagnostic only: it opens the touch device, tries EVIOCGRAB, prints the
 * answer and exits. Nothing is drawn and no job is touched, so this is the
 * cheap way to tell whether some other process (Xorg, a stale game, a `dd`
 * capture, the kfb mirror) is holding the panel's input.
 *
 *   make grabtest                       # build the ARM binary
 *   cat grabtest | k.sh 'cat > /tmp/gt && chmod +x /tmp/gt && /tmp/gt'
 *
 * Exit status: 0 grabbed, 1 refused (errno printed), 2 open failed.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define EVIOCGRAB_ 0x40044590u
#define TOUCH_DEV  "/dev/input/event3"

int main(void) {
    int fd = open(TOUCH_DEV, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "grabtest: open %s: %s\n", TOUCH_DEV, strerror(errno));
        return 2;
    }
    int grab = 1;
    if (ioctl(fd, EVIOCGRAB_, &grab) != 0) {
        fprintf(stderr, "grabtest: EVIOCGRAB refused: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    grab = 0;
    ioctl(fd, EVIOCGRAB_, &grab);
    close(fd);
    printf("grabtest: grab OK, touch is free\n");
    return 0;
}
