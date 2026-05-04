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

	if (Number > 0)
	{
		SendtoCard((short *)DMABuffer, Number);
		Number = 0;
	}

	// Wait for QAudioSink to reach IdleState. The
	// audioOutStateChanged slot in QtSoundModem.cpp clears
	// SoundIsPlaying on IdleState. 5 s is a generous bound — even
	// a 1200-sample frame at 12 kHz drains in well under 1 s.
	unsigned int started = getTicks();
	while (SoundIsPlaying && (getTicks() - started) < 5000)
		usleep(10000); // 10 ms

	// If we hit the timeout (sink torn down by hot-unplug, or
	// IdleState never delivered for some other reason) clear the
	// flag explicitly so DoTX / ProcessNewSamples don't wedge.
	SoundIsPlaying = 0;
}

extern int nonGUIMode;
extern char * g_wavInputPath;  // set by main.cpp from --decode-wav <path>

int InitSound(BOOL Report)
{
	(void)Report;
	if (SoundMode == 5)
	{
		// --decode-wav harness uses nogui + bypasses Qt audio; allow
		// init to succeed so the worker loop runs.
		if (g_wavInputPath != NULL)
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

// --decode-wav harness: read 12 kHz mono/stereo 16-bit PCM WAV
// directly into the modem, bypassing Qt audio entirely. Pre-convert
// any input with `afconvert -f WAVE -d LEI16@12000 -c 1 in.flac
// out.wav`. Only handles canonical 44-byte PCM WAV headers — fails
// loudly on extended headers / wrong rate / wrong bit depth.
extern void ProcessNewSamples(short * Samples, int nSamples);

void debugDecodeWav(const char * path)
{
	FILE * f = fopen(path, "rb");
	if (!f)
	{
		Debugprintf("debugDecodeWav: open %s failed: %s",
			path, strerror(errno));
		return;
	}

	unsigned char hdr[44];
	if (fread(hdr, 1, 44, f) != 44)
	{
		Debugprintf("debugDecodeWav: short header read");
		fclose(f);
		return;
	}

	if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
	{
		Debugprintf("debugDecodeWav: not a RIFF/WAVE file");
		fclose(f);
		return;
	}

	short numCh    = (short)(hdr[22] | (hdr[23] << 8));
	int sampleRate = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24);
	short bits     = (short)(hdr[34] | (hdr[35] << 8));

	Debugprintf("debugDecodeWav: %s — %d ch, %d Hz, %d-bit",
		path, numCh, sampleRate, bits);

	if (sampleRate != 12000 || bits != 16 || (numCh != 1 && numCh != 2))
	{
		Debugprintf("debugDecodeWav: expected 12000 Hz / 16-bit / 1 or 2 ch. "
			"Pre-convert with: afconvert -f WAVE -d LEI16@12000 -c 1 in.* out.wav");
		fclose(f);
		return;
	}

	// Process in 512-stereo-sample chunks (matches PollQSound).
	short stereo[1024];
	int totalFrames = 0;

	if (numCh == 1)
	{
		short mono[512];
		while (1)
		{
			size_t n = fread(mono, sizeof(short), 512, f);
			if (n == 0) break;
			for (size_t i = 0; i < n; i++)
			{
				stereo[2 * i]     = mono[i];
				stereo[2 * i + 1] = mono[i];
			}
			ProcessNewSamples(stereo, (int)n);
			totalFrames += (int)n;
		}
	}
	else
	{
		while (1)
		{
			// 512 stereo frames = 1024 shorts = 2048 bytes
			size_t n = fread(stereo, sizeof(short) * 2, 512, f);
			if (n == 0) break;
			ProcessNewSamples(stereo, (int)n);
			totalFrames += (int)n;
		}
	}

	fclose(f);
	Debugprintf("debugDecodeWav: processed %d sample frames (%.2f s)",
		totalFrames, (double)totalFrames / sampleRate);
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
