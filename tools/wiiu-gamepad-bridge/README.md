# Wii U GamePad bridge for macOS

Receives the MiisendU UDP protocol (JSON, UDP port 4242 by default) sent by
[MiisendU-Wii-U](https://github.com/Crayon2000/MiisendU-Wii-U) running on a Wii U, and publishes
the GamePad's buttons, dpad and sticks as a virtual HID gamepad through `IOHIDUserDevice` (user
space, no kext). Out of scope in v1: gyro/accelerometer, touch screen, rumble (the protocol is
send-only), and the Wii Remotes / Wii U Pro / GameCube pads that may share the JSON stream.

## Build

```sh
clang -Wall -Wextra -Werror -framework Foundation -framework IOKit \
    tools/wiiu-gamepad-bridge/wiiu_bridge.m -o wiiu_bridge
```

## Run

```sh
./wiiu_bridge [--port N] [--verbose] [--print-reports]
```

- `--port N` — UDP port to listen on (default 4242; match the console client's settings.ini).
- `--verbose` — log each parsed GamePad state to stderr.
- `--print-reports` — print each produced HID report as one hex line to stdout (tests/debug).

Point MiisendU on the console at this Mac's IP. The device shows up as **Wii U GamePad Bridge**
(VID 0x1201, PID 0x0001 — deliberately not Nintendo's, so runtime paths that match 0x057e devices
ignore it); check with `ioreg -r -c IOHIDUserDevice` or watch reports with `--print-reports`.
Ctrl-C stops the bridge.

## Known gap on this machine

`IOHIDUserDeviceCreateWithProperties` fails with `kIOReturnNotPrivileged` (0xe00002c2) on
macOS 26.6: opening the virtual HID device service requires the restricted entitlement
`com.apple.developer.hid.virtual.device` (adhoc-signing with it is SIGKILLed without a
provisioning profile). The bridge keeps receiving, parsing and printing reports in that case
and says so on stderr; publishing needs an entitled build (Developer ID + provisioning
profile), which is the follow-up before the manual console verification.

## Tests

`make check` builds and runs `tests/wiiu_bridge_probe.c` (mapping and JSON reassembly scan) and
`tests/test_wiiu_bridge.py` (probe + a UDP loopback: synthetic MiisendU JSON in, expected HID
report bytes out, including a message split across two datagrams).
