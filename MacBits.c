/*
Copyright (C) 2019-2020 Andrei Kopanchuk UZ7HO
Copyright (C) 2024 gm5dna QtSoundModem macOS port contributors

This file is part of QtSoundModem

QtSoundModem is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

QtSoundModem is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with QtSoundModem.  If not, see http://www.gnu.org/licenses

*/

// macOS platform shims for QtSoundModem.
//
// Counterpart to Linux.c (UNIX AND NOT APPLE) and Waveout.c (Windows).
// Provides only the symbols the macOS build actually links against from
// QSM_COMMON_SOURCES once Linux.c / ALSASound.c / pulse.c / audio.c are
// excluded:
//
//   Debugprintf  - variadic logging, forwards to WriteDebugLog (defined
//                  in QtSoundModem.cpp:273, which routes to qDebug()).
//   platformInit - SIGINT/SIGTERM handlers that flip the global Closing
//                  flag, plus SIGPIPE/SIGHUP ignore. Mirrors Linux.c.
//   txSleep      - paces the Qt audio output buffer in
//                  sendSamplestoQSound(). Mirrors the Linux variant's
//                  drain-while-waiting pattern, calling PollQSound()
//                  (the Qt-input equivalent of PollReceivedSamples)
//                  to keep input from backlogging during long TX
//                  bursts. Both txSleep and PollQSound run on
//                  workerThread (tcpCode.cpp), not the GUI thread.
//   Sleep        - mS-granularity sleep primitive used by txSleep.
//   stricmp      - case-insensitive strcmp; macOS libc has strcasecmp
//                  but not the Microsoft-style stricmp the codebase
//                  calls.
//   memicmp      - case-insensitive memcmp.
//   OpenCOMPort, CloseCOMPort, WriteCOMBlock,
//   COMSetRTS, COMClearRTS, COMSetDTR, COMClearDTR
//                - POSIX termios serial port + TIOCM line-state ioctls.
//                  Used by SMMain.c's PTT path for Signalink-style
//                  RTS/DTR keying. macOS supports the same termios +
//                  TIOCMGET/TIOCMSET interface as Linux, so these are
//                  near-verbatim copies of Linux.c's implementations
//                  with EAGAIN/EWOULDBLOCK substituted for the magic
//                  errno values 11/35.
//   CaptureDevice, PlaybackDevice, CaptureNames, PlaybackNames,
//   CaptureCount, PlaybackCount
//                - Audio device-name storage. ALSASound.c / Waveout.c
//                  own these on Linux/Windows; on macOS the Qt path
//                  fills the arrays at runtime from QMediaDevices.
//
// Deliberately NOT provided here:
//   - GPIO functions (gpioInitialise, gpioWrite, gpioSetMode,
//     SetupGPIOPTT). The call sites in SMMain.c are guarded by the
//     same __ARM_ARCH-only macro that commit 3 tightened in
//     LinuxBits.c; this commit tightens those guards too, so the GPIO
//     symbols are not referenced from the macOS build.
//   - Audio entry points (InitSound, CloseSound, GetSoundDevices).
//     Commit 6 forces SoundMode == 5 on macOS and routes through the
//     existing Qt Multimedia path in QtSoundModem.cpp; whether stubs
//     or call-site re-routing are needed is settled there.

#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include "UZ7HOStuff.h"

extern int Closing;
void WriteDebugLog(char * Msg);
void PollQSound(void);

void Sleep(int mS)
{
	usleep(mS * 1000);
}

void txSleep(int mS)
{
	// Called while waiting for Qt output buffer space.
	// Drain the Qt input via PollQSound() so capture does not backlog
	// during long transmit bursts. Linux.c does the equivalent with
	// PollReceivedSamples() against the ALSA capture ring.

	if (mS < 0)
		return;

	while (mS > 50)
	{
		PollQSound();
		Sleep(50);
		mS -= 50;
	}

	Sleep(mS);
	PollQSound();
}

void Debugprintf(const char * format, ...)
{
	char Mess[10000];
	va_list arglist;

	va_start(arglist, format);
	vsnprintf(Mess, sizeof(Mess), format, arglist);
	va_end(arglist);

	WriteDebugLog(Mess);
}

int stricmp(const unsigned char * pStr1, const unsigned char * pStr2)
{
	unsigned char c1, c2;
	int v;

	if (pStr1 == NULL)
	{
		if (pStr2)
			Debugprintf("stricmp called with NULL 1st param - 2nd %s ", pStr2);
		else
			Debugprintf("stricmp called with two NULL params");
		return 1;
	}

	do {
		c1 = *pStr1++;
		c2 = *pStr2++;
		v = tolower(c1) - tolower(c2);
	} while ((v == 0) && (c1 != '\0') && (c2 != '\0'));

	return v;
}

int memicmp(unsigned char * a, unsigned char * b, int n)
{
	if (n)
	{
		while (n && (toupper(*a) == toupper(*b)))
			n--, a++, b++;

		if (n)
			return toupper(*a) - toupper(*b);
	}
	return 0;
}

static void mac_sigterm_handler(int n)
{
	(void)n;
	Closing = TRUE;
}

void platformInit(void)
{
	struct sigaction act;

	memset(&act, '\0', sizeof(act));

	act.sa_handler = mac_sigterm_handler;
	if (sigaction(SIGINT, &act, NULL) < 0)
		perror("SIGINT");
	if (sigaction(SIGTERM, &act, NULL) < 0)
		perror("SIGTERM");

	act.sa_handler = SIG_IGN;
	if (sigaction(SIGHUP, &act, NULL) < 0)
		perror("SIGHUP");
	if (sigaction(SIGPIPE, &act, NULL) < 0)
		perror("SIGPIPE");
}

// Serial port shims for SMMain.c's PTT path (Signalink-style RTS/DTR
// keying). Mirrors Linux.c, with termios + TIOCM ioctls that are
// portable to macOS as-is. macOS expects /dev/cu.* or /dev/tty.* device
// names; SMMain.c prepends "/dev/", so users should configure PTTPort
// as e.g. "cu.usbserial-XXXX".

static const struct {
	int user_speed;
	speed_t termios_speed;
} mac_speed_table[] = {
	{ 300,    B300 },
	{ 600,    B600 },
	{ 1200,   B1200 },
	{ 2400,   B2400 },
	{ 4800,   B4800 },
	{ 9600,   B9600 },
	{ 19200,  B19200 },
	{ 38400,  B38400 },
	{ 57600,  B57600 },
	{ 115200, B115200 },
	{ -1,     B0 },
};

// On TIOCMGET failure (USB serial unplug, driver without modem-control
// bits) status would be uninitialised, so bail out before TIOCMSET
// would otherwise drive the line to a garbage state.

void COMSetDTR(int fd)
{
	int status;

	if (ioctl(fd, TIOCMGET, &status) == -1)
	{
		perror("COMSetDTR PTT TIOCMGET");
		return;
	}
	status |= TIOCM_DTR;
	if (ioctl(fd, TIOCMSET, &status) == -1)
		perror("COMSetDTR PTT TIOCMSET");
}

void COMClearDTR(int fd)
{
	int status;

	if (ioctl(fd, TIOCMGET, &status) == -1)
	{
		perror("COMClearDTR PTT TIOCMGET");
		return;
	}
	status &= ~TIOCM_DTR;
	if (ioctl(fd, TIOCMSET, &status) == -1)
		perror("COMClearDTR PTT TIOCMSET");
}

void COMSetRTS(int fd)
{
	int status;

	if (ioctl(fd, TIOCMGET, &status) == -1)
	{
		perror("COMSetRTS PTT TIOCMGET");
		return;
	}
	status |= TIOCM_RTS;
	if (ioctl(fd, TIOCMSET, &status) == -1)
		perror("COMSetRTS PTT TIOCMSET");
}

void COMClearRTS(int fd)
{
	int status;

	if (ioctl(fd, TIOCMGET, &status) == -1)
	{
		perror("COMClearRTS PTT TIOCMGET");
		return;
	}
	status &= ~TIOCM_RTS;
	if (ioctl(fd, TIOCMSET, &status) == -1)
		perror("COMClearRTS PTT TIOCMSET");
}

int OpenCOMPort(char * Port, int speed, BOOL SetDTR, BOOL SetRTS, BOOL Quiet, int Stopbits)
{
	int fd;
	u_long param = 1;
	struct termios term;
	int i;
	speed_t termios_speed = B0;
	char fulldev[80];
	char buf[256];

	(void)Stopbits;

	snprintf(fulldev, sizeof(fulldev), "/dev/%s", Port);

	if ((fd = open(fulldev, O_RDWR | O_NONBLOCK)) == -1)
	{
		if (Quiet == 0)
		{
			perror("Com Open Failed");
			snprintf(buf, sizeof(buf), " %s could not be opened", fulldev);
			Debugprintf("%s", buf);
		}
		return 0;
	}

	for (i = 0; mac_speed_table[i].user_speed != -1; i++)
	{
		if (mac_speed_table[i].user_speed == speed)
		{
			termios_speed = mac_speed_table[i].termios_speed;
			break;
		}
	}
	if (mac_speed_table[i].user_speed == -1)
	{
		Debugprintf("OpenCOMPort: invalid speed %d", speed);
		close(fd);
		return 0;
	}

	if (tcgetattr(fd, &term) == -1)
	{
		perror("OpenCOMPort tcgetattr");
		close(fd);
		return 0;
	}

	cfmakeraw(&term);
	cfsetispeed(&term, termios_speed);
	cfsetospeed(&term, termios_speed);

	if (tcsetattr(fd, TCSANOW, &term) == -1)
	{
		perror("OpenCOMPort tcsetattr");
		close(fd);
		return 0;
	}

	ioctl(fd, FIONBIO, &param);

	Debugprintf("Port %s fd %d", fulldev, fd);

	if (SetDTR)
		COMSetDTR(fd);
	else
		COMClearDTR(fd);

	if (SetRTS)
		COMSetRTS(fd);
	else
		COMClearRTS(fd);

	return fd;
}

BOOL WriteCOMBlock(int fd, char * Block, int BytesToWrite)
{
	int ToSend = BytesToWrite;
	int Sent = 0;
	int ret;

	while (ToSend)
	{
		ret = write(fd, &Block[Sent], ToSend);

		if (ret >= ToSend)
			return TRUE;

		if (ret == -1)
		{
			if (errno != EAGAIN && errno != EWOULDBLOCK)
				return FALSE;

			usleep(10000);
			ret = 0;
		}

		Sent += ret;
		ToSend -= ret;
	}
	return TRUE;
}

void CloseCOMPort(int fd)
{
	close(fd);
}

// Audio device-name storage. ALSASound.c and Waveout.c each define
// these for their platform; on macOS the Qt path in QtSoundModem.cpp
// fills CaptureNames / PlaybackNames at runtime via QMediaDevices.
// Default device names are empty strings — QtSoundModem.cpp falls
// back to QMediaDevices::defaultAudioInput()/Output() when the
// configured name does not match an enumerated device.
//
// Buffer width is 256 bytes, matching CaptureNames / PlaybackNames
// rows. CoreAudio QAudioDevice::description() values can exceed 79
// UTF-8 bytes (some USB audio interfaces with long manufacturer
// prefixes), so the historical 80-byte limit allowed strcpy
// overflow. UZ7HOStuff.h, ALSASound.c and Waveout.c are widened to
// match — Linux/Windows definitions go from [80] to [256], a
// strict-superset change that costs ~352 bytes of bss per platform.

char CaptureDevice[256] = "";
char PlaybackDevice[256] = "";
// QAudioDevice::id().toBase64() of the user-selected devices,
// persisted across launches. Empty until first save through the
// Devices dialog; legacy installs fall back to description-string
// matching in GetAudioDevices.
char CaptureDeviceId[512] = "";
char PlaybackDeviceId[512] = "";

int CaptureCount = 0;
int PlaybackCount = 0;

char CaptureNames[256][256] = { "" };
char PlaybackNames[256][256] = { "" };

// Audio I/O state. Linux defines these in ALSASound.c / Linux.c;
// Windows in Waveout.c. macOS routes everything through the Qt
// path (SoundMode == 5 forced in Config.cpp::getSettings), so the
// definitions here are minimum-viable storage with sensible
// defaults. The Qt-path code in QtSoundModem.cpp owns the real
// state; these globals are mostly read by code that gates on
// SoundMode and bails out on the non-Qt branches.

int SoundMode = 5;        // forced to Qt at config-load on macOS
int AutoRetuneSampleRate = 1;  // INI Init/AutoRetuneSampleRate; default on
int onlyMixSnoop = 0;
int txLatency = 50;       // matches Init/txLatency default
int CaptureIndex = 0;
int PlayBackIndex = 0;
int using48000 = 0;
int useTimedPTT = 1;      // matches ALSASound.c default
int ReceiveSize = 512;    // matches Linux.c
int SendSize = 1024;      // matches Linux.c
unsigned char CurrentLevel = 0;
unsigned char CurrentLevelR = 0;
struct timespec pttclk;

// Audio entry-point stubs. SMMain.c, Modulate.c, sm_main.c,
// SoundInput.c and tcpCode.cpp call these from common code.
// Linux/Windows definitions branch on SoundMode internally; the
// macOS variants simply route the SoundMode == 5 case through the
// Qt path in QtSoundModem.cpp and treat other modes as no-ops or
// non-fatal failures (they cannot occur at runtime because
// SoundMode is forced to 5).

extern unsigned short * sendSamplestoQSound(unsigned short * buf, int n);
extern void QtSoundInit(void);
extern void closeQSound(void);
extern unsigned short * DMABuffer;
extern unsigned short QtDMABuffer[8192];
extern int Number;
extern int SoundIsPlaying;

short * SendtoCard(short * buf, int n)
{
	if (SoundMode == 5)
	{
		sendSamplestoQSound((unsigned short *)buf, n);
		return buf;
	}
	// Other SoundModes are not built on macOS.
	return buf;
}

short * SoundInit(void)
{
	// Match Linux/Windows semantics: ALWAYS reset DMABuffer to the
	// soundcard buffer. Modulate.c::initFilter repoints DMABuffer at
	// ARDOPTXBuffer during encoding; SMMain.c relies on SoundInit()
	// to switch it back before ARDOPSendToCard, otherwise that
	// function reads and writes the same buffer and TX corrupts.
	DMABuffer = QtDMABuffer;
	return (short *)QtDMABuffer;
}

extern int isAudioOutputOpen(void);

void SoundFlush(void)
{
	// End-of-frame flush. SMMain.c accumulates samples into
	// DMABuffer up to SendSize and pushes a full chunk via
	// SendtoCard; the trailing fractional buffer (Number samples
	// < SendSize) is the caller's responsibility to drain. Push it
	// here, then block until QAudioSink reports drained so PTT
	// can drop without truncating TX audio.

	if (SoundMode != 5)
		return;

	// If the output sink was refused by initializeAudioOut or torn
	// down by hot-unplug, sendSamplestoQSound is a no-op and
	// audioOutStateChanged will never fire IdleState — the
	// SoundIsPlaying flag would stay TRUE and the loop below would
	// burn the full 5 s timeout on every transmit. Clear the flag
	// and bail immediately. The earlier SMMain.c TX path has
	// already done its work; there's just no sink to drain.
	if (!isAudioOutputOpen())
	{
		Number = 0;
		SoundIsPlaying = 0;
		return;
	}

	if (Number > 0)
	{
		SendtoCard((short *)DMABuffer, Number);
		Number = 0;
	}

	// Wait for QAudioSink to reach IdleState. The
	// audioOutStateChanged slot in QtSoundModem.cpp clears
	// SoundIsPlaying when the sink signals IdleState. 1 s is a
	// generous bound: a 1200-sample frame at 12 kHz drains in
	// ~100 ms, and even RUH 9600 at 48 kHz drains in ~100 ms.
	// The previous 5 s ceiling meant up to 5 s of PTT-on dead
	// carrier when the sink was wedged (IdleState never delivered,
	// e.g. mid-Tx hot-unplug or a CoreAudio device glitch).
	//
	// A synchronous state() peek was considered but rejected: there
	// is a race between out->write() returning on the worker thread
	// and the sink transitioning out of IdleState on the audio
	// thread, so a sync IdleState reading could be stale-from-the-
	// previous-frame and short-circuit the wait too soon. Stick to
	// the async flag; the tightened timeout is the real fix here.
	unsigned int started = getTicks();
	unsigned int elapsed = 0;
	while (SoundIsPlaying && elapsed < 1000)
	{
		usleep(10000); // 10 ms
		elapsed = getTicks() - started;
	}

	if (SoundIsPlaying)
		Debugprintf("MacBits: SoundFlush timed out after %u ms waiting for IdleState; forcing PTT release\n", elapsed);

	// Clear the flag explicitly so DoTX / ProcessNewSamples don't
	// wedge if we exited via the timeout above.
	SoundIsPlaying = 0;
}

extern int nonGUIMode;
extern char * g_wavInputPath;        // set by main.cpp from --decode-wav <path>
extern char * g_wavInputNativePath;  // set by main.cpp from --decode-wav-native <path>

int InitSound(BOOL Report)
{
	(void)Report;
	if (SoundMode == 5)
	{
		// Both --decode-wav harness modes use nogui + bypass Qt audio;
		// allow init to succeed so the worker loop runs into the
		// harness dispatch in tcpCode.cpp.
		if (g_wavInputPath != NULL || g_wavInputNativePath != NULL)
			return TRUE;

		// QtSoundInit() owns the real init, called from the
		// QtSoundModem widget ctor. In --nogui mode main.cpp
		// never constructs the widget, so the Qt audio backend
		// never starts and capture/playback would silently fail.
		// Refuse and tell the user.
		if (nonGUIMode)
		{
			Debugprintf("QtSoundModem: --nogui mode is not supported on "
				"macOS — Qt audio is the only working backend and it "
				"requires the GUI widget. Run without --nogui or use a "
				"Linux/Windows build for headless operation.");
			return FALSE;
		}
		return TRUE;
	}
	// No other backend exists on macOS.
	return FALSE;
}

// --decode-wav harness: read a PCM 16-bit WAV (mono or stereo, any
// sample rate that's an integer multiple of 12 kHz from 12 kHz to
// 96 kHz) and feed it through the same decimator that the live audio
// path (PollQSound) uses, then into the modem. Bypasses Qt audio
// entirely — deterministic regression-test rig for both the boxcar
// (today) and the windowed-sinc FIR (next commit).
//
// Walks the RIFF chunk list rather than assuming a 44-byte canonical
// header so files produced by ffmpeg / sox with metadata LIST chunks
// or extended fmt chunks parse without manual stripping.
extern void ProcessNewSamples(short * Samples, int nSamples);
extern void decimateAudioToModem(const short * src, int decim, short * dst);
extern void aaFilterInit(int sampleRateIn);
extern void BufferFull(short * Samples, int nSamples);
extern int using48000;

void debugDecodeWav(const char * path)
{
	FILE * f = fopen(path, "rb");
	if (!f)
	{
		Debugprintf("debugDecodeWav: open %s failed: %s",
			path, strerror(errno));
		return;
	}

	unsigned char riff[12];
	if (fread(riff, 1, 12, f) != 12 ||
		memcmp(riff, "RIFF", 4) != 0 ||
		memcmp(riff + 8, "WAVE", 4) != 0)
	{
		Debugprintf("debugDecodeWav: not a RIFF/WAVE file");
		fclose(f);
		return;
	}

	short numCh = 0;
	int sampleRate = 0;
	short bits = 0;
	short formatTag = 0;
	int dataChunkSize = 0;
	long dataStart = -1;

	// Walk chunks until we have fmt + data. Skip everything else
	// (LIST metadata, JUNK alignment, etc.). chunkSize is unsigned
	// in the WAV spec (DWORD); reading via signed int and rejecting
	// negatives catches malformed files cheaply rather than seeking
	// to a wild offset.
	while (1)
	{
		unsigned char ch[8];
		if (fread(ch, 1, 8, f) != 8) break;
		int chunkSize = ch[4] | (ch[5] << 8) | (ch[6] << 16) | (ch[7] << 24);
		if (chunkSize < 0)
		{
			Debugprintf("debugDecodeWav: malformed chunk size %d in %s, aborting walk",
				chunkSize, path);
			break;
		}

		if (memcmp(ch, "fmt ", 4) == 0)
		{
			unsigned char fmt[40] = {0};
			int n = chunkSize > (int)sizeof(fmt) ? (int)sizeof(fmt) : chunkSize;
			if (fread(fmt, 1, n, f) != (size_t)n) break;
			if (n < chunkSize) fseek(f, chunkSize - n, SEEK_CUR);
			// PCMWAVEFORMAT layout: format(0-1) channels(2-3) rate(4-7)
			// byterate(8-11) blockalign(12-13) bits(14-15).
			formatTag = (short)(fmt[0] | (fmt[1] << 8));
			numCh = (short)(fmt[2] | (fmt[3] << 8));
			sampleRate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
			bits = (short)(fmt[14] | (fmt[15] << 8));
		}
		else if (memcmp(ch, "data", 4) == 0)
		{
			dataStart = ftell(f);
			dataChunkSize = chunkSize;
			break;
		}
		else
		{
			// Skip this chunk (LIST / JUNK / id3 / etc).
			if (fseek(f, chunkSize, SEEK_CUR) != 0) break;
		}
	}

	if (dataStart < 0 || numCh == 0 || sampleRate == 0 || bits == 0)
	{
		Debugprintf("debugDecodeWav: failed to parse fmt + data chunks "
			"from %s (numCh=%d, rate=%d, bits=%d, dataStart=%ld)",
			path, numCh, sampleRate, bits, dataStart);
		fclose(f);
		return;
	}

	Debugprintf("debugDecodeWav: %s — %d ch, %d Hz, %d-bit, fmtTag=0x%04x, %d data bytes",
		path, numCh, sampleRate, bits, (unsigned short)formatTag, dataChunkSize);

	// WAVE_FORMAT_PCM is 0x0001. Anything else (float 0x0003, A-law
	// 0x0006, EXTENSIBLE 0xFFFE, …) would feed garbage to the
	// decimator if we just trusted bits==16. Refuse loudly instead.
	if (formatTag != 0x0001)
	{
		Debugprintf("debugDecodeWav: format tag 0x%04x is not PCM (0x0001); refusing. "
			"Pre-convert with: ffmpeg -i in.* -c:a pcm_s16le out.wav",
			(unsigned short)formatTag);
		fclose(f);
		return;
	}

	if (bits != 16 || (numCh != 1 && numCh != 2))
	{
		Debugprintf("debugDecodeWav: expected 16-bit PCM / 1 or 2 ch. "
			"Pre-convert with: ffmpeg -i in.* -c:a pcm_s16le out.wav");
		fclose(f);
		return;
	}

	int decim = 1;
	if (sampleRate < 12000)
	{
		Debugprintf("debugDecodeWav: sample rate %d Hz < 12000 Hz; "
			"upsampling not supported, aborting", sampleRate);
		fclose(f);
		return;
	}
	decim = sampleRate / 12000;
	if (decim > 8)
	{
		Debugprintf("debugDecodeWav: decim factor %d (sample rate %d Hz) "
			"exceeds reasonable range; aborting", decim, sampleRate);
		fclose(f);
		return;
	}
	if (sampleRate % 12000 != 0)
	{
		Debugprintf("debugDecodeWav: WARNING %d Hz is not an integer "
			"multiple of 12000 Hz; truncating to decim=%d (output rate "
			"%d Hz, drift %.2f%%). Pre-resample for clean tests.",
			sampleRate, decim, sampleRate / decim,
			100.0 * (1.0 - 12000.0 * decim / sampleRate));
	}

	// Design the antialias FIR for this file's rate. The same helper
	// runs from initializeAudioIn for live audio; designing twice with
	// the same rate is a cheap no-op.
	aaFilterInit(sampleRate);

	// Force one BPF/TXBPF coefficient computation per channel before
	// the first sample lands in BufferFull. Normally the GUI paths
	// (RX-frequency change handlers, mode-change reload) set these
	// flags during construction; in headless --decode-wav mode no
	// GUI runs so pnt_change stays FALSE and the modem code skips
	// the make_core_BPF call, leaving zero filter coefficients and
	// silently failing every decode. Mirror the GUI's "set all four
	// flags TRUE on init" behaviour here.
	extern int pnt_change[5];
	for (int i = 0; i < 4; i++) pnt_change[i] = 1;

	// Process in 512-output-frame chunks (matches PollQSound).
	const int inFramesPerChunk = 512 * decim;
	// Stack buffers sized for decim up to 8 (96 kHz → 12 kHz).
	short stereoIn[2 * 512 * 8];
	short monoBuf[512 * 8];
	short decimated[1024];
	int totalInFrames = 0;

	while (1)
	{
		if (numCh == 1)
		{
			size_t n = fread(monoBuf, sizeof(short), inFramesPerChunk, f);
			if ((int)n < inFramesPerChunk) break;  // ignore short tail
			for (int i = 0; i < inFramesPerChunk; i++)
			{
				stereoIn[2 * i]     = monoBuf[i];
				stereoIn[2 * i + 1] = monoBuf[i];
			}
		}
		else
		{
			size_t n = fread(stereoIn, sizeof(short) * 2, inFramesPerChunk, f);
			if ((int)n < inFramesPerChunk) break;  // ignore short tail (mono branch above too)
		}

		decimateAudioToModem(stereoIn, decim, decimated);
		ProcessNewSamples(decimated, 512);
		totalInFrames += inFramesPerChunk;
	}

	fclose(f);
	Debugprintf("debugDecodeWav: processed %d input frames (%.2f s at %d Hz)",
		totalInFrames, (double)totalInFrames / sampleRate, sampleRate);
}

// PCM-WAV chunk-walker factored out for reuse by the new native-rate
// harness below. The original debugDecodeWav above retains its own
// inline parser unchanged so its existing baseline behaviour is
// byte-for-byte preserved (same diagnostic messages, same error
// strings). Future cleanup could port debugDecodeWav to this helper
// too, but that's out of scope for the Plan-A commit.
//
// Returns 1 on success with the out-params populated, 0 on any parse
// error (already logged via Debugprintf). Caller fseek()s to dataStart
// and reads samples.
static int parseWavHeader(FILE * f, const char * path,
	short * outNumCh, int * outSampleRate, short * outBits,
	long * outDataStart, int * outDataChunkSize)
{
	unsigned char riff[12];
	if (fread(riff, 1, 12, f) != 12 ||
		memcmp(riff, "RIFF", 4) != 0 ||
		memcmp(riff + 8, "WAVE", 4) != 0)
	{
		Debugprintf("parseWavHeader: not a RIFF/WAVE file: %s", path);
		return 0;
	}

	short numCh = 0, bits = 0, formatTag = 0;
	int sampleRate = 0;
	int dataChunkSize = 0;
	long dataStart = -1;

	while (1)
	{
		unsigned char ch[8];
		if (fread(ch, 1, 8, f) != 8) break;
		int chunkSize = ch[4] | (ch[5] << 8) | (ch[6] << 16) | (ch[7] << 24);
		if (chunkSize < 0)
		{
			Debugprintf("parseWavHeader: malformed chunk size %d in %s", chunkSize, path);
			break;
		}

		if (memcmp(ch, "fmt ", 4) == 0)
		{
			unsigned char fmt[40] = {0};
			int n = chunkSize > (int)sizeof(fmt) ? (int)sizeof(fmt) : chunkSize;
			if (fread(fmt, 1, n, f) != (size_t)n) break;
			if (n < chunkSize) fseek(f, chunkSize - n, SEEK_CUR);
			formatTag = (short)(fmt[0] | (fmt[1] << 8));
			numCh = (short)(fmt[2] | (fmt[3] << 8));
			sampleRate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
			bits = (short)(fmt[14] | (fmt[15] << 8));
		}
		else if (memcmp(ch, "data", 4) == 0)
		{
			dataStart = ftell(f);
			dataChunkSize = chunkSize;
			break;
		}
		else
		{
			if (fseek(f, chunkSize, SEEK_CUR) != 0) break;
		}
	}

	if (dataStart < 0 || numCh == 0 || sampleRate == 0 || bits == 0)
	{
		Debugprintf("parseWavHeader: missing fmt+data chunks in %s "
			"(numCh=%d, rate=%d, bits=%d)", path, numCh, sampleRate, bits);
		return 0;
	}
	if (formatTag != 0x0001)
	{
		Debugprintf("parseWavHeader: format tag 0x%04x is not PCM (0x0001) in %s",
			(unsigned short)formatTag, path);
		return 0;
	}

	*outNumCh = numCh;
	*outSampleRate = sampleRate;
	*outBits = bits;
	*outDataStart = dataStart;
	*outDataChunkSize = dataChunkSize;
	return 1;
}

// --decode-wav-native harness: read a 48 kHz 16-bit PCM WAV (mono or
// stereo) and feed it raw to BufferFull with using48000=1 set. This
// bypasses decimateAudioToModem (and therefore the FIR antialias),
// giving the RUH/dw9600 demod the 48 kHz native baseband it expects.
// AFSK modems still work because BufferFull's runModems path
// downsamples internally — naive 4-sample-skip without antialias,
// adequate for clean direwolf test signals but not as good as the
// FIR-path --decode-wav for live off-air recordings.
//
// 48 kHz only — that's what the RUH demod hardcodes and the only
// rate that benefits from this mode.
void debugDecodeWavNative(const char * path)
{
	FILE * f = fopen(path, "rb");
	if (!f)
	{
		Debugprintf("debugDecodeWavNative: open %s failed: %s",
			path, strerror(errno));
		return;
	}

	short numCh = 0, bits = 0;
	int sampleRate = 0;
	long dataStart = -1;
	int dataChunkSize = 0;

	if (!parseWavHeader(f, path, &numCh, &sampleRate, &bits,
		&dataStart, &dataChunkSize))
	{
		fclose(f);
		return;
	}

	Debugprintf("debugDecodeWavNative: %s — %d ch, %d Hz, %d-bit, %d data bytes",
		path, numCh, sampleRate, bits, dataChunkSize);

	if (sampleRate != 48000 || bits != 16 || (numCh != 1 && numCh != 2))
	{
		Debugprintf("debugDecodeWavNative: requires 48000 Hz / 16-bit / 1 or 2 ch. "
			"Pre-convert with: ffmpeg -i in.* -ar 48000 -c:a pcm_s16le out.wav");
		fclose(f);
		return;
	}

	// Force one BPF/TXBPF coefficient computation per channel before
	// the first sample lands in BufferFull (same rationale as
	// debugDecodeWav).
	extern int pnt_change[5];
	for (int i = 0; i < 4; i++) pnt_change[i] = 1;

	// Flag the audio path as 48 kHz native so BufferFull's runModems
	// branch downsamples internally for FSK modems while leaving
	// Samples[] at 48 kHz for the RUH branch (which reads it raw).
	using48000 = 1;

	// rx_bufsize is 512 frames AT 12 kHz (the modem's working rate).
	// With using48000=1 BufferFull downsamples 4× internally, so the
	// caller must supply 4× rx_bufsize = 2048 stereo frames per call
	// at 48 kHz native. Mirrors the live-audio ReceiveSize path
	// (QtSoundModem.cpp sets ReceiveSize=2048 when any RUH modem is
	// active, which is exactly this case).
	const int chunkFrames = 2048;
	short stereoIn[2 * 2048];  // 4096 shorts per call
	short monoBuf[2048];
	int totalInFrames = 0;

	while (1)
	{
		if (numCh == 1)
		{
			size_t n = fread(monoBuf, sizeof(short), chunkFrames, f);
			if ((int)n < chunkFrames) break;
			for (int i = 0; i < chunkFrames; i++)
			{
				stereoIn[2 * i]     = monoBuf[i];
				stereoIn[2 * i + 1] = monoBuf[i];
			}
		}
		else
		{
			size_t n = fread(stereoIn, sizeof(short) * 2, chunkFrames, f);
			if ((int)n < chunkFrames) break;  // ignore short tail (mono branch above too)
		}

		BufferFull(stereoIn, chunkFrames);
		totalInFrames += chunkFrames;
	}

	fclose(f);
	Debugprintf("debugDecodeWavNative: processed %d input frames (%.2f s at %d Hz)",
		totalInFrames, (double)totalInFrames / sampleRate, sampleRate);
}

unsigned int getTicks(void)
{
	// Absolute monotonic milliseconds, truncated to 32 bits.
	// Wraps after ~49 days; callers compute deltas, so wrap is
	// benign. No first-call init race.
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned int)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

void printtick(char * msg)
{
	static unsigned int last = 0;
	unsigned int now = getTicks();
	Debugprintf("%s %u", msg ? msg : "", now - last);
	last = now;
}

void PollReceivedSamples(void)
{
	// SMMain.c::MainLoop calls this in the SoundMode != 5 branch;
	// the SoundMode == 5 branch routes to PollQSound directly.
	// Empty stub so the symbol resolves; runtime path never reaches
	// here while SoundMode is forced to 5.
}

int initPulse(void)
{
	// PulseAudio is not built on macOS.
	return FALSE;
}
