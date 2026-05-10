# QtSoundModem — macOS port

An unofficial, AI-assisted macOS port of [g8bpq/QtSoundModem](https://github.com/g8bpq/QtSoundModem).

This is a personal project. I'm a radio amateur (gm5dna) who wanted
QtSoundModem on Apple Silicon, so I ported it for my own use and am
sharing it in case it helps others. **No guarantees of any kind** — it
works for me on my hardware, with the modes I actually use; everything
else is on a best-effort basis. The Qt 6 audio backend, the libhidapi
PTT shim, and the macOS plumbing (`MacBits.c`, `MacPermissions.mm`)
were written with heavy assistance from Claude. Upstream ships
Linux/Windows binaries only and does not accept pull requests, so this
is a downstream fork.

See [CHANGELOG.md](CHANGELOG.md) for the per-release delta from upstream.

## Install (Homebrew)

Easiest route — the formula in my
[amateur-radio tap](https://github.com/gm5dna/homebrew-amateur-radio)
tracks the `macos-port` branch:

```sh
brew install gm5dna/amateur-radio/qtsoundmodem
```

The formula pulls in `qt`, `fftw`, `hidapi`, and `cmake`, builds the
bundle, and installs the `.app` into the Homebrew prefix. Microphone
permission is requested on first launch.

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

The bundle is ad-hoc codesigned — not Developer-ID signed, not
notarised. Gatekeeper needs a right-click → Open on first launch.

## Branches

- `master` mirrors upstream verbatim.
- `macos-port` is the patched series — build from this branch.
- Other named branches carry small UX enhancements on top.

## What works for me

Tested on macOS 26.4.1 (Tahoe), Apple Silicon, Qt 6.11 from Homebrew.

- AX.25 1200 baud AFSK — the path I use and have tested hardest.
- BPSK 300 IL2P (with and without CRC) — exercised against a small
  reference corpus.
- ARDOP, RSID, and other upstream modes — compiled and linked but not
  seriously exercised on macOS.
- CM108-style PTT (libhidapi), serial PTT (RTS/DTR), and CAT PTT.

Audio input must be Int16 PCM at 12 / 24 / 48 / 96 kHz; output is
Int16 at 12 kHz. Devices that don't offer a compatible format are
refused with a dialog rather than producing garbled audio.

## Issues

Personal maintenance fork, no support guarantee. macOS-side bugs are
welcome on the [tracker](https://github.com/gm5dna/QtSoundModem/issues);
upstream bugs should go to g8bpq directly.

## Licence

GPLv3, same as upstream. See per-source-file headers.
