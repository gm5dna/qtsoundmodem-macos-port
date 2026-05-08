# QtSoundModem — macOS port

An unofficial, AI-assisted macOS port of [g8bpq/QtSoundModem](https://github.com/g8bpq/QtSoundModem).

A personal project: I'm a radio amateur (gm5dna) who wanted QtSoundModem
on Apple Silicon. The Qt 6 audio backend, the libhidapi PTT shim, and
the macOS plumbing in `MacBits.c` / `MacPermissions.mm` were written
with heavy assistance from Claude. Upstream ships Linux/Windows binaries
only and does not accept pull requests, so this is a downstream fork.

## Branches

- `master` mirrors upstream verbatim.
- `macos-port` is the patched series — build from this branch.
- Other named branches carry small UX enhancements on top.

## Tested on

macOS 26.4.1 (Tahoe), Apple Silicon, Qt 6.11 from Homebrew.

AX.25 1200 baud AFSK and BPSK 300 IL2P (+CRC) have both been exercised
extensively through BlackHole loopback and the `--decode-wav` harness
against a small reference corpus (WA8LMF track 2, a G3RUH 9600 capture,
an IL2P 300 baud capture). Real-RF testing is limited to my own station.

## What appears to work

- AX.25 1200 baud AFSK — the path I care about most and have tested hardest.
- BPSK 300 IL2P (with and without the CRC variant) — also exercised
  extensively against the reference corpus.
- ARDOP, RSID, and other upstream modes — preserved verbatim, compiled
  and linked but not seriously exercised on macOS.
- Qt 6 Multimedia audio backend (`SoundMode = 5`) — the only audio path
  on macOS. ALSA / Pulse / OSS / Waveout are not built.
- CM108-style PTT via `libhidapi`; serial PTT (RTS/DTR) via a small
  termios shim; CAT PTT via the on-screen string fields.
- Hot-replug of audio devices.

## Install (Homebrew)

The easiest route, and probably the preferred one, is the formula in
my [amateur-radio tap](https://github.com/gm5dna/homebrew-amateur-radio):

```sh
brew install gm5dna/amateur-radio/qtsoundmodem
```

The formula tracks `macos-port`, pulls in `qt`, `fftw`, `hidapi`, and
`cmake` automatically, builds the bundle, and installs the `.app` into
the Homebrew prefix. Microphone permission is requested via
`NSMicrophoneUsageDescription` on first launch; if denied, the modem
silently reads zero samples.

## Build from source

```sh
brew install qt fftw hidapi cmake
git clone https://github.com/gm5dna/QtSoundModem.git
cd QtSoundModem
git checkout macos-port
cmake -S . -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
open build/QtSoundModem.app
```

The build runs `macdeployqt`, embeds `libhidapi` with `install_name_tool`,
and ad-hoc `codesign --deep`s the bundle so it is self-contained and
launchable. It is **not** Developer-ID signed and **not** notarised —
Gatekeeper needs a right-click → Open on first launch.

## Runtime files

- Config + tracelog: `~/Library/Application Support/gm5dna/QtSoundModem/`
- The app `chdir`s into that directory at launch so upstream's
  relative-path settings calls land in the right place.

## Notable fixes on this fork

Most macos-port commits are platform glue, but two recent commits fix
genuine upstream bugs that affect every platform:

- **PTT On/Off String now auto-detects ASCII vs hex** (commit
  [`99716e3`](https://github.com/gm5dna/QtSoundModem/commit/99716e3)).
  Upstream parses these fields as hex digit pairs only, so typing the
  obvious Yaesu CAT command `TX1;` silently produced `0xF1 0x1B` with
  no terminator. Fields now accept literal ASCII (`TX1;`) or hex
  (`5458313B`, `FE FE 48 E0 1C 00 01 FD`) and log the chosen
  interpretation. Backwards-compatible.
- **Buffer overflow in AGW/frame monitor on long IL2P frames** (commit
  [`45ddc4e`](https://github.com/gm5dna/QtSoundModem/commit/45ddc4e)).
  `AGW_frame_monitor` and `frame_monitor` declared 512-byte buffers
  while IL2P payloads carry up to 1023 bytes, producing a FORTIFY
  abort on the IL2P TX path. Buffers resized, all `sprintf`s converted
  to bounded `snprintf`.

## Known limitations

- **Audio devices that don't offer a clean modem-compatible format are
  refused with a dialog** rather than silently producing garbled audio.
  Input must be Int16 PCM at 12 / 24 / 48 / 96 kHz; output must be
  Int16 at 12 kHz (no TX resampler yet). Float-format devices and
  44.1 / 88.2 kHz are rejected. No polyphase resampler — out of scope.
- **No "Setup Font" menu item.** Qt 6.11's `QFontDialog` crashes inside
  QtWidgets on macOS 26 (null deref in font-family enumeration). The
  app defaults to the macOS system UI font; edit `FontFamily` /
  `PointSize` / `Weight` in `QtSoundModem.ini` to override.
- Anything not listed under "What appears to work" should be assumed
  untested on macOS.

## Origin

Forked at upstream `9cd2735` (0.0.0.76). The macOS-specific work is a
small patch series on top: the first commit strips Windows-only
artefacts, the rest add the Qt audio backend, libhidapi PTT, and the
`MacBits.c` / `MacPermissions.mm` shims. Follow-up commits are
single-purpose fixes.

## Issues

Personal maintenance fork, no support guarantee. macOS-side bugs are
welcome on the [tracker](https://github.com/gm5dna/QtSoundModem/issues);
upstream bugs should go to g8bpq directly.

## Licence

GPLv3, same as upstream. See per-source-file headers.
