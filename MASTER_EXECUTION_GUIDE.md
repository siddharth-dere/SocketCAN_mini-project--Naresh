# Complete Beginner Guide: Your BCM Solution + Friend BCM Solution

This document explains what to do after receiving the assignment solutions. The most important rule is to keep the two projects as separate Git repositories.

## Part A — Understand the Assignment Before Running Code

The assignment specification describes four independent ECUs operating over Linux SocketCAN and `vcan0`: a sensor ECU, a central BCM controller, an actuator ECU, and a diagnostic ECU. It also requires 8+ CAN IDs, fault handling, diagnostics, Mermaid diagrams, test cases, build instructions, and Git deployment. fileciteturn0file0L3-L22

Think of the system as:

```text
                 CAN BUS (vcan0)
                      |
      +---------------+---------------+
      |               |               |
      v               v               v
  SENSOR ECU      BCM ECU       ACTUATOR ECU
      |               |               |
      +---------------+---------------+
                      |
                DIAGNOSTIC ECU
```

The programs are separate processes. They do not call one another directly.

## Part B — Which Folder Is Which?

### Your repository

Use the project already generated for your assignment:

```text
vehicle_body_control_socketcan/
```

Its main files are:

```text
body_sensor_ecu.c
body_controller_ecu.c
body_actuator_ecu.c
body_diagnostic_ecu.c
README.md
Makefile
setup_vcan.sh
run_system.sh
.gitignore
```

### Friend repository

Use the newly generated alternate project:

```text
Vehicle-Body-Control-SocketCAN-Friend/
```

It has the same major deliverables but a different implementation, CAN matrix, signal packing, controller organization, diagnostic scheme, fault modes, and documentation structure.

### Do NOT do this

Do not copy the friend `.git` directory into your project.

Do not put both projects in one Git repository.

Do not overwrite your project files with your friend's files.

Do not push both sets of code to the same GitHub repository if they are intended as separate submissions.

## Part C — Install/Verify the Linux Tools

Use a Linux system, Linux VM, or WSL setup that provides SocketCAN and the `vcan` kernel module.

Check:

```bash
gcc --version
make --version
ip -V
```

Optional:

```bash
candump --version
```

The container used to build these files successfully compiled the four ECUs, but it did not have a usable `vcan` kernel module. Therefore source compilation is verified here; actual CAN runtime testing must be done on a Linux environment with SocketCAN/vcan enabled.

## Part D — Run YOUR Original Solution

### Step 1: Enter your project

```bash
cd /path/to/vehicle_body_control_socketcan
```

### Step 2: Make the scripts executable

```bash
chmod +x setup_vcan.sh run_system.sh
```

### Step 3: Create the virtual CAN interface

```bash
./setup_vcan.sh vcan0
```

Then:

```bash
ip -details link show vcan0
```

### Step 4: Compile

```bash
make clean
make
```

The assignment specifically requires a GCC build using the Linux CAN headers and strict warning flags. fileciteturn0file0L168-L176

### Step 5: Open four terminals

Terminal 1:

```bash
./body_diagnostic_ecu --interface vcan0 --log body_diagnostic_log.txt
```

Terminal 2:

```bash
./body_actuator_ecu --interface vcan0
```

Terminal 3:

```bash
./body_controller_ecu --interface vcan0
```

Terminal 4:

```bash
./body_sensor_ecu --interface vcan0
```

### Step 6: Open a fifth terminal for CAN observation

```bash
candump vcan0
```

### Step 7: Normal test

Let all four programs run.

Take screenshots of:

```text
- diagnostic HMI
- candump traffic
- all four terminal processes
```

This corresponds to the normal distributed-operation test.

### Step 8: Sensor fault test

Try one of the original project's supported faults:

```bash
./body_sensor_ecu --interface vcan0 --fault-sensor
```

or:

```bash
./body_sensor_ecu --interface vcan0 --fault-light
```

or:

```bash
./body_sensor_ecu --interface vcan0 --fault-door
```

Observe the BCM safe state and diagnostic log.

### Step 9: Node-loss test

Stop the sensor ECU only.

Wait at least five seconds.

The diagnostic ECU should report a timeout condition.

### Step 10: Recovery test

Restart the sensor ECU.

Observe the return of periodic frames and normal processing.

## Part E — Run YOUR FRIEND'S Alternate Solution

The procedure is almost the same, but the commands and CAN IDs belong to the alternate design.

### Step 1: Enter the friend's repository

```bash
cd /path/to/Vehicle-Body-Control-SocketCAN-Friend
```

### Step 2: Make scripts executable

```bash
chmod +x setup_vcan.sh run_system.sh
```

### Step 3: Create `vcan0`

```bash
./setup_vcan.sh vcan0
```

### Step 4: Compile

```bash
make clean
make
```

### Step 5: Start all four programs manually

Terminal 1:

```bash
./body_diagnostic_ecu --interface vcan0 --log body_diagnostic_log.txt
```

Terminal 2:

```bash
./body_controller_ecu --interface vcan0
```

Terminal 3:

```bash
./body_actuator_ecu --interface vcan0
```

Terminal 4:

```bash
./body_sensor_ecu --interface vcan0
```

### Step 6: Observe CAN frames

```bash
candump vcan0
```

The friend version uses this different message set:

```text
0x118  Body contacts
0x128  Light/rain
0x138  Ignition/speed
0x150  Sensor heartbeat
0x218  Cabin light command
0x228  Lock command
0x238  Wiper command
0x248  BCM status
0x318  Actuator feedback
0x320  Actuator fault
```

### Step 7: Run a friend-version fault test

Rain fault:

```bash
./body_sensor_ecu --interface vcan0 --fault-rain
```

Light fault:

```bash
./body_sensor_ecu --interface vcan0 --fault-light
```

Door payload fault:

```bash
./body_sensor_ecu --interface vcan0 --fault-door
```

Speed fault:

```bash
./body_sensor_ecu --interface vcan0 --fault-speed
```

Actuator fault:

```bash
./body_actuator_ecu --interface vcan0 --fault-actuator
```

## Part F — Understand One Complete Signal Path

Do this instead of trying to understand every C line immediately.

Use the rain signal as your first example.

### Friend version

```text
Sensor ECU
   |
   | 0x128
   | rain = 80%
   v
vcan0
   |
   v
Controller ECU
   |
   | 80% >= 75%
   | choose Wiper HIGH
   v
0x238
   |
   v
Actuator ECU
   |
   | wiper = 2
   v
0x318 feedback
   |
   v
Diagnostic ECU
```

Once you understand this path, the rest of the project becomes easier.

## Part G — Understand the Five Required Safety/Diagnostic Tests

The source assignment explicitly lists TC1 through TC5: normal operation, sensor fault injection, BCM safe-state transition, ECU node loss/timeout detection, and hot recovery. fileciteturn0file0L153-L157

### TC1 — Normal operation

Run all four processes.

Expected:

```text
Messages are exchanged
BCM makes normal decisions
Actuator sends feedback
Diagnostic HMI shows live state
```

### TC2 — Sensor fault injection

Start a fault mode.

Expected:

```text
Bad value
   -> validation failure
   -> diagnostic DTC
   -> BCM fault indication
```

### TC3 — Safe state

Keep the invalid value active.

Expected safe outputs:

```text
Doors = unlocked
Dome light = ON
Wiper = LOW
Safe mode flag = 1
```

These safe-state requirements are explicitly specified in the assignment. fileciteturn0file0L109-L113

### TC4 — Node loss

Kill the sensor process.

Wait >5 seconds.

Expected:

```text
No periodic sensor frame
   -> watchdog timeout
   -> diagnostic DTC
```

The assignment requires timeout supervision at 5 seconds. fileciteturn0file0L121-L133

### TC5 — Hot recovery

Restart the stopped ECU.

Expected:

```text
Sensor frames resume
   -> timeout condition clears
   -> valid control resumes
```

## Part H — What Screenshots Should Be Collected?

For each project, aim to collect the same categories of evidence:

1. `vcan0` interface created.
2. `make` completed successfully.
3. `candump vcan0` showing frames.
4. Diagnostic dashboard during normal operation.
5. Fault injection command.
6. Diagnostic DTC in `body_diagnostic_log.txt`.
7. Safe-state behavior.
8. Timeout after stopping one ECU.
9. Recovery after restarting the ECU.
10. GitHub repository page.

## Part I — How to Write the Report

The README structure required by the assignment is approximately:

```text
System Design Report
    Problem Description & Scope
    Objectives & Safety Principles
    Architecture
    State Machine
    CAN Matrix
    Signal Encoding
    Design Decisions

Test & Verification Report
    Test Environment
    TC1
    TC2
    TC3
    TC4
    TC5
    Diagnostic log sample
    Conclusions

Build & Execution Guide

GitHub CLI Deployment Instructions
```

The assignment explicitly asks for the technical design report and test report inside `README.md`. fileciteturn0file0L249-L269

## Part J — Push YOUR Project to GitHub

Inside your repository:

```bash
cd /path/to/vehicle_body_control_socketcan
git init
git branch -M main
git add .
git commit -m "Implement distributed vehicle BCM over SocketCAN"
```

Then create and push the remote repository using your preferred GitHub method.

## Part K — Push FRIEND'S Project to GitHub

Inside the friend's repository:

```bash
cd /path/to/Vehicle-Body-Control-SocketCAN-Friend
git init
git branch -M main
git add .
git commit -m "Create alternate distributed BCM SocketCAN implementation"
```

Using GitHub CLI:

```bash
gh auth login
gh repo create FRIEND_USERNAME/Friend-BCM-SocketCAN --public --source=. --remote=origin --push
```

Replace `FRIEND_USERNAME` with the real GitHub username.

## Part L — Before Submission

For YOUR repository:

```bash
git status
git log --oneline -1
```

For FRIEND'S repository:

```bash
cd /path/to/Vehicle-Body-Control-SocketCAN-Friend
git status
git log --oneline -1
```

Then check that:

```text
[ ] Four source files exist
[ ] README exists
[ ] Makefile works
[ ] setup_vcan.sh exists
[ ] run_system.sh exists
[ ] Mermaid diagrams exist
[ ] CAN matrix exists
[ ] Five required test cases are documented
[ ] Fault test was actually performed
[ ] Timeout test was actually performed
[ ] Recovery test was actually performed
[ ] GitHub push succeeded
```

## Part M — Do Not Claim Tests You Did Not Run

The build has been verified for both versions here, but CAN runtime testing requires a Linux host with a working `vcan` interface. When preparing the final report, mark runtime tests as `PASS` only after you actually execute them and observe the expected result.

That distinction is important: compilation success does not prove that four processes communicated correctly on a running CAN bus.
