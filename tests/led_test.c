#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define REPORT_SIZE 154
#define REPORT_ID_PROFILE_0 0xF3

int main(void) {
    int fd = open("/dev/hidraw1", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    uint8_t buf[REPORT_SIZE] = {0};
    buf[0] = REPORT_ID_PROFILE_0;

    int rc = ioctl(fd, HIDIOCGFEATURE(REPORT_SIZE), buf);
    if (rc < 0) { perror("HIDIOCGFEATURE"); close(fd); return 1; }

    printf("Report ID: 0x%02x\n", buf[0]);
    printf("LED R: %d G: %d B: %d\n", buf[1], buf[2], buf[3]);
    printf("LED effect: %d  duration: %d\n", buf[4], buf[5]);
    printf("Raw bytes 0-15: ");
    for (int i = 0; i < 16; i++) printf("%02x ", buf[i]);
    printf("\n");
printf("Bytes 10-20: ");
for (int i = 10; i <= 20; i++) printf("[%d]=%d ", i, buf[i]);
printf("\n");
    close(fd);
    return 0;
}
