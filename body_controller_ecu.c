/* Alternate BCM implementation - Decision ECU */
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

enum bcm_mode { BCM_RUN = 0, BCM_SAFE = 1 };
static volatile sig_atomic_t running = 1;
static void stop_now(int sig) { (void)sig; running = 0; }

static int bind_can(const char *iface) {
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) { perror("socket"); return -1; }
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) { perror("SIOCGIFINDEX"); close(fd); return -1; }
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); close(fd); return -1; }
    return fd;
}

static int send_can(int fd, canid_t id, const uint8_t *payload, uint8_t dlc) {
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id = id;
    frame.can_dlc = dlc;
    memcpy(frame.data, payload, dlc);
    return write(fd, &frame, CAN_MTU) == CAN_MTU ? 0 : -1;
}

static double mono_time(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int in_range(uint8_t door, uint16_t lux, uint8_t rain, uint8_t ignition, uint8_t speed) {
    return ((door & 0xF0U) == 0U) && lux <= 50000U && rain <= 100U && ignition <= 1U && speed <= 200U;
}

int main(int argc, char **argv) {
    const char *iface = "vcan0";
    if (argc == 3 && strcmp(argv[1], "--interface") == 0) iface = argv[2];
    else if (argc != 1) {
        fprintf(stderr, "Usage: %s [--interface IFACE]\n", argv[0]);
        return EXIT_FAILURE;
    }

    signal(SIGINT, stop_now);
    signal(SIGTERM, stop_now);
    int fd = bind_can(iface);
    if (fd < 0) return EXIT_FAILURE;

    uint8_t door_map = 0, ignition = 0, speed = 0, rain = 0;
    uint16_t lux = 500U;
    double seen_door = 0.0, seen_environment = 0.0, seen_state = 0.0, last_commands = 0.0;
    enum bcm_mode mode = BCM_SAFE;
    uint8_t fault_latched = 0;

    printf("[BCM-DECISION] interface=%s\n", iface);

    while (running) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);
        struct timeval wait_time = {0, 100000};
        int ready = select(fd + 1, &read_set, NULL, NULL, &wait_time);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (ready > 0 && FD_ISSET(fd, &read_set)) {
            struct can_frame rx;
            ssize_t bytes = read(fd, &rx, CAN_MTU);
            if (bytes != CAN_MTU) continue;
            double arrived = mono_time();

            switch (rx.can_id) {
                case 0x118:
                    if (rx.can_dlc >= 4) { door_map = rx.data[0] | (uint8_t)(rx.data[1] << 1) | (uint8_t)(rx.data[2] << 2); seen_door = arrived; }
                    break;
                case 0x128:
                    if (rx.can_dlc >= 4) { lux = (uint16_t)(rx.data[0] | ((uint16_t)rx.data[1] << 8)); rain = rx.data[2]; seen_environment = arrived; }
                    break;
                case 0x138:
                    if (rx.can_dlc >= 4) { ignition = rx.data[0]; speed = rx.data[1]; seen_state = arrived; }
                    break;
                default:
                    break;
            }
        }

        double now = mono_time();
        int stale = (seen_door > 0.0 && now - seen_door > 5.0) ||
                    (seen_environment > 0.0 && now - seen_environment > 5.0) ||
                    (seen_state > 0.0 && now - seen_state > 5.0);
        int invalid = !in_range(door_map, lux, rain, ignition, speed);

        if (stale || invalid) {
            mode = BCM_SAFE;
            fault_latched = 1U;
        } else {
            mode = BCM_RUN;
            fault_latched = 0U;
        }

        uint8_t dome_mode = 0U;
        uint8_t lock_state = 0U;
        uint8_t wiper_level = 0U;
        uint8_t cabin_mode = (uint8_t)mode;

        if (mode == BCM_SAFE) {
            dome_mode = 1U;
            lock_state = 0U;
            wiper_level = 1U;
        } else {
            if (door_map != 0U || lux < 100U) dome_mode = 1U;
            else if (lux < 250U) dome_mode = 2U;
            else dome_mode = 0U;

            if (rain >= 75U) wiper_level = 2U;
            else if (rain >= 25U) wiper_level = 1U;
            else wiper_level = 0U;

            /* Auto-lock policy: ignition active and speed beyond 15 km/h. */
            lock_state = (ignition && speed > 15U) ? 1U : 0U;
        }

        if (now - last_commands >= 0.250) {
            uint8_t light_msg[2] = {dome_mode, cabin_mode};
            uint8_t lock_msg[2] = {lock_state, fault_latched};
            uint8_t wiper_msg[2] = {wiper_level, rain};
            uint8_t status_msg[3] = {(uint8_t)mode, fault_latched, ignition};
            (void)send_can(fd, 0x218, light_msg, 2);
            (void)send_can(fd, 0x228, lock_msg, 2);
            (void)send_can(fd, 0x238, wiper_msg, 2);
            (void)send_can(fd, 0x248, status_msg, 3);
            last_commands = now;
        }
    }

    close(fd);
    return EXIT_SUCCESS;
}
