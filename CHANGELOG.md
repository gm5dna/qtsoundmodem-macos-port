# Changelog

All notable changes to this fork are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

This fork starts at `mac-0.1.0`. Pre-fork history (upstream tags `0.64`–`0.76`, `24.45`) lives in [g8bpq/QtSoundModem](https://github.com/g8bpq/QtSoundModem); this file does not duplicate it.

## [Unreleased]

## [mac-0.1.1] – 2026-05-17

Consolidates the rx/tx audit fixes and the tooltips / help-menu work onto `macos-port`. Twelve correctness and robustness fixes — four of which also affect upstream — plus in-app help.

### Added

- Help menu: User Guide, G8BPQ online, "What's This?", and About.
- Hover tooltips throughout: main-window controls (modem mode, centre, RX offset, DCD, audio, waterfall), the Devices / Modem (all tabs) / Calibration dialogs with `?` help buttons, and the Settings / View / Tools menu actions; CWID and modem-dialog filter coverage.

### Changed

- Documented 300-baud tuning guidance — raise `txbpf` and `TXTail`, and defer `pnt_change`.

### Fixed

- **(also affects upstream)** IL2P: `encoded[]` was not sized for the +CRC suffix, overflowing on maximum-length frames.
- **(also affects upstream)** IL2P: Rx CRC state-machine bit-mask now matches the validator (`& 1`).
- **(also affects upstream)** BPF: `init_BPF` off-by-one read past the last filter tap.
- **(also affects upstream)** AGW / KISS: `all_frame_buf` add/delete is now locked against the modem worker thread, closing a data race.
- Audio: surface `QAudioSource::start()` failure instead of silently leaving Rx off.
- Audio: log `QAudioSource` `StoppedState` errors instead of dying silently.
- Audio: drop PTT and exit `sendSamplestoQSound` when `QAudioSink` reports an error.
- Audio: cap the `SoundFlush` `IdleState` wait at 1 s to bound dead-air after PTT.
- Audio: honour `useTimedPTT` / `txLatency` on the macOS Qt path.
- Audio: anti-alias the 48 kHz → 12 kHz decimation step so co-running FSK channels no longer alias.
- Audio: carry sub-frame tails and reset capture state on device swap.
- Audio: flush the FIR decimator history on every capture (re)open.

## [mac-0.1.0] – 2026-05-10

First tagged release of the macOS port. Builds and runs natively on Apple Silicon (macOS 14+), uses Qt 6 Multimedia for audio and libhidapi for CM108 PTT, and ships with a curated set of fixes — three of which also affect upstream.

### Added

- CMake build with platform-conditional sources, replacing the upstream Qt 5 `.pro` flow.
- arm64 / Apple Silicon support: POSIX serial / PTT shims (`MacBits.c`), `__ARM_ARCH` guards, native build on macOS 14.
- macOS bundle plumbing: ad-hoc codesigning the `.app` as a bundle, custom `.icns` icon, libfftw3f bundled into the `.app` for self-containment.
- `chdir` to `~/Library/Application Support/QtSoundModem` on launch; lazy-init global `QSettings` so config honours the new working directory.
- Native-rate audio capture with windowed-sinc FIR decimation to 12 kHz (replaces the older boxcar decimator).
- `--decode-wav` and `--decode-wav-native` offline decode harnesses, including a baseline-locked test corpus for regression checks.
- CoreAudio nominal-rate retune shim with `AutoRetuneSampleRate` INI flag — opportunistically aligns shared RX/TX devices to a 12 kHz multiple at the Devices-dialog accept, on hot-plug, and at startup; gated to avoid retuning while a stream is live.
- TX Audio / RX Audio gain sliders.
- View menu: PSK Constellation show/hide toggle.
- Tools submenu grouping Calibration and Restart Waterfall.
- Default CM108 VID/PID on the macOS hidapi path.
- README for the macos-port branch, with install / build / test notes; Homebrew tap documented as the preferred install route.

### Changed

- Migrated Qt Multimedia from Qt 5 to Qt 6; macOS default backend is now Qt audio + libhidapi.
- Bumped macOS deployment target from 11.0 to 14.0.
- Auto-track macOS Light/Dark appearance (replaces the upstream manual Dark Theme toggle).
- UI cleanup: widened the Devices dialog PTT Port, modem-option, and 6Pack Serial Port combos so labels are no longer clipped; re-flowed the Devices dialog rows for legibility; positioned DCD indicators against actual row geometry.
- Audio device persistence: persist by stable `QAudioDevice::id()` with description fallback; index reopen against the filtered device list; queued `audioInputsChanged` / `audioOutputsChanged` signals; serialised teardown vs. worker-thread TX/RX with a `QMutex`; closeQSound now tears streams down on device switch.
- Audio pipeline robustness: refuse non-decimator-friendly device rates and formats; bypass FIR decimation when `using48000` is set; skip the SoundFlush drain wait when the output sink is closed; broadcast mono input to stereo before decimation; dump input samples before `ProcessNewSamples` rather than after; PollQSound drains unconditionally and dumps post-decimation samples.
- `dw9600`: scale `lp_filter_size` by upsample for polyphase interpolation.
- CLI: canonicalise `--decode-wav` / `--dump-input` paths against startup cwd; fail loudly on `canonicaliseCliPath` errors.
- Forced `NoRole` on Settings / View menu actions so macOS doesn't relocate them.
- RX offset control replaced with a spinbox so exact values (including 0) are reachable.
- Stopped centring the window in macOS fullscreen; the cluster now hugs the right edge.
- Suppressed "Minimize to Tray" on macOS.
- Dropped the redundant first-run microphone-permission dialog on macOS.

### Fixed

- **(also affects upstream)** Buffer overflow in AGW / frame monitor on long IL2P frames.
- **(also affects upstream)** PTT On/Off String parser silently treated values as hex; now auto-detects ASCII vs. hex so both forms work.
- **(also affects upstream)** AGW TX path and port-list use raw channel index, matching RX.
- Guarded against negative `QIODevice::read` return in PollQSound.
- Tightened the `__ARM_ARCH` guard in `LinuxBits.c` so Apple Silicon builds compile cleanly.

### Removed

- `QFontDialog` font picker; the app defaults to the system font.
- Manual Dark Theme toggle (replaced by automatic Light/Dark tracking).
- Broken "Restart Waterfall" menu item.
- Windows-only build artefacts and source backups.

[Unreleased]: https://github.com/gm5dna/qtsoundmodem-macos-port/compare/mac-0.1.1...HEAD
[mac-0.1.1]: https://github.com/gm5dna/qtsoundmodem-macos-port/compare/mac-0.1.0...mac-0.1.1
[mac-0.1.0]: https://github.com/gm5dna/qtsoundmodem-macos-port/releases/tag/mac-0.1.0
