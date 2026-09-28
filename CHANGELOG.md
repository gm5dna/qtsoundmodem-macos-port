# Changelog

All notable changes to this fork are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

This fork starts at `mac-0.1.0`. Pre-fork history (upstream tags `0.64`–`0.76`, `24.45`) lives in [g8bpq/QtSoundModem](https://github.com/g8bpq/QtSoundModem); this file does not duplicate it.

## [Unreleased]

### Fixed

- RSID: sliding-window shift used index 1024 instead of 512 and overran its buffer on 48 kHz input (also upstream).
- KISS: frames queued by KISS clients were added to the TX queue without the frame-buffer lock (also upstream).
- Over-long GUI or INI strings (Hamlib/FLRig host, PTT strings, digipeater calls, UDP host and others) overflowed fixed buffers; now truncated (also upstream).
- ARDOP: a second ARDOP channel, or a RUH channel after one, got a fraction of each 48 kHz chunk (also upstream).
- RUH selected on an input device not at 48 kHz silenced every other channel; they now decode, and the log says RUH needs a 48 kHz device.
- A crash path when TX started from inside RX processing (`txSleep` re-entering `PollQSound`).
- A stuck or stopped output device could hold PTT on dead air for minutes on a long frame; the abort now covers the whole transmission.
- A failed or errored output device is detected and reopened automatically (up to three times) instead of leaving every later TX silent.
- Devices dialog: selecting between two same-named sound cards, or a device change while the dialog is open, could save and open the wrong device.
- Device changes during a CoreAudio rate retune were dropped.
- Races between the GUI and the modem worker on capture reopen, the anti-alias filter, TX completion and the audio gains.
- Changing a modem type in the main window now applies between chunks on the modem thread (Qt audio).
- Long IL2P monitor lines were cut at 1023 characters.
- `--dump-input` WAVs had zero-length headers; `--decode-wav` read past the data chunk.

### Changed

- Qt audio code moved from `QtSoundModem.cpp` into `QtAudio.cpp`.
- Title bar shows the port version (`0.0.0.76+mac-…`); bundle version follows the release tag.
- Audio gain sliders apply and save on release instead of on every step.
- CI builds and runs the decode regression on every push and pull request (macOS 26 runner); the corpus is the `corpus-v1` release asset.
- The regression runner uses a fixture INI instead of the user's own settings.

### Removed

- Unused files: qmake `.pro`/`.pri`, `makeit`, Windows `.rc` resources, `rsid.cxx`, `LinuxBits.c`, `fftw3.f`, the stale sample INI, a duplicate icon, and the committed release-planning docs.

## [mac-0.1.1] – 2026-05-17

Consolidates the rx/tx audit fixes and the tooltips / help-menu work onto `macos-port`. Twelve correctness and robustness fixes — four of which also affect upstream — plus in-app help.

### Added

- Help menu: G8BPQ documentation (online), "What's This?", and About.
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
- `chdir` to `~/Library/Application Support/gm5dna/QtSoundModem` on launch; lazy-init global `QSettings` so config honours the new working directory.
- Native-rate audio capture with windowed-sinc FIR decimation to 12 kHz (replaces the older boxcar decimator).
- `--decode-wav` and `--decode-wav-native` offline decode harnesses, including a baseline-locked test corpus for regression checks.
- CoreAudio nominal-rate retune shim with `AutoRetuneSampleRate` INI flag — opportunistically aligns shared RX/TX devices to a 12 kHz multiple at the Devices-dialog accept, on hot-plug, and at startup; gated to avoid retuning while a stream is live.
- TX Audio / RX Audio gain sliders.
- View menu: PSK Constellation show/hide toggle.
- Tools submenu for Calibration.
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
