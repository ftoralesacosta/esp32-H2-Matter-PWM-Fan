# Project Customization Rules

These rules are specific to the `esp32-H2-WPM-Fan` project pair programming workflow.

## 1. Build Errors Workflow
- When the user states there is a **build error**, always check the [build_log.txt](file:///Users/ftorales/Projects/esp32-H2-WPM-Fan/build_log.txt) file in the workspace repository.
- Before reading, run `git pull` to ensure any updates pushed from the flashing machine are synchronized locally.
- If `build_log.txt` has not changed after `git pull`, explicitly ask the user to update the build log from their flashing machine.

## 2. Running Issues Workflow
- When the user describes a **running issue** (e.g., pin voltage not working, HomeKit connectivity problems, device offline), always check the [monitor_log.txt](file:///Users/ftorales/Projects/esp32-H2-WPM-Fan/monitor_log.txt) file in the workspace repository.
- Before reading, run `git pull` to ensure the latest serial monitor log is synchronized.
- If the log hasn't changed, ask the user to update the monitor log.

## 3. Code Modification & Flashing Workflow
- **Multi-Machine Flashing Context:** The user frequently flashes the ESP board from a **different Mac** (the flashing machine) than the one where the edits are made (this machine).
- **Critical Requirement:** Because of this multi-machine workflow, you **MUST ALWAYS** commit and push all code, configuration, and documentation changes to Git immediately after implementing them. This allows the user to pull the changes on their flashing machine.
- **Workflow Steps:**
  1. **Summarize** the proposed edits to the user.
  2. **Implement** the edits immediately using the appropriate tool (do not wait for user confirmation).
  3. **Summarize** the completed edits and **automatically commit and push** them to Git (permission to auto-push has been granted).

## 4. Hardware & Architecture Reference
- At the start of the session, you **MUST** read [FINDINGS.md](file:///Users/ftorales/Projects/esp32-H2-WPM-Fan/.agents/FINDINGS.md) in the `.agents/` directory to understand the Seeed Studio XIAO ESP32-C6 pin mappings, the Thread Minimal End Device (MTD) configurations, and the critical flashing/erase workflows. Do not make assumptions about standard dev kit pinouts.

## 5. ESPHome Attempt Log Monitoring
- During the ESPHome transition/build phase, the user will write compilation and run outputs to [esphome_attempt.txt](file:///Users/ftorales/Projects/esp32-H2-WPM-Fan/esphome_attempt.txt).
- You **MUST** check if this file has been updated at the start of your turn. If the user mentions building/flashing but `esphome_attempt.txt` has not been updated, explicitly ask the user to update it.

## 6. Next Session Verification
- At the very start of the next session, you **MUST** ask the user: *"Did the overnight soak test succeed, or did the chip disconnect?"* This is the primary verification step to confirm if the baseline software/network stability is holding before doing any further testing with the fan motor.

## 7. Findings Document Maintenance
- You **MUST** update [.agents/FINDINGS.md](file:///Users/ftorales/Projects/esp32-H2-WPM-Fan/.agents/FINDINGS.md) immediately after any successful build, flash, and verification iteration.
- You **MUST** update `FINDINGS.md` every time a new hypothesis is formulated or tried. Document the hypothesis, the experiment, and clearly mark whether it was debunked or confirmed.
- Document the build status, log output, and network behavior to ensure the project history is continuously maintained.



