// Userspace PIR driver: reads SparkFun Qwiic PIR (0x12) and writes JSON lines to /tmp/pir_fifo
// Build with CMake (already set up). Run on BeaglePlay: ./pir-driver -b 5 -a 0x12
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define DEFAULT_BUS   5
#define DEFAULT_ADDR  0x12
#define EVENT_REG     0x03
#define FIFO_PATH     "/tmp/pir_fifo"

static volatile sig_atomic_t runflag = 1;
static void on_sig(int s){ (void)s; runflag = 0; }

static void msleep(int ms){
    struct timespec ts = { ms/1000, (ms%1000)*1000000L };
    nanosleep(&ts, NULL);
}

static int i2c_read_byte(int fd, uint8_t addr, uint8_t reg, uint8_t *val){
    struct i2c_msg msgs[2];
    uint8_t wbuf[1] = { reg }, rbuf[1] = { 0 };
    msgs[0].addr = addr; msgs[0].flags = 0;        msgs[0].len = 1; msgs[0].buf = wbuf;
    msgs[1].addr = addr; msgs[1].flags = I2C_M_RD; msgs[1].len = 1; msgs[1].buf = rbuf;
    struct i2c_rdwr_ioctl_data x = { .msgs = msgs, .nmsgs = 2 };
    if (ioctl(fd, I2C_RDWR, &x) < 0) return -1;
    *val = rbuf[0];
    return 0;
}

static void usage(const char *p){
    fprintf(stderr, "Usage: %s [-b <bus>] [-a <addr>]\n", p);
}

int main(int argc, char **argv){
    int bus = DEFAULT_BUS, addr = DEFAULT_ADDR;
    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i], "-b") && i+1<argc) bus = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-a") && i+1<argc) addr = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
    }

    signal(SIGINT, on_sig); signal(SIGTERM, on_sig);

    // Ensure FIFO exists; open RDWR so writer never blocks without a reader
    mkfifo(FIFO_PATH, 0666);
    int fdfifo = open(FIFO_PATH, O_RDWR | O_NONBLOCK);
    if (fdfifo < 0){ perror("open fifo"); return 1; }

    char dev[32]; snprintf(dev, sizeof(dev), "/dev/i2c-%d", bus);
    int fdi2c = open(dev, O_RDWR);
    if (fdi2c < 0){ perror("open i2c"); close(fdfifo); return 1; }

    bool last = false; bool first = true;

    while (runflag){
        uint8_t st = 0;
        if (i2c_read_byte(fdi2c, (uint8_t)addr, EVENT_REG, &st) == 0){
            bool motion = (st != 0x00);              // any nonzero -> motion
            if (first || motion != last){
                dprintf(fdfifo, "{\"motion\": %s}\n", motion ? "true":"false");
                last = motion; first = false;
            }
        }
        msleep(50);
    }

    close(fdi2c); close(fdfifo);
    return 0;
}
