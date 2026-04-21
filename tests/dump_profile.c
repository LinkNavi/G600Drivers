#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>
#include <string.h>
#include <dirent.h>

#define REPORT_SIZE 154

static void dump_profile(int fd, uint8_t report_id, int profile_num) {
    uint8_t buf[REPORT_SIZE] = {0};
    buf[0] = report_id;
    if (ioctl(fd, HIDIOCGFEATURE(REPORT_SIZE), buf) < 0) { perror("HIDIOCGFEATURE"); return; }

    printf("\n=== Profile %d (report 0x%02x) ===\n", profile_num, report_id);
    printf("  LED:  r=%d g=%d b=%d  effect=%d duration=%d\n",
           buf[1], buf[2], buf[3], buf[4], buf[5]);
    printf("  DPI:  shift=%d default=%d  slots=[%d %d %d %d]\n",
           buf[12] * 50, buf[13],
           buf[14] * 50, buf[15] * 50, buf[16] * 50, buf[17] * 50);
    printf("  Button slots 0-7 (code / mod / key):\n");
    for (int b = 0; b < 8; b++) {
        int off = 31 + b * 3;
        printf("    slot %d: code=0x%02x  mod=0x%02x  key=0x%02x\n",
               b, buf[off], buf[off+1], buf[off+2]);
    }
    printf("  G-shift color: r=%d g=%d b=%d\n", buf[91], buf[92], buf[93]);
}

/* Find the hidraw node for the G600 (VID=046d PID=c24a) interface 1.
   Returns a newly-opened fd or -1 on failure. */
static int open_g600_hidraw(void) {
    DIR *d = opendir("/dev");
    if (!d) { perror("opendir /dev"); return -1; }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "hidraw", 6) != 0) continue;

        char path[64];
        snprintf(path, sizeof(path), "/dev/%s", ent->d_name);
        int fd = open(path, O_RDWR);
        if (fd < 0) continue;

        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) == 0 &&
            (info.vendor  & 0xffff) == 0x046d &&
            (info.product & 0xffff) == 0xc24a) {
            /* Check for interface 1 by trying to read the profile report;
               interface 0 will fail or return a different report ID. */
            uint8_t buf[154] = {0};
            buf[0] = 0xF3;
            if (ioctl(fd, HIDIOCGFEATURE(154), buf) > 0 &&
                buf[0] == 0xF3) {
                printf("Found G600 hidraw at %s (VID=%04x PID=%04x)\n",
                       path, info.vendor & 0xffff, info.product & 0xffff);
                closedir(d);
                return fd;
            }
        }
        close(fd);
    }
    closedir(d);
    return -1;
}

int main(void) {
    int fd = open_g600_hidraw();
    if (fd < 0) {
        fprintf(stderr, "Could not find G600 hidraw device "
                "(is the mouse plugged in and are you in the 'input' group?)\n");
        return 1;
    }

    dump_profile(fd, 0xF3, 1);
    dump_profile(fd, 0xF4, 2);
    dump_profile(fd, 0xF5, 3);

    close(fd);
    return 0;
}
