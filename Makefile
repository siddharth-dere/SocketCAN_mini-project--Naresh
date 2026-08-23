CC      := gcc
CFLAGS  := -Wall -Wextra -pedantic -std=c11 -O2
TARGETS := body_sensor_ecu body_controller_ecu body_actuator_ecu body_diagnostic_ecu

.PHONY: all clean verify

all: $(TARGETS)

body_sensor_ecu: body_sensor_ecu.c
	$(CC) $(CFLAGS) -o $@ $<

body_controller_ecu: body_controller_ecu.c
	$(CC) $(CFLAGS) -o $@ $<

body_actuator_ecu: body_actuator_ecu.c
	$(CC) $(CFLAGS) -o $@ $<

body_diagnostic_ecu: body_diagnostic_ecu.c
	$(CC) $(CFLAGS) -o $@ $<

verify: clean all
	@echo "Build verification complete."

clean:
	rm -f $(TARGETS)
