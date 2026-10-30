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

- [x] T1 — `bridge_map.h` (pure C: VPAD hold bits + sticks → HID report layout) + probe test
      + Python test wrapper. DONE (writer). Verified by parent spot check: probe 35/35.
- [x] T2 — `tools/wiiu-gamepad-bridge/wiiu_bridge.m` (UDP receive, JSON reassembly/parse,
      IOHIDUserDevice publish, CLI main). DONE (writer). Compiles clean under -Werror.
- [~] T3 — Loopback integration test: DONE (split-datagram reassembly asserted, passes).
      Docs blurb: DONE (tools/wiiu-gamepad-bridge/README.md). Work-unit commit: DONE (9d2aab2).
      **Manual console verification: BLOCKED** — see Blocker below.

## Blocker: macOS 26 restricted entitlement (decision needed)

`IOHIDUserDeviceCreateWithProperties` returns NULL with `kIOReturnNotPrivileged` (0xe00002c2)
on macOS 26.6 — independently reproduced by the parent with a minimal probe. Publishing a
virtual HID device now requires the **restricted** entitlement
`com.apple.developer.hid.virtual.device` (adhoc-signing with it = SIGKILL without a
provisioning profile). The bridge degrades gracefully (receive + `--print-reports`) and
says so on stderr.

Also environmental (pre-existing, not caused by this change): the default toolchain's
MacOSX27.0.sdk .tbd files break the installed linker (tapi arch-variant rejection);
`make check` is red on the base branch too. Pinned `SDKROOT=MacOSX26.5.sdk` makes the
whole suite green (333 tests + 2 new).

Fallback options if the entitlement is not obtainable:
1. In-process integration: runtime reads the MiisendU UDP directly (full fidelity,
   scoped to MacNativeGames games).
2. CGEvent keyboard/mouse injection standalone (works everywhere, loses analog sticks).

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
- 2026-10-25: T1+T2+T3 code done by delegated writer (general, ses_e89752dc); parent spot
  checks: C probe 35/35, loopback OK, .m compiles -Werror (all under SDKROOT=MacOSX26.5.sdk
  due to the pre-existing 27.0 SDK linker breakage). Work-unit commit 9d2aab2 on
  feat/wiiu-gamepad-bridge. 540 authored lines (over the ~400 advisory; kept full tests).
- 2026-10-25: parent independently reproduced the entitlement blocker (IOHIDUserDevice NULL
  from a minimal probe). Next: user decision on fallback vs entitled build.
