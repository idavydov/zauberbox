# Zauberbox Firmware Plan

This plan is based on the current firmware state and the current
[`SPECIFICATION.md`](./SPECIFICATION.md).

The current codebase is still a bring-up style application:

- only three firmware modules exist: [`main.cpp`](./firmware/src/main.cpp),
  [`audio_driver.cpp`](./firmware/src/audio_driver.cpp), and
  [`io_expander.cpp`](./firmware/src/io_expander.cpp)
- there is no SD-card media engine yet
- there is no QR scanner/camera pipeline yet
- there is no explicit state machine yet
- Wi-Fi provisioning exists, but the normal app server described in the spec
  does not
- button behavior is still diagnostic-oriented in places, for example `KEY3`
  plays `test.mp3`

The goal should be a gradual transition from bring-up code to a small set of
explicit services with a state-driven application layer.

## Immediate Technical Debt To Address First

These are the changes I would do before adding major new features.

### 1. Introduce a real application state model

Current issue:

- [`main.cpp`](./firmware/src/main.cpp) uses a small LED-oriented enum
  (`STATE_WAITING_AP`, `STATE_CONNECTING_WIFI`, `STATE_CONNECTED`,
  `STATE_RESETTING`) that does not match the product states in the spec.
- behavior is derived ad hoc from `AyresWiFiManager` instead of from one
  authoritative app state

Recommendation:

- define one canonical app state model for the product, for example:
  `Boot`, `QrScan`, `Idle`, `Playing`, `Paused`, `Sleep`, `WifiPortal`,
  `Resetting`
- model Wi-Fi as a separate service/overlay state rather than forcing
  provisioning and background STA serving into the same enum branch
- make LED, buttons, audio cues, camera behavior, and Wi-Fi behavior react to
  that model
- stop letting UI behavior depend directly on `awm.isConnected()` and
  `awm.isPortalActive()` outside a narrow integration layer

Why first:

- almost every spec feature depends on clear state transitions
- without this, new features will keep getting added as one-off conditionals in
  `main.cpp`

### 2. Split `main.cpp` into small controllers

Current issue:

- `main.cpp` currently owns boot flow, button tasks, reset logic, LED
  animation, Wi-Fi-manager orchestration, and some audio triggering
- this will not scale once QR scanning, SD playback, sleep, and app-server
  behavior are added

Recommendation:

- move toward a structure like:
  - `app_controller.*`
  - `button_controller.*`
  - `led_controller.*`
  - `wifi_service.*`
  - `media_service.*`
  - `qr_service.*`
- keep `main.cpp` as composition and bootstrap only

Why first:

- otherwise every feature will become tightly coupled to the boot file
- this is the main maintainability risk in the current codebase

### 3. Introduce a configuration/persistence layer

Current issue:

- factory reset currently deletes `/wifi.json` directly
- the spec now requires more than Wi-Fi credentials to be reset
- there is no explicit model for persisted settings

Recommendation:

- create a small config service for:
  - Wi-Fi credentials ownership
  - app/web authentication settings
  - runtime preferences such as volume, last mode, or future battery options
- make factory reset operate on that service instead of on raw filenames

Why first:

- once web auth and app settings arrive, direct file deletion will become
  fragile

### 4. Decide the long-term audio service boundary

Current issue:

- the audio path is now much better than before, but it is still only a thin
  “play this file” wrapper around `ESP32-audioI2S`
- there is no concept of:
  - playback state
  - current album
  - current track
  - pause/resume
  - next/previous
  - completion handling

Recommendation:

- keep the current codec bring-up and queued clip playback
- add a higher-level `media_service` on top of it instead of adding album logic
  into `audio_driver.cpp`
- reserve `audio_driver` for hardware/codec/output concerns only

Why first:

- the spec is mostly about album playback behavior, not about raw codec output

## Recommended Implementation Order

The safest path is not to implement features in spec order, but in dependency
order.

## Phase 1: Stabilize Runtime Architecture

Goal:

- convert the firmware from bring-up code into a small service-oriented app

Work:

- introduce a canonical app state enum and transition rules
- introduce a separate Wi-Fi mode model:
  - disabled
  - enabling/connecting
  - connected
  - portal active
- extract `button_controller`
- extract `led_controller`
- extract `wifi_service`
- make `main.cpp` boot and wire services only

Definition of done:

- `main.cpp` is mostly setup and service wiring
- no LED logic or button task logic remains inline in `main.cpp`
- state transitions are explicit and centralized

## Phase 2: Build the Media Domain

Goal:

- create the album/track model required by the spec

Work:

- add SD-card mounting and error handling
- define album discovery from root directories like `001`, `002`, `003`
- define supported audio-file filtering and alphabetical ordering
- implement `media_service` with:
  - load album
  - play
  - pause
  - resume
  - next track
  - previous track / restart current track
  - playback finished callback
- keep UI sounds as a separate queue or playback class within the same service

Definition of done:

- playback is no longer tied to `test.mp3`
- the firmware can play an album from the SD card deterministically

## Phase 3: Replace Diagnostic Button Behavior With Product Behavior

Goal:

- align button behavior with the specification

Work:

- remove `KEY3 -> test.mp3` from normal runtime
- implement per-state button mappings:
  - volume down/up
  - play/pause
  - previous/restart
  - next
  - stop to QR scan
- define long-press timing and debounce in one place
- keep factory reset as a global/boot-only action

Definition of done:

- button semantics depend on app state, not on isolated button tasks
- there is one place in code that defines button policy

## Phase 4: Add QR Scan Pipeline

Goal:

- connect camera scanning to album playback

Work:

- choose and integrate the camera/QR stack
- isolate it behind `qr_service`
- define scan session lifecycle:
  - start scanning
  - decode candidate
  - validate payload
  - hand off album ID
  - ignore duplicates or restart same album intentionally
- connect scan timeout to `Idle`
- make `Idle` the state where camera activity stops while the rest of the
  system remains available

Definition of done:

- scanning a valid `file://001`-style code starts album playback
- invalid or missing albums produce consistent feedback

## Phase 5: Separate Provisioning From the Real App Server

Goal:

- move from “Wi-Fi manager only” to the two-mode web story in the spec

Work:

- keep `AyresWiFiManager` only for provisioning
- add a separate app server for normal operation
- make Wi-Fi disabled by default at boot
- make `BOOT` the Wi-Fi toggle:
  - if credentials exist: enable background STA + app server
  - if credentials do not exist: enter dedicated portal mode
  - if Wi-Fi is already active: shut web/AP down cleanly
- serve the normal app UI on `/` only when connected to STA Wi-Fi
- protect the normal app UI with auth
- define which file-management operations are in scope for the first version

Definition of done:

- provisioning and normal app serving are separate code paths
- the spec’s web story matches the implementation architecture

## Phase 6: Sleep, Power, and Recovery Behavior

Goal:

- implement the non-happy-path behavior that makes the device feel finished

Work:

- define `QrScan -> Idle` timeout and `Idle -> Sleep` timeout
- make sure Wi-Fi-enabled operation suppresses deep sleep while still allowing
  the device to enter `Idle`
- implement wake behavior explicitly
- define playback error sounds and empty-album behavior
- expand factory reset to all persisted config, not just Wi-Fi

Definition of done:

- `Idle` and `Sleep` are both real states with different responsibilities
- recovery behavior is predictable

## Phase 7: Polish and Tooling

Goal:

- reduce regression risk once the system grows

Work:

- add lightweight host-side tests for:
  - QR payload parsing
  - album sorting/filtering
  - config serialization
  - state transition rules
- add development-only diagnostics:
  - optional audio self-test
  - optional SD scan diagnostics
  - optional QR test mode
- document operational constraints in the firmware README

Definition of done:

- core business logic can be tested without hardware
- hardware-specific issues are easier to isolate

## Specific Recommendations For The Current Code

### Replace direct `AyresWiFiManager` ownership in `main.cpp`

Current risk:

- app behavior is already coupled to one library’s state API

Recommendation:

- wrap it in a `wifi_service`
- keep all `awm.*` calls in one module

### Keep `io_expander` as the only owner of TCA9555

Current status:

- this is already heading in the right direction

Recommendation:

- continue routing all buttons and expander-controlled outputs through it
- extend it before adding more EXIO users

### Keep `audio_driver` narrow

Current status:

- good direction: codec init and queued file playback are centralized

Recommendation:

- do not put album rules, button semantics, or state transitions into
  `audio_driver`
- treat it as hardware/audio-output plumbing only

### Retire bring-up-only behaviors deliberately

Examples:

- `KEY3` playing `test.mp3`
- boot-time diagnostic sounds used as readiness checks
- direct file removal for reset

Recommendation:

- keep these only behind a diagnostic mode or remove them once the real media
  path exists

## What I Would Not Do Yet

- battery features
- mixed audio playback
- aggressive performance optimization
- large web UI scope before the app state and media model are stable

## Suggested First Milestone

If only one milestone should be tackled next, I would do this:

1. Introduce a canonical app state model.
2. Split `main.cpp` into app/button/LED/Wi-Fi modules.
3. Add a config service and proper factory-reset ownership.
4. Add an SD-backed media service.

After that, the rest of the specification becomes much easier to implement
incrementally instead of by repeated rewrites.
