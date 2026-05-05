# QtSoundModem — macOS port

An unofficial, AI-assisted macOS port of [g8bpq/QtSoundModem](https://github.com/g8bpq/QtSoundModem).

A fair warning up front: this is mostly a personal project. I'm a radio
amateur (gm5dna) who wanted QtSoundModem on Apple Silicon, and most of
the porting work — the Qt 6 audio backend, the libhidapi PTT shim, the
macOS plumbing in `MacBits.c` / `MacPermissions.mm` — was done with
heavy assistance from Claude. I've tried to read every diff, test the
results, and not commit anything I don't understand, but I'm not a Qt
or DSP expert and there will be rough edges I haven't spotted. Treat
it accordingly.

Upstream ships Linux and Windows binaries only and has stated they
don't accept pull requests, so this is a downstream fork rather than a
contribution path.

## Branch model

- `master` mirrors upstream verbatim.
- `macos-port` is the patched series. Build from this branch.
- Other named branches may carry small UX enhancements on top of
  `macos-port`; the port itself stays as close to upstream as possible.

## Tested on

- macOS 26.4.1 (Tahoe), Apple Silicon (M-series).
- Qt 6.11 from Homebrew.

Layer-2 AX.25 1200 baud AFSK decoding has been exercised through
BlackHole loopback and the `--decode-wav` harness against a small
reference corpus (WA8LMF track 2, a G3RUH 9600 capture, and an IL2P
300 baud capture). Real-RF testing is ongoing and limited to my own
station — I can't claim broad compatibility.

## What appears to work

- AX.25 1200 baud AFSK — the path I care about most and have tested
  hardest.
- ARDOP, IL2P, RSID — code paths preserved verbatim from upstream.
  Compiled and linked, but I haven't seriously exercised them on
  macOS; they may or may not behave.
- Qt 6 Multimedia audio backend (`SoundMode = 5`) — the only audio
  path on macOS. ALSA / Pulse / OSS / Waveout backends are not built.
- CM108-style PTT via `libhidapi`.
- Serial PTT (RTS/DTR) via a small POSIX termios shim.
- Hot-replug of audio devices (re-binds when a device with the same
  name reappears).

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

The build runs `macdeployqt`, uses `install_name_tool` to embed
`libhidapi`, and finishes with an ad-hoc `codesign --deep` so the
bundle is self-contained and launchable. The bundle is **not**
Developer-ID signed and **not** notarised — this is for local use.
Gatekeeper will need a right-click → Open on first launch.

Microphone permission is requested via `NSMicrophoneUsageDescription`
in the bundle's `Info.plist`. If you deny it, the modem will silently
read zero samples and look like it isn't working.

## Where things live at runtime

- Config: `~/Library/Application Support/gm5dna/QtSoundModem/QtSoundModem.ini`
- Logs / tracelog: same directory.
- The app `chdir`s into that directory at launch so the upstream
  relative-path settings calls land in the right place.

## Known limitations

- **44.1 / 96 kHz audio devices** fall back to truncated integer
  decimation, so the modem's effective rate isn't quite 12 kHz and
  timing drifts. `initializeAudioIn` asks CoreAudio for 48 / 24 /
  12 kHz first and only falls back if the device refuses all of
  those. Decoding still happens; quality won't be great.

- **No "Setup Font" menu item.** Qt 6.11's `QFontDialog` crashes
  inside QtWidgets on macOS 26 (null deref in font-family
  enumeration), so the menu entry has been removed. The app
  defaults to the macOS system UI font when `QtSoundModem.ini` has
  no saved `FontFamily`. To change the font manually, edit
  `FontFamily` / `PointSize` / `Weight` in the .ini directly. I'll
  revisit if upstream Qt fixes the dialog.

- **Anything I haven't listed under "What appears to work"** should
  be assumed untested on macOS until proven otherwise.

## Origin

Forked at upstream commit `9cd2735` (0.0.0.76). The macOS-specific
work is a small patch series on top of that point: the first commit
on `macos-port` strips Windows-only artefacts, and the rest add the
Qt audio backend, libhidapi PTT, and the `MacBits.c` /
`MacPermissions.mm` shims. Follow-up commits are single-purpose
fixes.

## Issues

This is a personal maintenance fork with no support guarantee. If
something is broken on the macOS side, an issue on the
[tracker](https://github.com/gm5dna/QtSoundModem/issues) is welcome
and I'll look when I can — but I may not have time, and I may not
know the answer. Bugs that exist in upstream too should go to g8bpq
directly.

## Licence

GPLv3 — same as upstream. See the per-source-file headers.
