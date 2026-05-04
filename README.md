# QtSoundModem — macOS port

A maintained downstream fork of [g8bpq/QtSoundModem](https://github.com/g8bpq/QtSoundModem)
that builds and runs on Apple Silicon. Upstream ships Linux and Windows
binaries only; this branch fills the macOS gap. There is no separate
upstream macOS effort — upstream has stated they don't accept pull
requests, so this is a permanent fork rather than a contribution path.

Branch model:

- `master` mirrors upstream verbatim.
- `macos-port` is the patched series. Build from this branch.

Tested on macOS 26.4 (Sequoia / Tahoe), Apple Silicon, Qt 6.10 from
Homebrew. Layer-2 AX.25 decoding has been validated end-to-end through
BlackHole loopback and the `--decode-wav` harness. Real-RF testing is
in progress.

## What works

- AX.25 1200 baud AFSK (the historical bread-and-butter path)
- ARDOP, IL2P, RSID — code paths preserved verbatim from upstream
- Qt 6 Multimedia audio backend (`SoundMode = 5`) — the only audio
  path on macOS; ALSA / Pulse / OSS / Waveout backends are not built.
- CM108-style PTT via `libhidapi`
- Serial PTT (RTS/DTR) via POSIX termios shim
- Hot-replug of audio devices (re-binds automatically on same-name reappearance)

## Build

```sh
brew install qt fftw hidapi cmake
git clone https://github.com/gm5dna/QtSoundModem.git
cd QtSoundModem
git checkout macos-port
cmake -S . -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
open build/QtSoundModem.app
```

The build chain runs `macdeployqt`, `install_name_tool` to embed
`libhidapi`, and a final ad-hoc `codesign --deep` so the bundle is
self-contained and launchable. The bundle is **not** Developer-ID
signed and is **not** notarised — local use only. Gatekeeper will
require a right-click → Open on first launch.

Microphone permission is requested via `NSMicrophoneUsageDescription`
in the bundle's `Info.plist`; deny it and the modem will silently
read zero samples.

## Where things live at runtime

- Config: `~/Library/Application Support/gm5dna/QtSoundModem/QtSoundModem.ini`
- Logs / Tracelog: same directory.
- The app `chdir`s into that directory at launch so all the upstream
  relative-path settings calls land in the right place.

## Known limitations

- The downsampling filter in `PollQSound` is a 4-tap boxcar moving
  average, sufficient for 1200 baud AFSK but not a proper FIR
  antialias — RUH48 / RUH96 modes are likely affected. See the
  comment at `QtSoundModem.cpp:PollQSound`.
- A narrow race exists if a TX device is hot-unplugged mid-transmit;
  the upstream TX path lacks the mutex coverage needed to fix it
  cleanly. Stop transmit before unplugging.
- Audio device persistence is by description string (CoreAudio
  display name), not stable `id()`. Two devices with identical
  names won't be distinguishable; rename one if it matters.

## Origin

Forked at upstream commit `9cd2735` (0.0.0.76). All macOS-specific
work lives in the six-commit patch series that diverges at that point.
The first commit on `macos-port` strips Windows-only artefacts; the
last lands the Qt-audio backend, libhidapi PTT, and `MacBits.c` /
`MacPermissions.mm` shims. Subsequent fixes are single-purpose
follow-up commits.

## Issues

This is a personal maintenance fork. Bug reports specific to the
macOS port are welcome on the [issue tracker](https://github.com/gm5dna/QtSoundModem/issues);
upstream-affecting bugs should go to g8bpq directly. There is no
support guarantee.

## Licence

GPLv3 — same as upstream. See the per-source-file headers.
