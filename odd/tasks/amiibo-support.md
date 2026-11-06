# Feature: amiibo support (BotW + TotK)

## Objective
Let the player scan amiibo in Breath of the Wild 1.6.0/1.9.0 and Tears of the Kingdom (1.0.0–1.4.3) on Mac by loading standard 540-byte NTAG215 `.bin` dumps from disk, through the port's own `nn::nfp` host implementation.

## Problem / Why
The host stubs every `nn::nfp::*` symbol with `0x8073` ("the host has no NFC reader"), so the amiibo rune never finds a tag. The player already has 743 valid NTAG215 dumps (540 bytes) in `/Users/jony/Library/CloudStorage/OneDrive-Personal/GAMES/amiiboall0630-1`.

## Scope
- In: one shared nfp state machine in `games/totk/runtime/` (this runtime builds BOTH games: `Makefile` compiles it with `-DGAME_BOTW=1` for BotW), a Mac key to pick a `.bin`, persistence of the last choice, README known-issues line update.
- Out: app-data decryption with amiibo keys (not needed for TagInfo/ModelInfo), other games (odyssey/by3 have their own runtime copies), iOS, NFC hardware readers.

## Constraints
- Generated artifacts in English; chat replies in Spanish (Rioplatense).
- TDD: OFF (source: prior session decision). Checks = `make check` at repo root + syntax-compile of changed runtime files.
- Ponytail full: minimal diff, no new abstractions, reuse `setting_read`/`setting_write` and the `runtime_front_*` frontend patterns.

## Facts the implementation must honor (research, source-backed)
- Enums: `State {0 NonInitialized, 1 Initialized}`; `DeviceState {0 Initialized, 1 SearchingForTag, 2 TagFound, 3 TagRemoved, 4 TagMounted, 5 Unavailable}`; `ModelType {0 Amiibo}`; `MountTarget {1 ROM, 2 RAM, 3 All}`.
- `TagInfo` = 0x58: `0x0 UUID[10]`, `0xA uuid_length u8`, `0xB pad[0x15]`, `0x20 Protocol u32`, `0x24 TagType u32`, rest 0. Use uuid = dump[0..8], length 9; Protocol=1, TagType=2 (yuzu values).
- `ModelInfo` = 0x40: `0x0 u16 CharacterId = dump[0x54..0x55]`, `0x2 variant = dump[0x56]`, `0x3 type = dump[0x57]`, `0x4..5 = byteswap(dump[0x58..0x59])`, `0x6 = dump[0x5A]`, rest 0.
- Amiibo ID sits PLAINTEXT at dump offset 0x54..0x5B; no decryption needed for these two games.
- Device: exactly one device, handle 0, NpadId = Player1 (0), availability event pre-signaled (cleared events risk a hang).
- Flow: Initialize→state1; ListDevices→1 handle; StartDetection→(if a file is selected) state TagFound + activate event signaled, else SearchingForTag; Mount→TagMounted; GetTagInfo legal in TagFound/TagMounted; GetModelInfo legal in TagMounted; StopDetection→deactivate signaled + state Initialized(0); Finalize→state0.
- Dump validation before presenting: size == 540 (accept 532/572 too if cheap), `dump[3]==0x88^b0^b1^b2`, `dump[8]==b4^b5^b6^b7`, `0x5B == 0x02`.
- BotW (1.6.0) additionally IMPORTS: `GetCommonInfo`, `GetRegisterInfo`, `OpenApplicationArea`, `GetApplicationArea`, `Unmount`, 2-arg `Mount(handle,ModelType)`, `StartRestorer` — these are absent from the override table today and must be added (fabricated/success-or-logged stubs; RegisterInfo/CommonInfo zero-filled with amiibo_initialized=1, app area 0xD8 zero bytes, OpenApplicationArea answers access id 0x1019C800).
- Existing result constant `NFP_DEVICE_NOT_FOUND 0x8073` keeps working as-is (host layer convention); do not "fix" it to 0x80008073 without checking other host results.

## Checklist

- [ ] T1 (delegated, inline-verified): nfp state machine in `games/totk/runtime/applet_host.c` — replace the `nfp_*` stubs with the real state machine above, load the dump from the path in `amiibo.txt` (App Support `<GAME_FOLDER>/`), validate it, expose `runtime_amiibo_set_path(const char *)` for T2. Covers BotW and TotK (shared runtime).
- [ ] T2 (delegated): BotW-only extra `nn::nfp` overrides in the same map (`#ifdef GAME_BOTW` section or unconditional — match how the map already gates BotW entries), plus the Mac key-8 picker: `input.c` key code 28 → open `NSOpenPanel` for `*.bin` (frontend.m, AppKit pattern), write chosen path to `amiibo.txt` via the `setting_write` pattern, add "8: amiibo" to `shortcut_text()`.
- [ ] T3 (inline): README known-issues + controls/shortcut doc line ("No online features, no amiibo" → mention amiibo dumps now supported; document key 8 and `amiibo.txt`).
- [ ] T4 (verification): `make check` at repo root; syntax-compile changed runtime files (`xcrun clang -std=c11 -Wall -Wextra -Werror -fsyntax-only` with the runtime include flags; frontend.m with `-fobjc-arc`); report exact results.
- [ ] T5 (after T1–T4 pass): rebuild both apps from the existing work folders (`~/Games/TOTK_build`, `~/Games/BotW_build`) with the same build command used before, then the user tests an amiibo scan in-game.

## Acceptance criteria
- In BotW and TotK, selecting a `.bin` with key 8 and using the amiibo rune yields the themed drop for that amiibo (per-game drop table already in the game).
- Without a selected file the game behaves as today (no reader/tag), no crash, no hang.
- `make check` green; changed files compile warning-free with `-Werror`.

## Progress
- [x] T1 — nfp state machine in `applet_host.c` (single device/handle 0, events, TagFound/TagMounted, GetTagInfo 0x58 / GetModelInfo 0x40 byte rules, dump load+validate, `runtime_amiibo_set_path`). Evidence: commit `15c39521`; `nm` shows `_runtime_amiibo_set_path` in both runtimes.
- [x] T2 — BotW's 7 extra nfp overrides (#ifdef GAME_BOTW, mangled names extracted from the installed main binaries), key 8 (Mac keycode 28 — verified non-sequential in SDK Events.h: kVK_ANSI_8=0x1C) → NSOpenPanel picker in frontend.m, `amiibo.txt` via un-static'd `setting_read`/`setting_write`, shortcut help line en/zh.
- [x] T3 — README: known-issues line + keys paragraph (key 8), touch prompt list.
- [x] T4 — `make check` with `SDKROOT=MacOSX26.5.sdk` + work-folder venv: **Ran 333 tests, OK** (base failures were the known CLT-27 tapi linker bug + missing `cryptography`; both environmental). Syntax compile `-Werror` clean for applet_host.c (both GAME_BOTW variants), input.c, frontend.m, gamepad.m. Incremental `make build/totk-native` linked exit 0 (writer) and again after final rsync (up to date).
- [x] T5 — Runtime rebuilt for both games in the existing work folders (`SDKROOT=...26.5.sdk`): TOTK `build/totk-native` (22.6 MB), BotW `build/totk-native-botw160` (15.3 MB), each with `_runtime_amiibo_pick`/`_runtime_amiibo_set_path` in `nm`. Original game inputs are no longer at the recorded SSD paths, so the app step was done the way build_app.py does it for a runtime-only change: binary copied over `Contents/Resources/totk/build/totk-native` in both apps (previous binaries backed up as `*.prev-amiibo` in the work folders), then `codesign --force --sign -` + `codesign --verify --strict` — both pass. Symbol count in installed apps: 2 each.
- [ ] T6 — Random UUID per scan (on by default): the games key their once-a-day amiibo rule on the tag UUID, so the dump's own UUID blocked a second scan until the next day. `nfp_load_dump` now randomizes the 7-byte UID (BCCs fixed up, amiibo type at 0x54.. untouched) on every load — Initialize and each StartDetection — via `games/totk/runtime/amiibo_uuid.h`; `TOTK_AMIIBO_RANDOM_UID=0` presents the dump's own UUID. Evidence: `tests/test_amiibo_uuid.{c,py}` (failed before, passes after); syntax compile clean for both variants; `make check` 334 tests (only sandbox-blocked `test_wiiu_bridge` socket bind failed in-sandbox, passes unsandboxed); both apps rebuilt, installed and `codesign --verify --strict` passes (previous binaries kept as `*.prev-random-uuid` in the work folders). Pending: user tests a second same-day scan in-game.

## Route / delegation record
- Route: delegated direct (writer trigger: 2+ non-trivial files + mapping done by explore agent); research delegated to general (source-backed nfp fact sheet); rebuild ran inline.
- TDD mode: off (source: prior session decision); gate command: `SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk make check PYTHON="$HOME/Games/TOTK_build/venv/bin/python"`.
- Review: RDD on; assess medium/262 lines/under_budget → deferred to slice close.
