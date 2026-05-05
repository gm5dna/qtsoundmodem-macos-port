#!/bin/bash
# Regression test rig for the QtSoundModem macOS port. Runs the
# --decode-wav harness against the reference corpus and asserts
# decoded-packet counts against thresholds in expected.txt. Returns
# 0 on pass, non-zero on regression.
#
# Reads the test corpus from CORPUS_DIR (default: a sibling of the
# repo root at ../test-files, where the WA8LMF FLAC and the two
# 5-min reference WAVs live — they're large binary files, kept out
# of the git repo).
#
# This script uses the user's existing QtSoundModem configuration
# at ~/Library/Application Support/gm5dna/QtSoundModem/QtSoundModem.ini
# — QStandardPaths::AppDataLocation uses getpwuid().pw_dir on macOS
# and ignores $HOME, so config isolation isn't reliably possible.
#
# To get non-zero decode counts you need at least one modem enabled
# with the right speed for each test file:
#   * 02_100-Mic-E-Bursts: AFSK 1200 (SPEED_1200, ModemType=1) on a
#     non-zero soundChannel.
#   * g3ruh_9600_5min:     RUH96 (SPEED_RUH96, ModemType=19).
#   * il2p_300_5min:       300-baud FSK (SPEED_300, ModemType=0)
#     with IL2P=1 in the matching AX25_<port> section.
#
# Usage: test-files/run-regression.sh
#   (or with explicit paths: BIN=/path/to/QtSoundModem
#                            CORPUS_DIR=/path/to/wavs run-regression.sh)
#
# To lock new baselines after a successful run, edit expected.txt
# and replace each min-count with the observed count (or a slightly
# lower number to allow for run-to-run jitter).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEFAULT_BIN="${REPO_ROOT}/build/QtSoundModem.app/Contents/MacOS/QtSoundModem"
BIN="${BIN:-${DEFAULT_BIN}}"
DEFAULT_CORPUS="${REPO_ROOT}/../test-files"
CORPUS_DIR="${CORPUS_DIR:-${DEFAULT_CORPUS}}"
if [ ! -d "${CORPUS_DIR}" ]; then
	echo "ERROR: corpus dir not found at ${CORPUS_DIR}" >&2
	echo "       Override with CORPUS_DIR=/path/to/test-files." >&2
	exit 2
fi

if [ ! -x "${BIN}" ]; then
	echo "ERROR: QtSoundModem binary not found at ${BIN}" >&2
	echo "       Build first: cmake --build ${REPO_ROOT}/QtSoundModem/build" >&2
	exit 2
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
	echo "ERROR: ffmpeg not found on PATH (needed to convert FLAC inputs)" >&2
	exit 2
fi

TMPDIR_REG=$(mktemp -d)
trap 'rm -rf "${TMPDIR_REG}"' EXIT

# Convert FLAC references to a 48 kHz canonical PCM WAV the harness
# can consume. Cached in $TMPDIR_REG, dropped on script exit.
flac_to_wav() {
	local src="$1"
	local dst="${TMPDIR_REG}/$(basename "${src%.flac}.wav")"
	if [ ! -f "${dst}" ]; then
		# `</dev/null` is critical — ffmpeg reads stdin opportunistically
		# even with -i set, which steals bytes from the outer
		# `while read` loop's stdin and corrupts subsequent filenames.
		ffmpeg -y -i "${src}" -map_metadata -1 -fflags +bitexact \
			-flags +bitexact -ar 48000 -c:a pcm_s16le "${dst}" \
			</dev/null >/dev/null 2>&1
	fi
	echo "${dst}"
}

run_one() {
	local file="$1"
	local mode="$2"          # "fir" or "native"
	local expected_min="$3"
	local src="${CORPUS_DIR}/${file}"

	if [ ! -f "${src}" ]; then
		printf "MISSING %-55s (%s)\n" "${file}" "${src}"
		return 1
	fi

	local input
	case "${file}" in
		*.flac) input="$(flac_to_wav "${src}")" ;;
		*)      input="${src}" ;;
	esac

	# Two harness modes:
	#   fir    → --decode-wav: decimateAudioToModem path (FIR antialias,
	#            12 kHz to BufferFull). Exercises the FIR. Won't decode
	#            RUH/G3RUH content because the dw9600 demod is
	#            hardwired to 48 kHz.
	#   native → --decode-wav-native: raw 48 kHz to BufferFull with
	#            using48000=1; BufferFull's internal 4-skip downsample
	#            handles FSK, and RUH gets the 48 kHz it needs.
	local flag
	case "${mode}" in
		fir)    flag="--decode-wav" ;;
		native) flag="--decode-wav-native" ;;
		*)      printf "FAIL  %-55s unknown mode '%s'\n" "${file}" "${mode}"; return 1 ;;
	esac

	# `< /dev/null` on the binary too — same stdin-stealing concern
	# as ffmpeg above.
	local count
	count=$("${BIN}" "${flag}" "${input}" </dev/null 2>&1 \
		| grep -c "^DECODED \[RX\]" || true)

	if [ "${count}" -ge "${expected_min}" ]; then
		printf "PASS  %-55s [%-6s] decoded %4d (>= %d)\n" "${file}" "${mode}" "${count}" "${expected_min}"
		return 0
	else
		printf "FAIL  %-55s [%-6s] decoded %4d (< %d)\n" "${file}" "${mode}" "${count}" "${expected_min}"
		return 1
	fi
}

fail=0
while IFS=$'\t' read -r file mode expected; do
	# Strip stray CR from CRLF-saved expected.txt files. Without this,
	# `expected` ends with \r and the integer comparison below fails
	# silently with "integer expression expected".
	file="${file%$'\r'}"; mode="${mode%$'\r'}"; expected="${expected%$'\r'}"
	# Skip blank lines and # comments.
	case "${file}" in ''|\#*) continue ;; esac
	run_one "${file}" "${mode}" "${expected}" || fail=1
done < "${SCRIPT_DIR}/expected.txt"

exit ${fail}
