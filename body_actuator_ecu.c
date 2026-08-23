/* Alternate BCM implementation - Actuator Plant ECU */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t live = 1;
static void terminate(int sig) { (void)sig; live = 0; }

static int can_attach(const char *dev) {
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) { perror("socket"); return -1; }
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", dev);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) { perror("SIOCGIFINDEX"); close(fd); return -1; }
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); close(fd); return -1; }
    return fd;
}

static int emit(int fd, canid_t id, const uint8_t *data, uint8_t dlc) {
    struct can_frame tx;
    memset(&tx, 0, sizeof(tx));
    tx.can_id = id;
    tx.can_dlc = dlc;
    memcpy(tx.data, data, dlc);
    return write(fd, &tx, CAN_MTU) == CAN_MTU ? 0 : -1;
}

static double now_mono(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    const char *iface = "vcan0";
    int inject_electrical_fault = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--interface") == 0 && i + 1 < argc) iface = argv[++i];
        else if (strcmp(argv[i], "--fault-actuator") == 0) inject_electrical_fault = 1;
        else { fprintf(stderr, "Usage: %s [--interface IFACE] [--fault-actuator]\n", argv[0]); return EXIT_FAILURE; }
    }

    signal(SIGINT, terminate);
    signal(SIGTERM, terminate);
    int fd = can_attach(iface);
    if (fd < 0) return EXIT_FAILURE;

    uint8_t light_cmd = 0, lock_cmd = 0, wiper_cmd = 0, safe_cmd = 1;
    double last_feedback = 0.0;
    uint8_t fault_sequence = 0U;
    printf("[ACTUATOR-PLANT] interface=%s forced_fault=%d\n", iface, inject_electrical_fault);

    while (live) {
        fd_set inputs;
        FD_ZERO(&inputs);
        FD_SET(fd, &inputs);
        struct timeval tick = {0, 100000};
        int result = select(fd + 1, &inputs, NULL, NULL, &tick);
        if (result < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }
        if (result > 0 && FD_ISSET(fd, &inputs)) {
            struct can_frame command;
            if (read(fd, &command, CAN_MTU) == CAN_MTU) {
                switch (command.can_id) {
                    case 0x218: if (command.can_dlc >= 1) light_cmd = command.data[0]; break;
                    case 0x228: if (command.can_dlc >= 1) lock_cmd = command.data[0]; break;
                    case 0x238: if (command.can_dlc >= 1) wiper_cmd = command.data[0]; break;
                    case 0x248: if (command.can_dlc >= 1) safe_cmd = command.data[0]; break;
                    default: break;
                }
            }
        }

        if (now_mono() - last_feedback >= 0.500) {
            uint8_t error_flags = 0U;
            if (light_cmd > 2U || lock_cmd > 1U || wiper_cmd > 2U) error_flags |= 0x01U;
            if (safe_cmd) error_flags |= 0x02U;
            if (inject_electrical_fault) error_flags |= 0x04U;

            uint8_t feedback[4] = {lock_cmd, light_cmd, wiper_cmd, safe_cmd};
            uint8_t electrical[2] = {error_flags, ++fault_sequence};
            (void)emit(fd, 0x318, feedback, 4);
            if (error_flags != 0U) (void)emit(fd, 0x320, electrical, 2);
            last_feedback = now_mono();
        }
    }

    close(fd);
    return EXIT_SUCCESS;
}
