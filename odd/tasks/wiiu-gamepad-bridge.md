# Feature: Wii U GamePad bridge para macOS (standalone)

Created: 2026-10-25
Route: delegated direct (ODD — writer trigger: 4+ files)
Delivery strategy: ask-on-risk | Chain strategy: (not yet needed)
TDD: off (no strict-tdd configured for this repo) — ordinary functional checks
Test runner: `make check` (python unittest + cc probes with -Wall -Wextra -Werror -fsanitize=undefined)

## Objective

Let a Wii U GamePad be used on macOS (26.x, Apple Silicon) as a system-wide gamepad by
receiving the open MiisendU UDP protocol and publishing a virtual HID gamepad — without
modifying the closed-source 32-bit UsendMii.app (impossible: no source, i386-only,
unsupported since macOS 10.15).

## Problem / Why

- User wants the Wii U GamePad connected on macOS. UsendMii.app (Downloads) is a Delphi
  FMX **i386-only closed binary**: cannot run on macOS >= 10.15, cannot be ported (no source).
- The Wii U side client (MiisendU-Wii-U, MIT, open source) already sends full GamePad
  state as **JSON over UDP port 4242** (~100 Hz, configurable via settings.ini on SD).
- Therefore only the *receive + inject* side is missing on macOS.

## Scope

### In scope (v1)
1. UDP listener (default port 4242) for MiisendU JSON messages.
2. Robust receive: client may split JSON > 1400 bytes across datagrams (udp.cpp chunking);
   buffer per-sender until the JSON is complete, then parse with NSJSONSerialization.
3. Parse `wiiUGamePad`: `hold` (button bitmask), `lStickX/Y`, `rStickX/Y`.
4. Publish a virtual HID gamepad via `IOHIDUserDevice` (user space, no kext):
   16 buttons, hat switch (dpad), 4 axes (2 sticks). Generic (non-Nintendo) VID/PID so the
   runtime's Switch-Pro-specific paths (gamepad_feedback.m matches 0x057e:0x2009) ignore it.
5. Pure-C mapping core in a header (`bridge_map.h`) so it is unit-testable like
   `gamepad_layout.h`, fed by authoritative VPAD button masks from devkitPro/wut
   `vpad/input.h` (fetch source; do not guess masks).
6. CLI: `wiiu_bridge [--port N] [--verbose]`.
7. Tests: probe C test + Python wrapper following the existing tests/ pattern; plus a
   loopback test (send synthetic JSON over UDP, assert produced HID report bytes).

### Out of scope (v1) — honest gaps
- Gyro/accelerometer (JSON has it, but mapping it to a HID motion report that
  GCController consumes is a separate investigation).
- Touch screen (tpX/tpY) → mouse injection.
- Rumble back-channel (MiisendU client is send-only; protocol has no return path).
- Wii Remotes / Wii U Pro / GameCube pads from the same JSON stream.
- Modifying UsendMii.app itself (closed source, 32-bit — impossible).

## Constraints

- Artifacts in English; user-facing replies in Spanish (Rioplatense).
- No new dependencies: Foundation + IOKit only (already available).
- Follow repo conventions: tests/ Python unittest + C probes, `make check` must stay green.
- Ponytail mode: fewest files, no speculative abstractions.
- Branch first (currently on main): `feat/wiiu-gamepad-bridge`.

## Acceptance criteria

- [ ] `make check` passes (existing + new tests).
- [ ] Bridge compiles with `-Wall -Wextra -Werror`.
- [ ] Loopback test: synthetic MiisendU JSON in → expected HID report bytes out.
- [ ] Button/stick mapping matches wut VPAD masks (cited from source).
- [ ] Documented manual verification path: run bridge + MiisendU on console → device
      appears (ioreg/GCController) and input reaches MacNativeGames runtime.
- [ ] Work-unit commit(s) with conventional commit messages.

## Tasks

- [ ] T1 — `bridge_map.h` (pure C: VPAD hold bits + sticks → HID report layout) + probe test
      + Python test wrapper. Route: writer (delegated). Evidence: 3+ files.
- [ ] T2 — `tools/wiiu-gamepad-bridge/wiiu_bridge.m` (UDP receive, JSON reassembly/parse,
      IOHIDUserDevice publish, CLI main). Route: writer (delegated). Evidence: non-trivial new file.
- [ ] T3 — Loopback integration test (UDP sender → bridge → assert HID report), README/docs
      blurb, full `make check` + compile verification, work-unit commit(s).

## Route declaration

| Task | Route | Trigger evidence |
|------|-------|------------------|
| T1   | delegated (writer) | 3+ files (header + 2 test files) |
| T2   | delegated (writer) | non-trivial new implementation file |
| T3   | delegated (same writer batch or follow-up) | test + docs + verify |

## Progress / evidence

- 2026-10-25: research done inline (UsendMii binary inspected = i386 Delphi; protocol
  documented from Crayon2000/MiisendU-Wii-U source: vpad_to_json.cpp, udp.cpp, main.cpp;
  port 4242 default; JSON split at 1400 bytes). Discovery saved to Engram.
- Next: launch T1+T2 writer.
