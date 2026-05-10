/*
Copyright (C) 2019-2020 Andrei Kopanchuk UZ7HO

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

// UZ7HO Soundmodem Port by John Wiseman G8BPQ



#include "QtSoundModem.h"
#include <QtWidgets/QApplication>
#include "UZ7HOStuff.h"
#if defined(Q_OS_MACOS)
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QSettings>

#if defined(Q_OS_MACOS)
extern "C" int macSetDeviceNominalSampleRate(const char *uidUtf8,
    double *outChosenRate, char *errBuf, int errBufLen);
#endif
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#endif

extern "C" int nonGUIMode;

// Set by --decode-wav <path> on the command line. NULL means normal
// audio path; non-NULL means the wav harness will run after worker
// init and the process will exit when the wav is exhausted.
extern "C" char * g_wavInputPath = NULL;
extern "C" void debugDecodeWav(const char * path);

// Set by --decode-wav-native <path>. Like --decode-wav but feeds 48 kHz
// raw samples to BufferFull with using48000=1, bypassing the FIR
// decimator in PollQSound. Required for RUH48/RUH96 testing because
// dw9600's demod_9600_init is hardwired to 48 kHz; the standard
// --decode-wav path decimates to 12 kHz first which the RUH demod
// can't decode. AFSK still works because BufferFull's runModems path
// downsamples internally (naive 4-sample-skip; less clean than the
// FIR path but adequate for the clean direwolf test signals).
extern "C" char * g_wavInputNativePath = NULL;
extern "C" void debugDecodeWavNative(const char * path);

// Set by --dump-input <path>. When non-NULL, PollQSound additionally
// writes its captured samples to this WAV file (12 kHz stereo Int16).
// Lets us compare what Qt is actually delivering against the working
// reference wav. The file is written incrementally; the WAV header's
// data-size field is patched up at exit (or left zero on a kill).
extern "C" char * g_dumpInputPath = NULL;

extern void getSettings();
extern void saveSettings();
extern int Closing;

workerThread *t;
serialThread *serial;
mynet m1;

QCoreApplication * a;		

QtSoundModem * w;


int main(int argc, char *argv[])
{
	char Title[128];
	QString Response;

	if (argc > 1 && strcmp(argv[1], "nogui") == 0)
		nonGUIMode = 1;

	// --decode-wav <path>: bypass Qt audio, feed the WAV directly
	// into the modem, exit when done. Implies --nogui.
	// --dump-input <path>: capture live Qt audio into a WAV.
	for (int i = 1; i < argc - 1; i++)
	{
		if (strcmp(argv[i], "--decode-wav") == 0)
		{
			g_wavInputPath = argv[i + 1];
			nonGUIMode = 1;
		}
		if (strcmp(argv[i], "--decode-wav-native") == 0)
		{
			g_wavInputNativePath = argv[i + 1];
			nonGUIMode = 1;
		}
		if (strcmp(argv[i], "--dump-input") == 0)
		{
			g_dumpInputPath = argv[i + 1];
		}
	}

	if (nonGUIMode)
		sprintf(Title, "QtSoundModem Version %s Running in non-GUI Mode", VersionString);
	else
		sprintf(Title, "QtSoundModem Version %s Running in GUI Mode", VersionString);

	qDebug() << Title;



#if defined(Q_OS_MACOS)
	// Set org/app metadata BEFORE QApplication construction. Qt-recommended
	// ordering: several Qt subsystems (QSettings, QStandardPaths,
	// QFileSystemEngine cache) consult QCoreApplication::organizationName/
	// applicationName at first access and may cache. Setting before
	// construction is the safe canonical order.
	QCoreApplication::setOrganizationName("gm5dna");
	QCoreApplication::setOrganizationDomain("gm5dna.com");
	QCoreApplication::setApplicationName("QtSoundModem");
#endif

	if (nonGUIMode)
		a = new QCoreApplication(argc, argv);
	else
		a = new QApplication(argc, argv);			// GUI version

#if defined(Q_OS_MACOS)
	// Canonicalise CLI paths against the launching shell's cwd BEFORE
	// the chdir below. Otherwise --decode-wav / --decode-wav-native /
	// --dump-input with a relative path silently fail to open from
	// the AppData cwd. strdup is fine — these strings live for the
	// lifetime of the process.
	//
	// All failure paths are fatal: the user explicitly asked for the
	// path on the command line. Silently keeping the relative form
	// would land us back in the chdir-broken state this commit set
	// out to fix.
	auto canonicaliseCliPath = [](char *& p, const char * argName)
	{
		if (!p || p[0] == '/') return;  // null or already absolute
		char cwd[PATH_MAX];
		if (!getcwd(cwd, sizeof(cwd)))
			qFatal("%s: getcwd failed canonicalising '%s' (%s)",
				argName, p, strerror(errno));
		char joined[PATH_MAX];
		int n = snprintf(joined, sizeof(joined), "%s/%s", cwd, p);
		if (n <= 0 || n >= (int)sizeof(joined))
			qFatal("%s: cwd+path exceeds PATH_MAX while canonicalising '%s'",
				argName, p);
		char * dup = strdup(joined);
		if (!dup)
			qFatal("%s: strdup failed canonicalising '%s'", argName, p);
		p = dup;
	};
	canonicaliseCliPath(g_wavInputPath,       "--decode-wav");
	canonicaliseCliPath(g_wavInputNativePath, "--decode-wav-native");
	canonicaliseCliPath(g_dumpInputPath,      "--dump-input");

	// Config / Save Settings open "QtSoundModem.ini" via a *relative*
	// path. Linux/Windows users launch from the install dir so the
	// cwd happens to be writable; a Finder-launched .app has cwd=/
	// and QSettings::AccessError fires on first save. chdir into the
	// AppDataLocation subdir before any QSettings call.
	QString macConfigDir = QStandardPaths::writableLocation(
		QStandardPaths::AppDataLocation);
	if (!macConfigDir.isEmpty())
	{
		QDir().mkpath(macConfigDir);
		if (!QDir::setCurrent(macConfigDir))
			qWarning() << "Failed to chdir to" << macConfigDir
				<< "— settings may fail to save.";
	}

	// Pre-Qt retune: switch the saved RX/TX devices' nominal CoreAudio
	// rate to a 12-kHz multiple BEFORE Qt populates QPlatformAudioDevices'
	// device-format cache. Qt 6 caches AudioDeviceFormat per QAudioDevice
	// and only refreshes it on device-list-changed events; a HAL
	// kAudioDevicePropertyNominalSampleRate change does NOT fire that.
	// If we retune AFTER Qt's first QMediaDevices call (e.g. inside
	// QtSoundInit), Qt's cache reflects pre-retune capabilities and
	// QAudioSource::start refuses our 48 kHz format — even though the
	// HAL is now at 48 kHz. Doing it here, before getSettings()'s
	// QMediaDevices::defaultAudioInput() call, lets Qt's first probe
	// see the post-retune device.
	//
	// Gated on !nonGUIMode: --decode-wav, --decode-wav-native, and
	// nogui starts all set nonGUIMode=1 and bypass live audio entirely.
	// Mutating the user's CoreAudio hardware rate from a regression-
	// test harness or a headless WAV-decode invocation would be a
	// nasty surprise. --dump-input does NOT set nonGUIMode (it
	// captures live audio on top of normal operation), so it still
	// gets the retune.
	if (!nonGUIMode)
	{
		QSettings ini("QtSoundModem.ini", QSettings::IniFormat);
		bool autoRetune = ini.value("Init/AutoRetuneSampleRate", 1).toBool();
		if (autoRetune) {
			QByteArray rxId = QByteArray::fromBase64(
				ini.value("Init/SndRXDeviceId").toString().toUtf8());
			QByteArray txId = QByteArray::fromBase64(
				ini.value("Init/SndTXDeviceId").toString().toUtf8());

			char errBuf[256];
			double chosen = 0.0;
			if (!rxId.isEmpty()) {
				errBuf[0] = '\0';
				int rc = macSetDeviceNominalSampleRate(
					rxId.constData(), &chosen, errBuf, sizeof(errBuf));
				qDebug() << "Pre-Qt RX retune rc=" << rc
				         << "rate=" << chosen
				         << "uid=" << rxId.constData()
				         << "err=" << errBuf;
			}
			if (!txId.isEmpty() && txId != rxId) {
				errBuf[0] = '\0';
				int rc = macSetDeviceNominalSampleRate(
					txId.constData(), &chosen, errBuf, sizeof(errBuf));
				qDebug() << "Pre-Qt TX retune rc=" << rc
				         << "rate=" << chosen
				         << "uid=" << txId.constData()
				         << "err=" << errBuf;
			}
		}
	}
#endif

	getSettings();


	t = new workerThread;

	if (nonGUIMode == 0)
	{
		w = new QtSoundModem();

		char Title[128];
		sprintf(Title, "QtSoundModem Version %s Ports %d%s/%d%s", VersionString, AGWPort, AGWServ ? "*" : "", KISSPort, KISSServ ? "*" : "");
		w->setWindowTitle(Title);

		w->show();
	}

	QObject::connect(&m1, SIGNAL(HLSetPTT(int)), &m1, SLOT(doHLSetPTT(int)), Qt::QueuedConnection);
	QObject::connect(&m1, SIGNAL(FLRigSetPTT(int)), &m1, SLOT(doFLRigSetPTT(int)), Qt::QueuedConnection);
	QObject::connect(&m1, SIGNAL(mgmtSetPTT(int, int)), &m1, SLOT(domgmtSetPTT(int, int)), Qt::QueuedConnection);


	QObject::connect(&m1, SIGNAL(startTimer(int)), &m1, SLOT(dostartTimer(int)), Qt::QueuedConnection);
	QObject::connect(&m1, SIGNAL(stopTimer()), &m1, SLOT(dostopTimer()), Qt::QueuedConnection);

	t->start();				// This runs init

	m1.start();				// Start TCP 

	return a->exec();

}

