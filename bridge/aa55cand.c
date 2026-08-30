#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#define FRAME_LEN 20

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static uint8_t checksum17(const uint8_t *frame) {
    unsigned sum = 0;
    for (int i = 2; i <= 18; i++)
        sum += frame[i];
    return (uint8_t)(sum & 0xff);
}

static int open_serial(const char *path) {
    int fd = open(path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("open serial");
        return -1;
    }
    struct termios tio;
    if (tcgetattr(fd, &tio) < 0) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }
    cfmakeraw(&tio);
    cfsetispeed(&tio, B2000000);
    cfsetospeed(&tio, B2000000);
    tio.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
    tio.c_cflag |= CS8 | CLOCAL | CREAD;
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &tio) < 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

static int open_can(const char *ifname) {
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        perror("socket(PF_CAN)");
        return -1;
    }
    struct ifreq ifr = {0};
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("SIOCGIFINDEX");
        close(fd);
        return -1;
    }
    struct sockaddr_can addr = {0};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    return fd;
}

static void send_config(int serial_fd, uint8_t can_speed_code) {
    uint8_t cfg[FRAME_LEN] = {0};
    cfg[0] = 0xAA;
    cfg[1] = 0x55;
    cfg[2] = 0x02; /* fixed 20-byte mode */
    cfg[3] = can_speed_code;
    cfg[4] = 0x01; /* default outgoing frame type: standard */
    cfg[13] = 0x00; /* normal mode */
    cfg[14] = 0x00; /* auto-retransmit enabled */
    cfg[19] = checksum17(cfg);
    if (write(serial_fd, cfg, FRAME_LEN) != FRAME_LEN)
        perror("write config");
}

static void serial_to_can(const uint8_t *buf, int can_fd) {
    if (buf[0] != 0xAA || buf[1] != 0x55 || buf[2] != 0x01)
        return;
    if (checksum17(buf) != buf[19]) {
        fprintf(stderr, "aa55cand: bad checksum, dropping frame\n");
        return;
    }
    struct can_frame frame = {0};
    uint8_t frame_type = buf[3]; /* 1 standard, 2 extended */
    uint8_t dlc = buf[9] > 8 ? 8 : buf[9];
    uint32_t id = (uint32_t)buf[5] | ((uint32_t)buf[6] << 8) |
                  ((uint32_t)buf[7] << 16) | ((uint32_t)buf[8] << 24);
    frame.can_id = (frame_type == 2) ? ((id & CAN_EFF_MASK) | CAN_EFF_FLAG)
                                      : (id & CAN_SFF_MASK);
    frame.can_dlc = dlc;
    memcpy(frame.data, buf + 10, dlc);
    if (write(can_fd, &frame, sizeof(frame)) != (ssize_t)sizeof(frame))
        perror("write can");
}

static void can_to_serial(const struct can_frame *frame, int serial_fd) {
    uint8_t buf[FRAME_LEN] = {0};
    buf[0] = 0xAA;
    buf[1] = 0x55;
    buf[2] = 0x01;
    int ext = frame->can_id & CAN_EFF_FLAG;
    buf[3] = ext ? 0x02 : 0x01;
    buf[4] = 0x01;
    uint32_t id = ext ? (frame->can_id & CAN_EFF_MASK) : (frame->can_id & CAN_SFF_MASK);
    buf[5] = id & 0xff;
    buf[6] = (id >> 8) & 0xff;
    buf[7] = (id >> 16) & 0xff;
    buf[8] = (id >> 24) & 0xff;
    uint8_t dlc = frame->can_dlc > 8 ? 8 : frame->can_dlc;
    buf[9] = dlc;
    memcpy(buf + 10, frame->data, dlc);
    buf[19] = checksum17(buf);
    if (write(serial_fd, buf, FRAME_LEN) != FRAME_LEN)
        perror("write serial");
}

static void usage(const char *prog, FILE *out) {
    fprintf(out,
            "aa55cand - bridges a Waveshare USB-CAN-A (AA55 protocol) serial\n"
            "adapter to a Linux SocketCAN interface.\n"
            "\n"
            "Usage: %s [-h] <serial-device> <can-ifname> [can-speed-code]\n"
            "\n"
            "Options:\n"
            "         -h          (show this help page)\n"
            "\n"
            "can-speed-code: 0x01=1M 0x02=800k 0x03=500k 0x04=400k 0x05=250k\n"
            "                0x06=200k 0x07=125k 0x08=100k 0x09=50k 0x0a=20k\n"
            "                0x0b=10k 0x0c=5k  (default 0x05 = 250k)\n"
            "\n"
            "Example:\n"
            "%s /dev/ttyUSB0 vcan1 0x05\n",
            prog, prog);
}

int main(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
        case 'h':
            usage(argv[0], stdout);
            return 0;
        default:
            usage(argv[0], stderr);
            return 1;
        }
    }

    if (argc - optind < 2) {
        usage(argv[0], stderr);
        return 1;
    }
    const char *serial_path = argv[optind];
    const char *can_ifname = argv[optind + 1];
    uint8_t can_speed_code = argc - optind > 2 ? (uint8_t)strtol(argv[optind + 2], NULL, 0) : 0x05;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    int serial_fd = open_serial(serial_path);
    if (serial_fd < 0)
        return 1;

    int can_fd = open_can(can_ifname);
    if (can_fd < 0) {
        close(serial_fd);
        return 1;
    }

    send_config(serial_fd, can_speed_code);

    uint8_t rxbuf[FRAME_LEN];
    int rxlen = 0;

    struct pollfd pfds[2];
    pfds[0].fd = serial_fd;
    pfds[0].events = POLLIN;
    pfds[1].fd = can_fd;
    pfds[1].events = POLLIN;

    fprintf(stderr, "aa55cand: bridging %s <-> %s (speed code 0x%02x)\n",
            serial_path, can_ifname, can_speed_code);

    while (!g_stop) {
        int r = poll(pfds, 2, 1000);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }
        if (pfds[0].revents & POLLIN) {
            uint8_t b;
            if (read(serial_fd, &b, 1) == 1) {
                if (rxlen == 0 && b != 0xAA)
                    continue; /* resync on header */
                rxbuf[rxlen++] = b;
                if (rxlen == FRAME_LEN) {
                    serial_to_can(rxbuf, can_fd);
                    rxlen = 0;
                }
            }
        }
        if (pfds[1].revents & POLLIN) {
            struct can_frame frame;
            if (read(can_fd, &frame, sizeof(frame)) == (ssize_t)sizeof(frame))
                can_to_serial(&frame, serial_fd);
        }
    }

    close(serial_fd);
    close(can_fd);
    return 0;
}
