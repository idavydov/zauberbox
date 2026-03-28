# Zauberbox Firmware Plan

This plan is based on the current firmware state and the current
[`SPECIFICATION.md`](./SPECIFICATION.md).

The current codebase is still a bring-up style application:

- a first controller split now exists:
  [`main.cpp`](./firmware/src/main.cpp),
  [`app_controller.cpp`](./firmware/src/app_controller.cpp),
  [`app_state.cpp`](./firmware/src/app_state.cpp),
  [`button_controller.cpp`](./firmware/src/button_controller.cpp),
  [`led_controller.cpp`](./firmware/src/led_controller.cpp),
  [`config_service.cpp`](./firmware/src/config_service.cpp),
  [`wifi_service.cpp`](./firmware/src/wifi_service.cpp),
  [`media_service.cpp`](./firmware/src/media_service.cpp),
  [`qr_service.cpp`](./firmware/src/qr_service.cpp),
  [`audio_driver.cpp`](./firmware/src/audio_driver.cpp), and
  [`io_expander.cpp`](./firmware/src/io_expander.cpp)
- an initial SD-card media engine and album-playback path now exist
- a first OV5640 QR scanner/camera pipeline now exists
- a canonical app-state store now exists, and `main.cpp` is now effectively
  bootstrap/composition only
- Wi-Fi provisioning exists, but the normal app server described in the spec
  does not
- button behavior is still incomplete relative to the product spec

The goal should be a gradual transition from bring-up code to a small set of
explicit services with a state-driven application layer.

## Immediate Technical Debt To Address First

These are the changes I would do before adding major new features.

### 1. Introduce a real application state model

Status:

- completed as the first architectural step
- the firmware now has a canonical `AppState` model plus a separate `WifiMode`
  overlay, wrapped in a dedicated state-store class
- LED behavior, factory-reset gating, and Wi-Fi synchronization now consume
  that model instead of the old LED-only enum

Remaining gap:

- buttons, media behavior, and camera behavior are now service-owned, but some
  state-driven product policy is still incomplete:
  - non-playback button behavior
  - scan timeout and duplicate-scan policy
  - Wi-Fi toggle and app-server behavior

Previous issue:

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

Status:

- mostly completed for the current runtime scope
- `app_controller` now owns the remaining boot/app orchestration that used to
  live in `main.cpp`
- `button_controller`, `led_controller`, and `wifi_service` now exist and own
  their respective tasks/integration points
- `main.cpp` is now effectively composition/bootstrap only

Remaining gap:

- media and QR services now exist, but the remaining work is still split across
  product-policy gaps rather than missing controller boundaries

Previous issue:

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

Status:

- completed for the current runtime scope
- a dedicated `config_service` now owns persisted config file paths
- factory reset now runs through that service instead of deleting
  `/wifi.json` directly
- Wi-Fi credential presence checks now also go through that service

Remaining gap:

- persisted settings are not yet modeled as typed application config objects
- web authentication and runtime preferences are only represented as owned
  reset targets, not as first-class settings APIs

Previous issue:

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

Status:

- completed for the current runtime scope
- a dedicated `media_service` now exists above `audio_driver`
- `audio_driver` now owns generic queued file playback instead of named product
  sounds
- `media_service` now owns UI sounds, SD-card mounting, album discovery, and
  basic album track sequencing

Remaining gap:

- the new media service is not yet wired to QR scans or product button policy
- SD-card mounting still needs hardware validation and any board-specific pin
  overrides that may be required on the target device
- playback conflict policy for UI sounds versus album playback is still only a
  first implementation, not a finalized product rule

Previous issue:

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

- done: introduce a canonical app state enum and transition rules
- done: introduce a separate Wi-Fi mode model:
  - disabled
  - enabling/connecting
  - connected
  - portal active
- done: extract `button_controller`
- done: extract `led_controller`
- done: extract `wifi_service`
- done: move remaining boot/app orchestration out of `main.cpp`
- done: make `main.cpp` composition and bootstrap only
- done: introduce `config_service` and route factory reset/config checks
  through it

Definition of done:

- `main.cpp` is mostly setup and service wiring
- no LED logic or button task logic remains inline in `main.cpp`
- state transitions are explicit and centralized
- persisted config file ownership is no longer spread across runtime code

## Phase 2: Build the Media Domain

Goal:

- create the album/track model required by the spec

Work:

- done: introduce `media_service` above `audio_driver`
- done: move UI sound ownership out of `audio_driver`
- done: add initial SD-card mount path and error handling
- done: define supported audio-file filtering and alphabetical ordering
- done: add basic album sequencing:
  - load album
  - play first track
  - advance on track completion
  - pause/resume
  - next track
  - previous/restart current track
- validate and, if necessary, correct SD-card mount configuration on target
  hardware
- done: connect album loading to QR-scan handoff
- connect transport controls to the product button policy
- finalize the playback conflict rule for queued UI sounds versus album content
- add explicit handling for missing albums and empty albums as product-level
  error outcomes, not just service-level failures

Definition of done:

- playback is no longer tied to any bring-up-only file
- the firmware can play an album from the SD card deterministically

## Phase 3: Replace Diagnostic Button Behavior With Product Behavior

Goal:

- align button behavior with the specification

Work:

- done: remove `KEY3 -> test.mp3` from normal runtime
- done: define long-press timing and debounce in one place
- done: keep factory reset as a global/boot-only action
- done: move physical button scanning out of isolated one-off logic and into a
  dedicated button event source
- done: implement playback-state button mappings:
  - volume down/up
  - play/pause
  - previous/restart
  - next
  - stop to QR scan
- connect non-playback button behavior once QR and Wi-Fi toggle flows exist

Definition of done:

- button semantics depend on app state, not on isolated button tasks
- there is one place in code that defines button policy

## Phase 4: Add QR Scan Pipeline

Goal:

- connect camera scanning to album playback

Work:

- done: isolate camera ownership behind `qr_service`
- done: integrate the OV5640 board bring-up path:
  - EXIO camera power control
  - EXIO camera pin routing
  - `esp_camera` init/deinit for scan mode
- done: add QR payload parsing and album handoff for `file://NNN` payloads
- done: add a first decode backend using vendored `quirc` via
  `ESP32QRCodeReader`
- continue defining scan session lifecycle:
  - start scanning
  - done: decode candidate
  - done: validate payload
  - done: hand off album ID to media playback
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

1. Add a config service and proper factory-reset ownership.
2. Add an SD-backed media service.
3. Implement the product button policy on top of that media model.
4. Add the QR scan pipeline on top of that state/media structure.

After that, the rest of the specification becomes much easier to implement
incrementally instead of by repeated rewrites.
