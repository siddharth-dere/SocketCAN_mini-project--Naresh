# Step-by-Step Guide: How to Use the Assignment Solutions

This guide is written for a student who has never worked with Linux SocketCAN before. Treat your version and your friend's version as two separate repositories. Do not mix their source files, binaries, logs, or Git history.

## 1. First Understand What the Assignment Is Asking

The assignment is not asking for one C program. It asks for a small distributed automotive system made from four separate ECUs communicating over a CAN bus.

Think about the system like this:

```text
Sensors -> CAN bus -> BCM controller -> CAN bus -> Actuators
                         |
                         +---- CAN traffic observed by Diagnostic ECU
```

The four programs have different jobs:

1. `body_sensor_ecu.c`
   Generates simulated vehicle inputs such as doors, light, rain, ignition and speed.

2. `body_controller_ecu.c`
   Reads sensor messages, applies the BCM rules, and sends actuator commands.

3. `body_actuator_ecu.c`
   Pretends to be the vehicle's physical hardware. It receives commands and reports feedback/faults.

4. `body_diagnostic_ecu.c`
   Watches CAN traffic, checks values and communication timing, logs DTCs, and displays the system state.

## 2. What Is `vcan0`?

Normally an automotive ECU would use a physical CAN controller and transceiver. For this assignment there may be no CAN hardware, so Linux provides a virtual CAN interface called `vcan0`.

`vcan0` behaves like a CAN network in software.

Your programs do not directly call each other. They all open sockets connected to the same virtual CAN interface.

```text
Sensor process ------+
Controller process --+----> vcan0
Actuator process ----+
Diagnostic process --+
```

That is why this is a distributed-system exercise.

## 3. What Is SocketCAN?

SocketCAN is the Linux networking API for CAN. The important sequence used in each ECU is:

```text
socket()
   |
   v
PF_CAN / SOCK_RAW / CAN_RAW
   |
   v
Find interface index with ioctl(SIOCGIFINDEX)
   |
   v
bind() to vcan0
   |
   +---- write() CAN frame
   |
   +---- read() CAN frame
```

You do not need to memorize the API before running the project. First get the system working, then study the source.

## 4. What Is a CAN ID?

Every CAN message has an identifier. The assignment uses identifiers such as `0x118` or `0x218`.

A CAN ID tells you what the message represents. It does not automatically identify a C process.

For the friend version:

```text
0x118 = Body contacts
0x128 = Light and rain
0x138 = Ignition and speed
0x150 = Sensor heartbeat
0x218 = Cabin light command
0x228 = Lock command
0x238 = Wiper command
0x248 = BCM status
0x318 = Actuator feedback
0x320 = Actuator fault event
```

The exact signal mapping is documented in `README.md`.

## 5. What Happens When the System Starts?

The easiest way to understand execution is to follow one example.

### Example: Rain becomes high

1. Sensor ECU calculates a rain value.
2. Sensor ECU places the value in CAN ID `0x128`.
3. The frame is transmitted to `vcan0`.
4. BCM controller receives `0x128`.
5. BCM checks the range.
6. If rain is 80%, BCM selects Wiper HIGH.
7. BCM sends command `0x238` with wiper level `2`.
8. Actuator ECU receives `0x238`.
9. Actuator ECU updates its simulated wiper state.
10. Actuator ECU sends `0x318` feedback.
11. Diagnostic ECU sees both the command/feedback traffic and the current BCM mode.

This chain is the core of the assignment.

## 6. First-Time Setup on Linux

Use a Linux machine or Linux virtual machine with SocketCAN support.

Check GCC:

```bash
gcc --version
```

Check Make:

```bash
make --version
```

Check the `ip` command:

```bash
ip -V
```

Optional but useful:

```bash
candump --version
```

## 7. Create `vcan0`

Go into the repository:

```bash
cd Vehicle-Body-Control-SocketCAN-Friend
```

Make scripts executable:

```bash
chmod +x setup_vcan.sh run_system.sh
```

Create the virtual CAN interface:

```bash
./setup_vcan.sh vcan0
```

If successful, verify:

```bash
ip -details link show vcan0
```

You should see an interface named `vcan0` and its state should be UP.

## 8. Compile the Friend Version

Run:

```bash
make clean
make
```

Four executable files should appear:

```text
body_sensor_ecu
body_controller_ecu
body_actuator_ecu
body_diagnostic_ecu
```

You can verify them with:

```bash
ls -l body_*_ecu
```

If compilation fails, do not start the ECUs yet. Fix the compiler error first.

## 9. Start the ECUs the Easy Way

Run:

```bash
./run_system.sh vcan0
```

This starts all four programs.

A `runtime/` directory is created for process output.

Press `Ctrl+C` to stop the group.

## 10. Start the ECUs Manually for the Assignment Demo

Manual execution is better when your teacher asks you to demonstrate every ECU separately.

Open four terminal windows.

### Terminal 1: Diagnostic ECU

```bash
cd Vehicle-Body-Control-SocketCAN-Friend
./body_diagnostic_ecu --interface vcan0 --log body_diagnostic_log.txt
```

This terminal should show the live diagnostic dashboard.

### Terminal 2: Controller ECU

```bash
cd Vehicle-Body-Control-SocketCAN-Friend
./body_controller_ecu --interface vcan0
```

### Terminal 3: Actuator ECU

```bash
cd Vehicle-Body-Control-SocketCAN-Friend
./body_actuator_ecu --interface vcan0
```

### Terminal 4: Sensor ECU

```bash
cd Vehicle-Body-Control-SocketCAN-Friend
./body_sensor_ecu --interface vcan0
```

Now the system is running as four independent Linux processes.

## 11. Watch the CAN Bus

Open a fifth terminal:

```bash
candump vcan0
```

You should see the four ECUs communicating.

Look for the friend version's IDs:

```text
118
128
138
150
218
228
238
248
318
320
```

Do not worry if frames appear very quickly. Some are intentionally periodic.

## 12. Test Normal Operation

Start the system without fault flags.

Observe:

- Sensor frames arrive regularly.
- BCM status changes between RUN and SAFE depending on startup timing and received state.
- Actuator feedback is visible.
- Diagnostic HMI updates approximately once per second.

For your report, describe this as TC1.

## 13. Test a Sensor Fault

Stop the sensor ECU with `Ctrl+C` if it is running in a dedicated terminal.

Restart it using:

```bash
./body_sensor_ecu --interface vcan0 --fault-rain
```

The sensor now intentionally sends an invalid rain value above 100%.

Watch the diagnostic terminal and log:

```bash
tail -f body_diagnostic_log.txt
```

Expected behavior:

```text
Invalid rain value
      |
      v
Diagnostic DTC
      |
      v
BCM SAFE MODE
      |
      +--> doors unlocked
      +--> dome light ON
      +--> wiper LOW
```

This is TC2.

## 14. Test the Door Fault

Run:

```bash
./body_sensor_ecu --interface vcan0 --fault-door
```

The reserved bits in the door message are intentionally corrupted.

The diagnostic ECU should record a door-related DTC.

This provides another example for your viva/report because the system is not only checking environmental signals.

## 15. Test the Five-Second Timeout

Start the normal system.

Then terminate only the sensor ECU.

Do not terminate the controller or diagnostic ECU.

Wait more than five seconds.

The diagnostic ECU should report a watchdog timeout.

This demonstrates an important concept: a message can be valid when it arrives, but the system can still become unsafe if the message stops arriving.

## 16. Test Hot Recovery

After the timeout appears, start the sensor ECU again:

```bash
./body_sensor_ecu --interface vcan0
```

The sensor messages resume.

The diagnostic watchdog should stop reporting the active timeout condition after frames are received again, and the controller can return to normal operation when valid data are available.

This is TC5.

## 17. Test an Actuator Fault

Stop the current actuator ECU and restart it with:

```bash
./body_actuator_ecu --interface vcan0 --fault-actuator
```

The actuator now reports an electrical/status fault using `0x320`.

The diagnostic ECU should write an actuator DTC.

## 18. What Files Matter for Submission?

The friend repository should contain at least:

```text
Vehicle-Body-Control-SocketCAN-Friend/
├── body_sensor_ecu.c
├── body_controller_ecu.c
├── body_actuator_ecu.c
├── body_diagnostic_ecu.c
├── README.md
├── STEP_BY_STEP_GUIDE.md
├── Makefile
├── setup_vcan.sh
├── run_system.sh
└── .gitignore
```

After execution, these may also exist locally:

```text
body_sensor_ecu
body_controller_ecu
body_actuator_ecu
body_diagnostic_ecu
body_diagnostic_log.txt
runtime/
```

The `.gitignore` prevents generated binaries and runtime files from being pushed.

## 19. What You Should Actually Submit

For the assignment, the important source/design deliverables are:

1. Four C source files.
2. README technical report.
3. Mermaid architecture and control diagrams.
4. CAN matrix and signal mapping.
5. Test procedure/results.
6. Build and execution commands.
7. GitHub repository containing the project.

If your college specifically asks for screenshots, capture:

- `ip -details link show vcan0`
- successful `make`
- `candump vcan0` showing traffic
- diagnostic HMI during normal operation
- diagnostic HMI/log during a fault
- timeout DTC after stopping a node
- recovered communication after restarting it
- GitHub repository page

## 20. How Your Version and Your Friend's Version Should Be Kept Separate

Create two folders:

```text
Your project/
    Vehicle-Body-Control-SocketCAN/

Friend project/
    Vehicle-Body-Control-SocketCAN-Friend/
```

Run Git independently inside each folder.

Your repository:

```bash
cd Vehicle-Body-Control-SocketCAN
git status
```

Friend repository:

```bash
cd Vehicle-Body-Control-SocketCAN-Friend
git status
```

Never copy the `.git` directory from one project into the other.

Never use the same GitHub remote for both repositories unless you intentionally want the same repository.

## 21. Publishing the Friend Version

Inside the friend folder:

```bash
cd Vehicle-Body-Control-SocketCAN-Friend
git init
git branch -M main
git add .
git commit -m "Create alternate distributed BCM SocketCAN implementation"
```

Then either create the GitHub repository through the web or use GitHub CLI:

```bash
gh auth login
gh repo create FRIEND_USERNAME/Friend-BCM-SocketCAN --public --source=. --remote=origin --push
```

Replace `FRIEND_USERNAME` with the friend's actual GitHub username.

## 22. What to Say During a Viva

### What is the purpose of the project?

It demonstrates a distributed Vehicle Body Control Module in Linux where four independent ECU processes exchange automotive body signals and commands using SocketCAN.

### Why use `vcan0`?

It allows CAN communication to be tested without physical CAN transceivers or vehicle hardware.

### Why four ECUs instead of one program?

The assignment is modeling a distributed automotive architecture. Separation makes sensing, control, actuation, and diagnostics independent and easier to test.

### What happens when a sensor sends an invalid value?

The BCM validates the signal, sets SAFE MODE, commands unlocked doors, dome light ON, and wiper LOW, while the diagnostic ECU records a DTC.

### Why is the diagnostic ECU separate?

An independent monitor is useful because the controller should not be the only component deciding whether the system is healthy.

### Why is the timeout five seconds?

The specification explicitly requires periodic-message supervision with a 5-second timeout.

## 23. Important Difference Between the Two Repositories

Your original repository and the friend repository may implement the same assignment requirements, but they should not be treated as identical copies.

The friend version here differs in meaningful engineering choices:

- Different CAN identifiers.
- Different payload layouts.
- Separate sensor heartbeat message.
- Door/trunk model instead of the original two-contact pattern.
- Ignition + speed carried together.
- Explicit `enum bcm_mode` controller state.
- Different light thresholds and rain thresholds.
- Different simulation waveforms.
- Different diagnostic codes and log formatting.
- Different command/feedback representation.
- Different startup/run scripts and documentation structure.

That is much stronger than simply renaming variables in copied code.

## 24. Recommended Order for Completing the Assignment

Do not try to understand all four source files at once.

Use this order:

```text
1. Create vcan0
2. Compile all four programs
3. Start diagnostic ECU
4. Start actuator ECU
5. Start controller ECU
6. Start sensor ECU
7. Observe candump
8. Understand one signal path (rain -> BCM -> wiper)
9. Perform fault injection
10. Perform node-loss timeout test
11. Perform recovery test
12. Collect screenshots
13. Review README
14. Create Git repository
15. Push to GitHub
```

Once those steps work, study the code function-by-function instead of trying to memorize the entire project.
