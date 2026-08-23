/* Alternate BCM implementation - Sensor Gateway ECU */
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

static volatile sig_atomic_t keep_running = 1;
static void handle_stop(int sig) { (void)sig; keep_running = 0; }

static int open_can(const char *name) {
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) { perror("socket"); return -1; }
    struct ifreq req;
    memset(&req, 0, sizeof(req));
    snprintf(req.ifr_name, IFNAMSIZ, "%s", name);
    if (ioctl(fd, SIOCGIFINDEX, &req) < 0) { perror("SIOCGIFINDEX"); close(fd); return -1; }
    struct sockaddr_can local;
    memset(&local, 0, sizeof(local));
    local.can_family = AF_CAN;
    local.can_ifindex = req.ifr_ifindex;
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) { perror("bind"); close(fd); return -1; }
    return fd;
}

static int publish(int fd, canid_t id, const uint8_t *bytes, uint8_t len) {
    struct can_frame msg;
    memset(&msg, 0, sizeof(msg));
    msg.can_id = id;
    msg.can_dlc = len;
    memcpy(msg.data, bytes, len);
    ssize_t written = write(fd, &msg, CAN_MTU);
    if (written != CAN_MTU) { if (written < 0) perror("CAN transmit"); return -1; }
    return 0;
}

static double seconds_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void usage(const char *program) {
    fprintf(stderr,
        "Usage: %s [--interface IFACE] [--fault-light] [--fault-rain] [--fault-door] [--fault-speed]\n",
        program);
}

int main(int argc, char **argv) {
    const char *interface_name = "vcan0";
    int force_bad_light = 0, force_bad_rain = 0, force_bad_door = 0, force_bad_speed = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--interface") == 0 && i + 1 < argc) interface_name = argv[++i];
        else if (strcmp(argv[i], "--fault-light") == 0) force_bad_light = 1;
        else if (strcmp(argv[i], "--fault-rain") == 0) force_bad_rain = 1;
        else if (strcmp(argv[i], "--fault-door") == 0) force_bad_door = 1;
        else if (strcmp(argv[i], "--fault-speed") == 0) force_bad_speed = 1;
        else { usage(argv[0]); return EXIT_FAILURE; }
    }

    signal(SIGINT, handle_stop);
    signal(SIGTERM, handle_stop);
    int can_fd = open_can(interface_name);
    if (can_fd < 0) return EXIT_FAILURE;

    printf("[SENSOR-GW] interface=%s faults(light=%d rain=%d door=%d speed=%d)\n",
           interface_name, force_bad_light, force_bad_rain, force_bad_door, force_bad_speed);

    double next_cycle = seconds_now();
    uint32_t sample = 0;
    uint8_t previous_ignition = 0;

    while (keep_running) {
        double now = seconds_now();
        if (now < next_cycle) {
            struct timeval nap = {0, 5000};
            select(0, NULL, NULL, NULL, &nap);
            continue;
        }
        next_cycle += 0.100;

        uint8_t doors[4] = {0};
        uint8_t environment[4] = {0};
        uint8_t vehicle_state[4] = {0};

        /* The alternate simulation uses three body inputs and a deterministic speed profile. */
        uint8_t front_right = ((sample / 50U) % 2U) == 1U ? 1U : 0U;
        uint8_t rear_right = ((sample / 125U) % 2U) == 1U ? 1U : 0U;
        uint8_t trunk = ((sample / 200U) % 3U) == 2U ? 1U : 0U;
        uint8_t door_count = (uint8_t)(front_right + rear_right + trunk);
        uint8_t contact_fault = 0;
        if (force_bad_door) {
            door_count = 7U;
            contact_fault = 1U;
        }
        doors[0] = front_right;
        doors[1] = rear_right;
        doors[2] = trunk;
        doors[3] = (uint8_t)((door_count & 0x0FU) | (contact_fault << 7));

        uint16_t lux = (uint16_t)(240U + ((sample * 17U) % 700U));
        uint8_t rain = (uint8_t)((sample * 5U) % 101U);
        uint8_t sensor_flags = 0;
        if (force_bad_light) { lux = 62000U; sensor_flags |= 0x01U; }
        if (force_bad_rain) { rain = 140U; sensor_flags |= 0x02U; }
        environment[0] = (uint8_t)(lux & 0xFFU);
        environment[1] = (uint8_t)(lux >> 8);
        environment[2] = rain;
        environment[3] = sensor_flags;

        uint8_t ignition = ((sample / 180U) % 2U) ? 1U : 0U;
        uint8_t speed = ignition ? (uint8_t)(5U + ((sample * 3U) % 46U)) : 0U;
        uint8_t state_flags = 0;
        if (force_bad_speed) { speed = 250U; state_flags |= 0x01U; }
        if (ignition != previous_ignition) state_flags |= 0x02U;
        vehicle_state[0] = ignition;
        vehicle_state[1] = speed;
        vehicle_state[2] = state_flags;
        vehicle_state[3] = (uint8_t)(sample & 0xFFU);

        (void)publish(can_fd, 0x118, doors, 4);
        (void)publish(can_fd, 0x128, environment, 4);
        (void)publish(can_fd, 0x138, vehicle_state, 4);
        if ((sample % 50U) == 0U) {
            uint8_t heartbeat[2] = {0xA5U, (uint8_t)(sample / 50U)};
            (void)publish(can_fd, 0x150, heartbeat, 2);
        }

        previous_ignition = ignition;
        ++sample;
    }

    close(can_fd);
    return EXIT_SUCCESS;
}
