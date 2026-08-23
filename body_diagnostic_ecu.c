/* Alternate BCM implementation - Diagnostic Supervisor ECU */
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

static volatile sig_atomic_t active = 1;
static void stop_diag(int sig) { (void)sig; active = 0; }

static int connect_can(const char *iface) {
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) { perror("socket"); return -1; }
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) { perror("SIOCGIFINDEX"); close(fd); return -1; }
    struct sockaddr_can endpoint;
    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.can_family = AF_CAN;
    endpoint.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, (struct sockaddr *)&endpoint, sizeof(endpoint)) < 0) { perror("bind"); close(fd); return -1; }
    return fd;
}

static double clock_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void write_dtc(FILE *log, const char *code, const char *text) {
    time_t wall = time(NULL);
    struct tm local_tm;
    localtime_r(&wall, &local_tm);
    fprintf(log, "%04d-%02d-%02d %02d:%02d:%02d | %s | %s\n",
            local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday,
            local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec, code, text);
    fflush(log);
}

static void screen(void) {
    printf("\033[H\033[2J");
}

int main(int argc, char **argv) {
    const char *iface = "vcan0";
    const char *log_file = "body_diagnostic_log.txt";
    if (argc == 3 && strcmp(argv[1], "--interface") == 0) iface = argv[2];
    else if (argc == 5 && strcmp(argv[1], "--interface") == 0 && strcmp(argv[3], "--log") == 0) { iface = argv[2]; log_file = argv[4]; }
    else if (argc != 1) { fprintf(stderr, "Usage: %s [--interface IFACE] [--log FILE]\n", argv[0]); return EXIT_FAILURE; }

    signal(SIGINT, stop_diag);
    signal(SIGTERM, stop_diag);
    int fd = connect_can(iface);
    if (fd < 0) return EXIT_FAILURE;
    FILE *log = fopen(log_file, "a");
    if (!log) { perror("fopen log"); close(fd); return EXIT_FAILURE; }

    time_t last_rx[2048];
    memset(last_rx, 0, sizeof(last_rx));
    const canid_t watched[] = {0x118, 0x128, 0x138, 0x248, 0x318};
    const char *names[] = {"DOOR", "ENV", "VEHICLE", "BCM", "ACTUATOR"};
    const char *codes[] = {"WD01", "WD02", "WD03", "WD04", "WD05"};
    int timeout_active[5] = {0, 0, 0, 0, 0};
    uint8_t door_mask = 0U, rain = 0U, ignition = 0U, speed = 0U;
    uint16_t lux = 0U;
    uint8_t bcm_mode = 1U, bcm_fault = 1U;
    uint8_t lock_fb = 0U, light_fb = 0U, wiper_fb = 1U;
    unsigned long dtc_total = 0UL;
    double next_display = 0.0;

    printf("[DIAG-SUPERVISOR] interface=%s log=%s\n", iface, log_file);

    while (active) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        struct timeval timeout = {1, 0};
        int ready = select(fd + 1, &set, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (ready > 0 && FD_ISSET(fd, &set)) {
            struct can_frame frame;
            if (read(fd, &frame, CAN_MTU) == CAN_MTU) {
                time_t arrival = time(NULL);
                if (frame.can_id < 2048U) last_rx[frame.can_id] = arrival;
                switch (frame.can_id) {
                    case 0x118:
                        if (frame.can_dlc >= 4) {
                            door_mask = (uint8_t)(frame.data[0] | (frame.data[1] << 1) | (frame.data[2] << 2));
                            if (frame.data[3] & 0x80U) { write_dtc(log, "S-DOOR", "Door contact payload flagged invalid"); ++dtc_total; }
                        }
                        break;
                    case 0x128:
                        if (frame.can_dlc >= 4) {
                            lux = (uint16_t)(frame.data[0] | ((uint16_t)frame.data[1] << 8));
                            rain = frame.data[2];
                            if (lux > 50000U || rain > 100U) { write_dtc(log, "S-SENSE", "Environment signal out of range"); ++dtc_total; }
                        }
                        break;
                    case 0x138:
                        if (frame.can_dlc >= 4) { ignition = frame.data[0]; speed = frame.data[1]; if (speed > 200U || ignition > 1U) { write_dtc(log, "S-VEH", "Vehicle state signal invalid"); ++dtc_total; } }
                        break;
                    case 0x248:
                        if (frame.can_dlc >= 3) { bcm_mode = frame.data[0]; bcm_fault = frame.data[1]; }
                        break;
                    case 0x318:
                        if (frame.can_dlc >= 4) { lock_fb = frame.data[0]; light_fb = frame.data[1]; wiper_fb = frame.data[2]; }
                        break;
                    case 0x320:
                        write_dtc(log, "A-ELEC", "Actuator electrical/status fault reported");
                        ++dtc_total;
                        break;
                    default:
                        break;
                }
            }
        }

        time_t current = time(NULL);
        for (size_t i = 0; i < sizeof(watched)/sizeof(watched[0]); ++i) {
            if (last_rx[watched[i]] == 0) continue;
            if (difftime(current, last_rx[watched[i]]) > 5.0) {
                if (!timeout_active[i]) {
                    char message[96];
                    snprintf(message, sizeof(message), "%s frame 0x%03X missing for more than 5 seconds", names[i], watched[i]);
                    write_dtc(log, codes[i], message);
                    ++dtc_total;
                    timeout_active[i] = 1;
                }
            } else {
                timeout_active[i] = 0;
            }
        }

        double now = clock_seconds();
        if (now >= next_display) {
            screen();
            printf("=== BCM DIAGNOSTIC SUPERVISOR ===\n");
            printf("CAN: %-8s   DTC events: %-5lu\n\n", iface, dtc_total);
            printf("INPUTS\n");
            printf("  Doors mask : 0x%02X\n", door_mask);
            printf("  Light      : %u lux\n", lux);
            printf("  Rain       : %u %%\n", rain);
            printf("  Ignition   : %u\n", ignition);
            printf("  Speed      : %u km/h\n\n", speed);
            printf("BCM STATE\n");
            printf("  Mode       : %s\n", bcm_mode == 0U ? "RUN" : "SAFE");
            printf("  Fault flag : %u\n\n", bcm_fault);
            printf("ACTUATOR FB\n");
            printf("  Lock       : %u\n", lock_fb);
            printf("  Dome mode  : %u\n", light_fb);
            printf("  Wiper      : %u\n", wiper_fb);
            printf("\nWatchdog: 5-second periodic supervision\n");
            fflush(stdout);
            next_display = now + 1.0;
        }
    }

    fclose(log);
    close(fd);
    return EXIT_SUCCESS;
}
