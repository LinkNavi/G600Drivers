#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>

#define REPORT_SIZE 154

int main(void) {
    int fd = open("/dev/hidraw1", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    uint8_t buf[REPORT_SIZE] = {0};
    buf[0] = 0xF3;  // profile 1
    if (ioctl(fd, HIDIOCGFEATURE(REPORT_SIZE), buf) < 0) { perror("get"); return 1; }

    printf("Full profile 1 report (%d bytes):\n", REPORT_SIZE);
    for (int i = 0; i < REPORT_SIZE; i++) {
        printf("[%3d] 0x%02x (%3d)", i, buf[i], buf[i]);
        if (i >= 31 && i < 91 && ((i - 31) % 3 == 0))
            printf("  <-- button %d code", (i - 31) / 3);
        if (i >= 31 && i < 91 && ((i - 31) % 3 == 1))
            printf("  <-- button %d modifier", (i - 31) / 3);
        if (i >= 31 && i < 91 && ((i - 31) % 3 == 2))
            printf("  <-- button %d key", (i - 31) / 3);
        printf("\n");
    }
printf("Read back: buf[1-5] = %02x %02x %02x %02x %02x\n",
       buf[1], buf[2], buf[3], buf[4], buf[5]);
    close(fd);
    return 0;
}
