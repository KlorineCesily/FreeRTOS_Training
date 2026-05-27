# RP2350-PiZero Bring-Up Notes

Board: Waveshare RP2350-PiZero  
SDK: Raspberry Pi Pico SDK 2.2.0  
IDE: VSCode with Raspberry Pi Pico extension  
Build generator: Ninja

## Verified

- USB CDC serial output works.
- Manual BOOTSEL mode and UF2 copy works.
- Manual BOOTSEL mode plus VSCode `Run Project (USB)` works.
- `printf()` output over USB is visible in VSCode Serial Monitor.

## Current Flashing Note

`Run Project (USB)` succeeds when the board is already in BOOTSEL mode, but automatic reset from the running USB serial application into BOOTSEL currently fails.

The same symptom was reproduced from another Pico SDK project on the same RP2350-PiZero board, so this is currently more likely to be board/tooling/USB reset behavior than a project-specific build configuration issue.

Observed message:

```text
RP2350 device ... appears to have a USB serial connection, but picotool was unable to connect.
```

Observed VSCode Pico extension command shape:

```text
picotool.exe load <project>.elf -fx
```

Temporary workflow:

1. Compile the project.
2. Manually enter BOOTSEL mode.
3. Use VSCode `Run Project (USB)`.
4. Reopen the USB serial port in Serial Monitor.

This is tracked as a tooling/USB reset workflow issue, not as a firmware baseline blocker.

Follow-up: compare this behavior with other available Pico/RP-series boards using the same PC, USB cable, VSCode Pico extension, and `Run Project (USB)` flow.
