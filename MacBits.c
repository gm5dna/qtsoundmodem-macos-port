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
#include <unistd.h>

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
