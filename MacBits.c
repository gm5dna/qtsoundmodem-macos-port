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

char CaptureDevice[80] = "";
char PlaybackDevice[80] = "";

int CaptureCount = 0;
int PlaybackCount = 0;

char CaptureNames[256][256] = { "" };
char PlaybackNames[256][256] = { "" };
