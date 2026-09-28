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

// Qt Multimedia audio layer (SoundMode 5): device enumeration and
// hot-plug, CoreAudio rate retune, capture (PollQSound + FIR
// decimation) and TX (sendSamplestoQSound). Moved verbatim out of
// QtSoundModem.cpp so the port's audio code no longer sits inside an
// upstream file.

#include "QtSoundModem.h"
#include <QMessageBox>
#include <QTimer>
#include <QMutex>
#include <QCoreApplication>
#include <atomic>
#include <mutex>

#include "UZ7HOStuff.h"

#include <time.h>

// Globals owned by QtSoundModem.cpp.
extern workerThread *t;
extern QCoreApplication * a;
extern serialThread *serial;
extern "C" void CloseSound();
extern "C" void GetSoundDevices();
extern "C" char modes_name[modes_count][21];
extern "C" int speed[5];
extern "C" int KISSPort;
extern "C" short rx_freq[5];
extern "C" int CaptureCount;
extern "C" int PlaybackCount;
extern "C" int PlayBackIndex;
extern "C" char CaptureNames[256][256];
extern "C" char PlaybackNames[256][256];
extern "C" int SoundMode;
extern "C" bool onlyMixSnoop;
extern "C" int multiCore;
extern "C" int refreshModems;
extern "C" int pnt_change[5];
extern "C" int needRSID[4];
extern "C" int needSetOffset[4];
extern "C" float MagOut[4096];
extern "C" float MaxMagOut;
extern "C" int MaxMagIndex;
extern "C" int ReceiveSize;
extern "C" int txLatency;
extern "C" int BusyDet;
extern "C" char CWIDMark[32];
extern "C" int NeedWaterfallHeaders;
extern "C" float BinSize;
extern "C" unsigned int pskStates[4];
extern "C" bool useKISSControls;
extern "C" int UDPClientPort;
extern "C" int UDPServerPort;
extern "C" int TXPort;
extern char UDPHost[64];
extern QList<QAudioDevice> inputDevices;
extern QList<QAudioDevice> outputDevices;
extern QList<QAudioDevice> inputDevicesFiltered;
extern QList<QAudioDevice> outputDevicesFiltered;
extern QAudioDevice inDeviceInfo;
extern QAudioDevice outDeviceInfo;
extern int txAudioLevel;
extern int rxAudioLevel;
extern "C" unsigned char CurrentLevel;
extern "C" unsigned char CurrentLevelR;

#if defined(Q_OS_MACOS)
// macOS device-rate retune wiring. The C entry lives in
// MacAudioRate.mm; result codes mirror QSM_RETUNE_* there.
// The two QByteArrays gate the per-device "couldn't change rate"
// warning so it shows once per UID rather than on every audio
// event. They live at file scope because three call sites
// (deviceaccept, QtSoundInit, onAudioDevicesChanged) need them.
extern "C" int macSetDeviceNominalSampleRate(const char *uidUtf8,
    double *outChosenRate, char *errBuf, int errBufLen);
enum {
    QSM_RETUNE_OK_NO_CHANGE         =  0,
    QSM_RETUNE_OK_CHANGED           =  1,
    QSM_RETUNE_ERR_DEVICE_NOT_FOUND = -2,
    QSM_RETUNE_ERR_NO_MATCHING_RATE = -3,
    QSM_RETUNE_ERR_SET_FAILED       = -4,
    QSM_RETUNE_ERR_TIMEOUT          = -5,
    QSM_RETUNE_ERR_QUERY_FAILED     = -6,
    QSM_RETUNE_ERR_NOT_SETTABLE     = -7,
};
static QByteArray s_lastWarnedRetuneIn;
static QByteArray s_lastWarnedRetuneOut;
// Re-entry guard. retuneDeviceIfNeeded calls QCoreApplication::
// processEvents(50ms) so Qt's CoreAudio backend can update its
// cached AudioDeviceFormat after the HAL nominal-rate change.
// Pumping the event loop also delivers any queued
// audioInputsChanged / audioOutputsChanged signals, which would
// re-enter onAudioDevicesChanged and (if our streams are null)
// auto-reopen them while deviceaccept / QtSoundInit are still
// mid-flight — leading to a duplicate initializeAudio* open.
// Bumping this counter for the duration of a retune lets the
// slot bail out cleanly; the next legitimate device-list event
// after the retune finishes will deliver an up-to-date snapshot.
static int s_retuneInProgress = 0;
// An event skipped during a retune is replayed (queued) once the last
// retune unwinds, so e.g. an unplug during a retune warning isn't lost.
static bool s_devChangeDeferred = false;
#endif

 // QSound based Soundcard interface


 static QIODevice * out;
 static QIODevice * in;

 QAudioSink * m_audioOutput;
 QAudioSource * m_audioInput;

 // Serialises every access to the four audio pointers above
 // (m_audioInput, m_audioOutput, in, out) so the worker-thread
 // TX/RX path cannot read a pointer the GUI-thread teardown is in
 // the middle of nulling. Held only across pointer reads/writes
 // and short Qt calls (bytesFree, write, stop); never across
 // txSleep or anything that would block. File-static, not a class
 // member, because the worker callbacks have C linkage and don't
 // carry a `this`.
 // std::mutex rather than QMutex so ThreadSanitizer can see it.
 static std::mutex s_audioMutex;

 // Capture generation. The GUI thread opens and tears down capture;
 // the worker (PollQSound) owns Buffer/BufferLen, the sub-frame carry
 // and the FIR state. Every open and teardown publishes the stream's
 // parameters and bumps s_capGen under s_audioMutex. The worker applies
 // a new generation (drop buffered capture, redesign/flush the FIR)
 // inside the same lock hold as its read, so bytes from two streams
 // can never mix and the FIR is never touched by the GUI thread.
 struct CaptureParams { int rate, decim, channels; };
 static CaptureParams s_capParams = { 12000, 1, 2 };	// under s_audioMutex
 static std::atomic<unsigned> s_capGen{0};			// bumped under s_audioMutex

#ifndef WIN32
 extern "C" int stricmp(char * pStr1, char *pStr2);
#endif

 void QtSoundModem::GetAudioDevices()
 {
	 // Refresh the cached lists every time. Qt 6's QMediaDevices needs
	 // a live QCoreApplication and the user can hot-plug devices.
	 inputDevices = QMediaDevices::audioInputs();
	 outputDevices = QMediaDevices::audioOutputs();
	 if (inDeviceInfo.isNull())
		 inDeviceInfo = QMediaDevices::defaultAudioInput();
	 if (outDeviceInfo.isNull())
		 outDeviceInfo = QMediaDevices::defaultAudioOutput();

	 CaptureCount = 0;
	 inputDevicesFiltered.clear();
	 Debugprintf("Capture Devices:");

	 for (int i = 0; i < inputDevices.count(); ++i)
	 {
		 QString deviceName = inputDevices[i].description();

		 if (strstr(deviceName.toUtf8(), "surround") == 0)
		 {
			 qstrncpy(CaptureNames[CaptureCount], deviceName.toUtf8().constData(),
				 sizeof(CaptureNames[CaptureCount]));
			 inputDevicesFiltered.append(inputDevices[i]);

			 bool matched = false;
#if defined(Q_OS_MACOS)
			 // Prefer stable-id match: distinguishes duplicate-named
			 // devices and survives macOS Audio MIDI Setup renames.
			 // Falls through to description match for legacy .ini
			 // files written before Commit B.
			 if (CaptureDeviceId[0] != '\0')
			 {
				 QByteArray idB64 = inputDevices[i].id().toBase64();
				 if (qstrcmp(idB64.constData(), CaptureDeviceId) == 0)
				 {
					 matched = true;
					 // Refresh the description so the .ini stays
					 // human-readable if macOS renamed the device.
					 qstrncpy(CaptureDevice, deviceName.toUtf8().constData(),
						 sizeof(CaptureDevice));
				 }
			 }
#endif
			 if (!matched && stricmp(&CaptureNames[CaptureCount][0], CaptureDevice) == 0)
				 matched = true;

			 if (matched)
			 {
				if (inDeviceInfo == QMediaDevices::defaultAudioInput())
					 inDeviceInfo = inputDevices[i];

				qDebug() << "* " << deviceName;
			 }
			 else
				 qDebug() << "  " << deviceName;

			 CaptureCount++;
		 }
	 }

	 PlaybackCount = 0;
	 outputDevicesFiltered.clear();
	 Debugprintf("Playback Devices:");

	 for (int i = 0; i < outputDevices.count(); ++i)
	 {
		 QString deviceName = outputDevices[i].description();

		 if (strstr(deviceName.toUtf8(), "surround") == 0)
		 {
			 qstrncpy(PlaybackNames[PlaybackCount], deviceName.toUtf8().constData(),
				 sizeof(PlaybackNames[PlaybackCount]));
			 outputDevicesFiltered.append(outputDevices[i]);

			 bool matched = false;
#if defined(Q_OS_MACOS)
			 // See CaptureNames matcher above for the id-first rationale.
			 if (PlaybackDeviceId[0] != '\0')
			 {
				 QByteArray idB64 = outputDevices[i].id().toBase64();
				 if (qstrcmp(idB64.constData(), PlaybackDeviceId) == 0)
				 {
					 matched = true;
					 qstrncpy(PlaybackDevice, deviceName.toUtf8().constData(),
						 sizeof(PlaybackDevice));
				 }
			 }
#endif
			 if (!matched && stricmp(&PlaybackNames[PlaybackCount][0], PlaybackDevice) == 0)
				 matched = true;

			 if (matched)
			 {
				 if (outDeviceInfo == QMediaDevices::defaultAudioOutput())
					 outDeviceInfo = outputDevices[i];
				 qDebug() << "* " << deviceName;
			 }

			 else
				 qDebug() << "  " << deviceName;

			 PlaybackCount++;
		 }
	 }

	 Debugprintf("CaptureCount %d PlaybackCount %d", CaptureCount, PlaybackCount);
 }

 void QtSoundModem::audioInStateChanged(QAudio::State newState)
 {
	 // A queued stateChanged can be delivered after teardown has nulled
	 // m_audioInput (the disconnect-before-stop ordering in closeQSound /
	 // onAudioDevicesChanged closes the window for new emissions, but
	 // events already queued before disconnect still dispatch). Bail
	 // before dereferencing.
	 if (!m_audioInput)
		 return;

	 switch (newState)
	 {
	 case QAudio::StoppedState:
		 if (m_audioInput->error() != QAudio::NoError) 
		 {
			 // Source errored out mid-Rx (USB unplug, CoreAudio
			 // device-lost, rate change rejected, etc). Before this
			 // the branch was empty, so the failure mode was a
			 // completely silent Rx death — PollQSound just keeps
			 // returning early on the null/!in guard with no trace.
			 // Log so the wedge is diagnosable; mirror of the
			 // audioOutStateChanged StoppedState+error fix.
			 //
			 // We deliberately do NOT null `in` / m_audioInput here.
			 // The QAudioSource::stateChanged connect is made before
			 // m_audioInput->start(), and start() is called while
			 // initializeAudioIn holds s_audioMutex; a direct-connection
			 // emission would re-enter this slot with that mutex held,
			 // so taking s_audioMutex here could self-deadlock the
			 // non-recursive QMutex. Genuine device removal is already
			 // handled by onAudioDevicesChanged (stop + null under the
			 // mutex, with inDeviceInfo cleared to stop the reopen
			 // loop); this handler only needs to make a transient
			 // source error observable.
			 Debugprintf("audioInStateChanged: source stopped with error %d — Rx halted",
				 (int)m_audioInput->error());
		 }
		 else {
			 // Finished recording
		 }
		 break;

	 case QAudio::ActiveState:

		 // Started recording - read from IO device

		 break;

	 case QAudio::IdleState:

		 // Started recording - read from IO device
		 break;

	 default:
		 // ... other cases as appropriate
		 break;
	 }
 }

 // Consecutive automatic output reopens; see audioOutStateChanged.
 // Atomic: reset by the worker in qtAudioConsumeSinkIdle.
 static std::atomic<int> s_outRecoveries{0};

 // SoundIsPlaying is read and written by the modem worker (DoTX sets it,
 // SoundFlush waits on it). The sink's stateChanged slots run on the GUI
 // thread, so instead of writing it themselves they raise this flag and
 // the worker clears SoundIsPlaying when it next polls.
 static std::atomic<int> s_sinkIdle{0};

 // Worker thread: PollQSound entry and the SoundFlush wait loop.
 extern "C" void qtAudioConsumeSinkIdle()
 {
	 if (s_sinkIdle.exchange(0))
	 {
		 if (SoundIsPlaying)
			 s_outRecoveries = 0;	// a TX drained: the sink works
		 SoundIsPlaying = 0;
	 }
 }

 void QtSoundModem::audioOutStateChanged(QAudio::State newState)
 {
	 // See audioInStateChanged: queued stateChanged can be delivered
	 // after teardown nulls m_audioOutput.
	 if (!m_audioOutput)
		 return;

	 switch (newState)
	 {
	 case QAudio::StoppedState:
		 // Upstream typo: was checking m_audioInput here. The branch was
		 // empty so it had no functional effect, but reading the wrong
		 // pointer makes the code misleading. Touched on the way past.
		 if (m_audioOutput->error() != QAudio::NoError)
		 {
			 // Sink errored out mid-Tx (USB unplug, CoreAudio device-lost,
			 // rate change rejected, etc). Log so the wedge is diagnosable;
			 // the sendSamplestoQSound state/error check + SoundFlush
			 // 1 s timeout (MacBits.c) handles PTT release. Clear the
			 // SoundIsPlaying flag here too in case sendSamplestoQSound
			 // wasn't actively waiting when the state changed.
			 Debugprintf("audioOutStateChanged: sink stopped with error %d — Tx aborted",
				 (int)m_audioOutput->error());
			 s_sinkIdle.store(1);

			 // A stopped sink never restarts by itself, so every later
			 // TX would key PTT with no audio. Reopen the same device
			 // once things settle. Deferred (never inline: this slot can
			 // run inside start() with s_audioMutex held) and capped so a
			 // persistently failing device doesn't loop; the count resets
			 // when a TX drains normally (IdleState below). A device that
			 // has gone away is handled by onAudioDevicesChanged instead.
			 static bool s_recoveryPending = false;
			 if (!s_recoveryPending && s_outRecoveries < 3)
			 {
				 s_recoveryPending = true;
				 s_outRecoveries++;
				 QTimer::singleShot(2000, this, [this]()
				 {
					 s_recoveryPending = false;
					 if (!m_audioOutput || outDeviceInfo.isNull())
						 return;
					 if (m_audioOutput->state() != QAudio::StoppedState)
						 return;
					 Debugprintf("audioOutStateChanged: reopening output '%s' (attempt %d)",
						 outDeviceInfo.description().toUtf8().constData(), s_outRecoveries.load());
					 {
						 QMutexLocker locker(&s_audioMutex);
						 disconnect(m_audioOutput, &QAudioSink::stateChanged,
							 this, &QtSoundModem::audioOutStateChanged);
						 m_audioOutput->stop();
						 out = nullptr;
						 m_audioOutput->deleteLater();
						 m_audioOutput = nullptr;
					 }
					 initializeAudioOut(outDeviceInfo);
				 });
			 }
		 }
		 else 
		 {
			 // Finished recording
		 }
		 break;

	 case QAudio::ActiveState:

		 break;

	 case QAudio::IdleState:

		 // Used to see when TX is complete so can drop PTT

			 // I think we should turn round the link here. I dont see the point in
	 // waiting for MainPoll

		 s_sinkIdle.store(1);	// worker clears SoundIsPlaying
		 break;


	 default:
		 // ... other cases as appropriate
		 break;
	 }
 }

 extern "C" unsigned short * DMABuffer;

 // Sized for RUH48/RUH96 modes: SendSize rises to 4096 there and
 // SampleSink/ARDOPSendToCard pack stereo into DMABuffer, so the
 // worst-case write index is 2*(SendSize-1)+1 = 8191. Linux and
 // Windows allocate buffer[N][MaxSendSize*2] = 8192 shorts; match.
 unsigned short QtDMABuffer[8192];

 #if defined(Q_OS_MACOS)
 extern "C" int macAudioAuthorisationStatus(void);
 extern "C" void macRequestAudioAuthorisation(void);

 void QtSoundModem::retuneDeviceIfNeeded(
     QAudioDevice &deviceInfo,
     QByteArray &lastWarnedKey,
     const char *direction)  // "input" or "output"
 {
     if (!AutoRetuneSampleRate) return;          // user opt-out
     if (deviceInfo.isNull()) return;
     const QByteArray uid = deviceInfo.id();
     if (uid.isEmpty()) return;

     // Re-entry guard — see s_retuneInProgress declaration. RAII so
     // an early return / exception in the body still decrements.
     ++s_retuneInProgress;
     struct RetuneGuard
     {
         int *p;
         QtSoundModem *w;
         ~RetuneGuard()
         {
             // Queued, so it runs after the caller (deviceaccept /
             // QtSoundInit) has reopened its streams.
             if (--*p == 0 && s_devChangeDeferred)
             {
                 s_devChangeDeferred = false;
                 QMetaObject::invokeMethod(w, &QtSoundModem::onAudioDevicesChanged,
                     Qt::QueuedConnection);
             }
         }
     } guard{&s_retuneInProgress, this};

     char errBuf[256] = {0};
     double chosenRate = 0.0;
     int rc = macSetDeviceNominalSampleRate(
         uid.constData(), &chosenRate, errBuf, sizeof(errBuf));

     if (rc == QSM_RETUNE_OK_NO_CHANGE) {
         Debugprintf("Audio %s device '%s' already at a 12-kHz "
             "multiple (%.0f Hz); leaving rate alone.",
             direction,
             deviceInfo.description().toUtf8().constData(),
             chosenRate);
         lastWarnedKey.clear();
         return;
     }
     if (rc == QSM_RETUNE_OK_CHANGED) {
         Debugprintf("Audio %s device '%s' retuned to %.0f Hz.",
             direction,
             deviceInfo.description().toUtf8().constData(),
             chosenRate);
         // Yield the event loop briefly so Qt's CoreAudio backend
         // can observe the HAL change before we re-fetch a fresh
         // QAudioDevice. Without this, isFormatSupported(48 kHz)
         // may still report the old (pre-retune) capabilities.
         QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
         const QList<QAudioDevice> fresh =
             (strcmp(direction, "input") == 0)
                 ? QMediaDevices::audioInputs()
                 : QMediaDevices::audioOutputs();
         for (const QAudioDevice &d : fresh) {
             if (d.id() == uid) { deviceInfo = d; break; }
         }
         Debugprintf("Audio %s device handle refreshed after retune.",
             direction);
         lastWarnedKey.clear();
         return;
     }

     // Failure path: log then surface a one-shot warning per UID.
     Debugprintf("Audio %s device '%s' retune failed (rc=%d): %s",
         direction, deviceInfo.description().toUtf8().constData(),
         rc, errBuf);
     if (lastWarnedKey == uid) return;
     lastWarnedKey = uid;

     QString reason;
     switch (rc) {
     case QSM_RETUNE_ERR_NO_MATCHING_RATE:
         reason = tr("none of 48, 96, 24 or 12 kHz are available "
                     "as a nominal rate on this device"); break;
     case QSM_RETUNE_ERR_NOT_SETTABLE:
         reason = tr("the device's sample rate is read-only and "
                     "cannot be changed programmatically"); break;
     case QSM_RETUNE_ERR_SET_FAILED:
         reason = tr("CoreAudio refused the rate change "
                     "(another application may be holding the device)");
         break;
     case QSM_RETUNE_ERR_TIMEOUT:
         reason = tr("the device acknowledged the change but did "
                     "not commit it within 2 seconds"); break;
     case QSM_RETUNE_ERR_DEVICE_NOT_FOUND:
         reason = tr("the device disappeared from CoreAudio's "
                     "device list"); break;
     case QSM_RETUNE_ERR_QUERY_FAILED:
         reason = tr("the device's current sample rate could not "
                     "be read"); break;
     default:
         reason = tr("unexpected error code %1").arg(rc); break;
     }

     QMessageBox::warning(this,
         tr("Could not set %1 device sample rate").arg(direction),
         tr("Could not switch \"%1\" to a 48 kHz-compatible sample "
            "rate. Please open Audio MIDI Setup and set this device "
            "to 48 kHz manually.\n\nDetail: %2")
             .arg(deviceInfo.description())
             .arg(QString::fromUtf8(errBuf).isEmpty()
                  ? reason : QString::fromUtf8(errBuf)));
 }
 #endif

 // Open inDeviceInfo / outDeviceInfo (QtSoundInit at startup, the
 // Devices dialog on a change). On macOS, first set each device's
 // nominal CoreAudio rate to a 12-kHz multiple: the HAL's built-in
 // resampler jitters enough on 44.1 kHz hardware to break AFSK bit
 // recovery. RX and TX on one physical device (common with single-USB
 // radio interfaces) is retuned once.
 void QtSoundModem::reopenQSound()
 {
#if defined(Q_OS_MACOS)
	 retuneDeviceIfNeeded(inDeviceInfo, s_lastWarnedRetuneIn, "input");

	 if (!outDeviceInfo.isNull() &&
	     !inDeviceInfo.isNull() &&
	     outDeviceInfo.id() == inDeviceInfo.id()) {
		 Debugprintf("RX and TX share a CoreAudio UID — output retune skipped.");
		 const QList<QAudioDevice> fresh = QMediaDevices::audioOutputs();
		 for (const QAudioDevice &d : fresh) {
			 if (d.id() == outDeviceInfo.id()) { outDeviceInfo = d; break; }
		 }
	 } else {
		 retuneDeviceIfNeeded(outDeviceInfo, s_lastWarnedRetuneOut, "output");
	 }
#endif

	 initializeAudioOut(outDeviceInfo);
	 initializeAudioIn(inDeviceInfo);
 }

 extern "C" void QtSoundModem::QtSoundInit()
 {
#if defined(Q_OS_MACOS)
	// Probe microphone authorisation. Without permission, QAudioSource
	// returns zeros indefinitely and the modem silently fails to
	// decode. Status codes mirror AVAuthorizationStatus.
	int authStatus = macAudioAuthorisationStatus();
	if (authStatus == 0) // NotDetermined
	{
		// First run — fire the request. The system prompt is async; if
		// the user grants whilst QAudioSource is already open with zero
		// samples, decode begins working within 5–10 s once CoreAudio
		// reports the new permission. The native TCC prompt (driven by
		// NSMicrophoneUsageDescription in Info.plist) is sufficient on
		// its own — no follow-up Qt dialog needed.
		macRequestAudioAuthorisation();
	}
	else if (authStatus == 1 || authStatus == 2) // Restricted or Denied
	{
		// Restricted (1) means policy-blocked (Screen Time, MDM); the
		// system will not even prompt. The remediation surface is the
		// same as Denied — open System Settings — so we share the
		// dialog copy.
		QMessageBox::warning(this, tr("Microphone permission unavailable"),
			tr("QtSoundModem cannot capture audio because microphone "
			   "access is %1. Open System Settings → Privacy & "
			   "Security → Microphone, enable QtSoundModem, then "
			   "restart the app. (If the toggle is locked your device "
			   "may be managed by Screen Time or an MDM profile.)")
			   .arg(authStatus == 1 ? tr("restricted") : tr("denied")));
	}
#endif

	 GetAudioDevices();

	 reopenQSound();

	 DMABuffer = QtDMABuffer;

	 // Hot-unplug + system-default change handling. QMediaDevices
	 // emits audioInputsChanged / audioOutputsChanged when the user
	 // yanks a USB sound dongle or switches the system default.
	 //
	 // Qt::QueuedConnection (rather than the default same-thread
	 // DirectConnection that AutoConnection picks for a same-thread
	 // emitter+receiver) is critical on macOS 26.4.1 with Qt 6.11:
	 // these signals fire from inside QtMultimedia's CoreAudio
	 // listener dispatch (HALPropertyListener::Call →
	 // QPlatformAudioDevices::updateAudioInputsCache → emit). If our
	 // slot runs synchronously inside that callback, calling
	 // m_audioInput->stop() during the teardown branches re-enters
	 // QtMultimedia, which then calls
	 // AudioObjectRemovePropertyListenerBlock on a listener block
	 // that is mid-cache-rebuild — objc_retain dereferences a stale
	 // block pointer and SIGSEGVs. Queuing the slot defers it until
	 // control returns to the receiver thread's event loop, outside
	 // the synchronous CoreAudio/QtMultimedia callback stack that
	 // emitted the signal. Audio device events fire at human
	 // timescales so the one-tick delay is invisible; the cached
	 // device lists we read in the slot (via GetAudioDevices() →
	 // QMediaDevices::audioInputs()) reflect the latest state when
	 // the slot eventually runs.
	 if (!m_mediaDevices)
	 {
		 m_mediaDevices = new QMediaDevices(this);
		 connect(m_mediaDevices, &QMediaDevices::audioInputsChanged,
			 this, &QtSoundModem::onAudioDevicesChanged,
			 Qt::QueuedConnection);
		 connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged,
			 this, &QtSoundModem::onAudioDevicesChanged,
			 Qt::QueuedConnection);
	 }
 }

 void QtSoundModem::onAudioDevicesChanged()
 {
#if defined(Q_OS_MACOS)
	 // A retune in progress is pumping the event loop to let Qt's
	 // CoreAudio backend refresh its cached device formats. Any
	 // queued device-list signal that lands on us during that
	 // window would see m_audioInput/m_audioOutput == nullptr (in
	 // the deviceaccept path the caller has already torn them down)
	 // and hit the auto-reopen branches below, opening streams the
	 // caller is about to open itself — duplicate CoreAudio open.
	 // Defer: the retune guard replays one event when it unwinds.
	 if (s_retuneInProgress) {
		 Debugprintf("onAudioDevicesChanged deferred — retune in progress.");
		 s_devChangeDeferred = true;
		 return;
	 }
#endif

	 // Re-enumerate. Qt 6 does not always emit StoppedState cleanly
	 // when the underlying device vanishes — depending on the macOS
	 // backend the QAudioSource can hang in ActiveState producing
	 // zeros (the silent-zero failure mode). Stop the dead stream
	 // explicitly and clear the cached IODevice pointers so PollQSound
	 // and sendSamplestoQSound short-circuit until the user re-picks.
	 GetAudioDevices();

	 bool inGone = inDeviceInfo.isNull()
		 || !QMediaDevices::audioInputs().contains(inDeviceInfo);
	 bool outGone = outDeviceInfo.isNull()
		 || !QMediaDevices::audioOutputs().contains(outDeviceInfo);

	 // Serialise teardown against the worker-thread TX/RX path.
	 // The auto-reopen calls below run after this scope releases
	 // the lock, so initializeAudio* (which is itself unlocked) is
	 // not at risk of recursive deadlock here. Worker access to the
	 // four audio pointers is also under s_audioMutex, so the
	 // null-then-deleteLater sequence cannot race with a write.
	 {
		 QMutexLocker locker(&s_audioMutex);
		 if (inGone)
		 {
			 if (m_audioInput)
			 {
				 // Disconnect the state-change signal before stop() so a
				 // queued stateChanged delivered after deleteLater() does
				 // not fire on a freed object.
				 disconnect(m_audioInput, &QAudioSource::stateChanged,
					 this, &QtSoundModem::audioInStateChanged);
				 m_audioInput->stop();
				 // The QIODevice returned by QAudioSource::start() is no
				 // longer usable after stop(); null the static cache so
				 // PollQSound's `if (in == nullptr) return;` fires and
				 // we don't read through a dangling pointer.
				 in = nullptr;
				 m_audioInput->deleteLater();
				 m_audioInput = nullptr;
			 }
			 // New capture generation: the worker drops whatever it
			 // buffered from the vanished device.
			 ++s_capGen;
			 inDeviceInfo = QAudioDevice();
		 }
		 if (outGone)
		 {
			 if (m_audioOutput)
			 {
				 disconnect(m_audioOutput, &QAudioSink::stateChanged,
					 this, &QtSoundModem::audioOutStateChanged);
				 m_audioOutput->stop();
				 out = nullptr;
				 m_audioOutput->deleteLater();
				 m_audioOutput = nullptr;
			 }
			 outDeviceInfo = QAudioDevice();
			 // The audioOutStateChanged → IdleState handler usually
			 // clears SoundIsPlaying, but a torn-down sink will not
			 // emit it. Clear here so DoTX's "still playing?" guard
			 // does not wedge transmit until restart.
			 s_sinkIdle.store(1);
		 }
	 }

	 if (inGone || outGone)
	 {
		 Debugprintf("Audio device list changed; active %s%s%s no longer present — stream stopped, re-pick from Devices dialog",
			 inGone ? "input" : "",
			 (inGone && outGone) ? " and " : "",
			 outGone ? "output" : "");
	 }

	 // Replug-same-device: GetAudioDevices() re-matched the saved
	 // device descriptions, so inDeviceInfo / outDeviceInfo are
	 // valid again. If our streams were torn down on a previous
	 // unplug they're still null — reopen automatically so the user
	 // does not have to revisit the Devices dialog.

#if defined(Q_OS_MACOS)
	 // Streams on the affected device(s) were just torn down by the
	 // teardown branches above (or were already null). We are now in
	 // the safe window: device handle valid, no QAudioSource/Sink open
	 // on it. Re-apply the rate policy here before the reopen blocks
	 // below call initializeAudio*. If we retuned during a live stream,
	 // CoreAudio's HAL would interrupt the running graph and may SIGSEGV
	 // in the listener-block path.
	 const bool inAboutToReopen =
		 !inGone && !m_audioInput && !inDeviceInfo.isNull();
	 const bool outAboutToReopen =
		 !outGone && !m_audioOutput && !outDeviceInfo.isNull();

	 const bool sameUid =
		 !inDeviceInfo.isNull() && !outDeviceInfo.isNull() &&
		 inDeviceInfo.id() == outDeviceInfo.id();

	 if (sameUid) {
		 // RX and TX share one physical device. CoreAudio nominal-rate
		 // changes are device-wide, so retuning would also disturb the
		 // *other* direction. Only retune when both directions are
		 // about to be reopened — i.e. neither side has a live stream
		 // on this device.
		 if (inAboutToReopen && outAboutToReopen) {
			 retuneDeviceIfNeeded(inDeviceInfo, s_lastWarnedRetuneIn, "input");
			 Debugprintf("RX and TX share a CoreAudio UID — output retune skipped.");
			 const QList<QAudioDevice> fresh = QMediaDevices::audioOutputs();
			 for (const QAudioDevice &d : fresh) {
				 if (d.id() == outDeviceInfo.id()) { outDeviceInfo = d; break; }
			 }
		 } else if (inAboutToReopen || outAboutToReopen) {
			 Debugprintf("Hot-plug retune skipped: RX and TX share a "
				 "CoreAudio UID and only one direction is about to "
				 "reopen — retuning would disturb the live stream "
				 "on the other side.");
		 }
	 } else {
		 if (inAboutToReopen)
			 retuneDeviceIfNeeded(inDeviceInfo, s_lastWarnedRetuneIn, "input");
		 if (outAboutToReopen)
			 retuneDeviceIfNeeded(outDeviceInfo, s_lastWarnedRetuneOut, "output");
	 }
#endif

	 if (!inGone && !m_audioInput && !inDeviceInfo.isNull())
	 {
		 Debugprintf("Audio input device returned — reinitialising");
		 initializeAudioIn(inDeviceInfo);
	 }
	 if (!outGone && !m_audioOutput && !outDeviceInfo.isNull())
	 {
		 Debugprintf("Audio output device returned — reinitialising");
		 initializeAudioOut(outDeviceInfo);
	 }
 }




 // The downstream modem code (PollQSound, sendSamplestoQSound) hard-codes
 // 12 kHz / 2 channels / Int16 — it casts the captured byte stream to
 // signed shorts and writes n*4-byte chunks of stereo Int16 to the sink.
 // If the device cannot do that exact format, falling back to the
 // device's preferred format will *open* the stream but produce garbage
 // (wrong sample rate or float samples reinterpreted as Int16). The Qt 5
 // version had the same latent bug via nearestFormat(); fixing it
 // properly needs a Qt resampler in the modem feed and is out of scope
 // for the Qt5→Qt6 migration. Surface the mismatch loudly via
 // Debugprintf so it lands in the trace pane.

 // Captured input format. Set during initializeAudioIn. PollQSound
 // decimates to 12 kHz before feeding the modem and broadcasts mono
 // sources to stereo before decimation so the rate-of-time math stays
 // honest (a mono Int16 stream reinterpreted as stereo halves the
 // effective sample rate).
 int g_audioInputRate = 12000;
 int g_audioInputDecim = 1;
 int g_audioInputChannelCount = 2;

 extern "C" void aaFilterInit(int sampleRateIn);

 // Integer multiples of 12000 supported by the input decimator. Anything
 // else falls back to truncated integer decimation (decim = floor(rate/12000))
 // which leaves the modem running at a rate it thinks is 12 kHz but
 // actually isn't: 44.1 kHz → decim=3 → effective 14.7 kHz (22.5% drift,
 // breaks AFSK bit recovery); 88.2 kHz → decim=7 → 12.6 kHz (5%).
 // Upper bound 96 kHz: decimateAudioToModem silences decim > 8 (sized for
 // a 96→12 kHz max), so 192 kHz is *not* whitelisted here even though it
 // divides cleanly — the FIR working buffer would overrun.
 static bool isSafeModemInputRate(int rate)
 {
	 return rate == 12000 || rate == 24000
		 || rate == 48000 || rate == 96000;
 }

 // Output is stricter: sendSamplestoQSound writes the modem's 12 kHz
 // frames directly to the QAudioSink without rate adaptation. A sink
 // opened at any other rate would play TX at the wrong speed (e.g. 48 kHz
 // → 4× fast). Until an output resampler exists, only 12 kHz is safe.
 // CoreAudio normally accepts 12 kHz on every device via auto-resample,
 // so this rarely refuses on macOS.
 static bool isSafeModemOutputRate(int rate)
 {
	 return rate == 12000;
 }

 // INPUT: the downstream modem code reinterprets the byte stream as
 // stereo Int16. A Float preferredFormat would have its 4-byte float
 // values read as pairs of Int16, producing wildly wrong sample values
 // that the demod cannot recover from — refuse outright. Mono Int16 is
 // accepted but PollQSound broadcasts it to stereo before passing to
 // decimateAudioToModem (each mono sample becomes L=R), so the
 // sample-rate math stays correct. >2 channels is refused — we'd need
 // explicit channel-pick logic to know which channel carries the
 // modem audio, and any device offering surround layouts to a USB
 // radio interface is misconfigured for this use.
 static bool isCompatibleAudioInputFormat(const QAudioFormat &fmt)
 {
	 if (fmt.sampleFormat() != QAudioFormat::Int16) return false;
	 const int ch = fmt.channelCount();
	 return ch == 1 || ch == 2;
 }

 // OUTPUT: stricter than input. sendSamplestoQSound writes `n * 4` bytes
 // = exactly stereo Int16 frames, so a mono sink receives every two
 // consecutive sample values as one stereo frame and plays back garbled.
 // Require stereo as well as Int16. CoreAudio normally accepts stereo
 // Int16 on every device via auto-channel-mixing, so this rarely refuses.
 static bool isCompatibleAudioOutputFormat(const QAudioFormat &fmt)
 {
	 return fmt.sampleFormat() == QAudioFormat::Int16
		 && fmt.channelCount() == 2;
 }

 void QtSoundModem::initializeAudioIn(const QAudioDevice &deviceInfo)
 {
	 // See initializeAudioOut: don't open over a live stream.
	 if (m_audioInput)
	 {
		 Debugprintf("initializeAudioIn: input already open; skipped");
		 return;
	 }
	 // Qt 6 dropped QAudioFormat::setSampleSize / setCodec / setByteOrder /
	 // setSampleType. Codec is always PCM, byte order is native, and sample
	 // type + size collapse into a single SampleFormat enum.
	 //
	 // Rate selection: ask for 48 kHz first. CoreAudio's auto-resampler
	 // (when we ask for 12 kHz on a 48 kHz BlackHole) preserves levels
	 // and spectrum but mangles bit-level timing enough to break AFSK
	 // demod. Asking for the device's native 48 kHz and decimating
	 // ourselves with a deterministic filter avoids the SRC entirely.
	 // Fall back to 12 kHz only if 48 kHz isn't supported.

	 QAudioFormat format;
	 format.setChannelCount(2);
	 format.setSampleFormat(QAudioFormat::Int16);

	 // Prefer integer-decimation rates: 48 kHz (×4), 24 kHz (×2),
	 // 12 kHz (×1).
	 const int candidateRates[] = { 48000, 24000, 12000 };
	 g_audioInputRate = 0;
	 for (int candidate : candidateRates)
	 {
		 format.setSampleRate(candidate);
		 if (deviceInfo.isFormatSupported(format))
		 {
			 g_audioInputRate = candidate;
			 break;
		 }
	 }
	 bool usedPreferredFallback = false;
	 if (g_audioInputRate == 0)
	 {
		 format = deviceInfo.preferredFormat();
		 g_audioInputRate = format.sampleRate();
		 usedPreferredFallback = true;
	 }

	 // Per-direction gate to suppress repeat dialogs when multiple init
	 // signals fire for the same bad device. Keyed on QAudioDevice::id()
	 // (opaque, stable per physical device, distinguishes duplicate
	 // descriptions) — same persistence key used by Init/SndRXDeviceId.
	 // Cleared on successful init so a later refusal of a different
	 // device, or this same device after the user fixes its rate,
	 // re-warns.
	 static QByteArray s_lastWarnedInDev;

	 // Refuse rates outside the supported decimator set or formats the
	 // downstream modem code can't interpret. Caller's prior teardown
	 // leaves m_audioInput/in null, so we just early-return without
	 // constructing a stream.
	 const bool rateOk = isSafeModemInputRate(g_audioInputRate);
	 const bool fmtOk = isCompatibleAudioInputFormat(format);
	 if (!rateOk || !fmtOk)
	 {
		 // The fallback to preferredFormat lands here only when we
		 // couldn't get stereo Int16 at any of 48/24/12 kHz AND
		 // the device's preferred format is also unsafe. Surface
		 // the device's offer in the trace before the REFUSED line
		 // so the user can see why we wouldn't open the stream.
		 if (usedPreferredFallback)
			 Debugprintf("Input device offers no stereo Int16 at "
				 "12/24/48 kHz; preferredFormat is %d Hz / %d ch / "
				 "sample-format %d (unusable for the modem).",
				 format.sampleRate(), format.channelCount(),
				 (int)format.sampleFormat());

		 // Drift figure for the rate diagnostic (only meaningful when the
		 // rate is the problem — float-format / mono is reported separately).
		 int decim = (g_audioInputRate < 12000) ? 1 : g_audioInputRate / 12000;
		 if (decim > 8) decim = 8;
		 double effectiveOut = (double)g_audioInputRate / decim;
		 double drift = effectiveOut - 12000.0;
		 if (drift < 0) drift = -drift;
		 double driftPct = drift / 12000.0 * 100.0;

		 Debugprintf("REFUSED: input device '%s' offered %d Hz / %d ch / "
			 "sample-format %d; modem requires Int16 PCM at "
			 "12/24/48/96 kHz. Stream not opened.",
			 deviceInfo.description().toUtf8().constData(),
			 g_audioInputRate, format.channelCount(),
			 (int)format.sampleFormat());

		 const QByteArray deviceKey = deviceInfo.id();
		 if (s_lastWarnedInDev != deviceKey)
		 {
			 s_lastWarnedInDev = deviceKey;
			 QString detail;
			 if (!rateOk)
				 detail = tr("offers %1 Hz, which is not 12, 24, 48 or "
						"96 kHz (truncated decimation would yield "
						"%2 Hz, a %3% timing error)")
					 .arg(g_audioInputRate)
					 .arg((int)effectiveOut)
					 .arg(QString::number(driftPct, 'f', 1));
			 else
				 detail = tr("offers a non-Int16 sample format that "
						"the modem cannot interpret");

			 QMessageBox::warning(this, tr("Audio input not supported"),
				 tr("The input device \"%1\" %2. Decoding has been "
					"disabled for this device.\n\n"
					"Pick an input that supports 48 kHz Int16 PCM "
					"(most do), or change this device's rate in "
					"Audio MIDI Setup.")
				 .arg(deviceInfo.description())
				 .arg(detail));
		 }

		 g_audioInputRate = 12000;
		 g_audioInputDecim = 1;
		 g_audioInputChannelCount = 2;
		 // Clear the device handle so onAudioDevicesChanged's auto-reopen
		 // path doesn't loop on this device on every device-list emit.
		 // The user must re-pick from the Devices dialog (correct, since
		 // we already told them via the modal that this device can't work).
		 inDeviceInfo = QAudioDevice();
		 return;
	 }

	 g_audioInputDecim = g_audioInputRate / 12000;
	 if (g_audioInputDecim < 1) g_audioInputDecim = 1;
	 // Capture the negotiated channel count. PollQSound branches on
	 // this — mono input gets broadcast to stereo before decimation
	 // so the rate math stays correct (a mono Int16 stream
	 // reinterpreted as stereo halves effective time resolution and
	 // the demod silently runs at the wrong baud rate).
	 g_audioInputChannelCount = format.channelCount();

	 // Mono-only USB radio interfaces (e.g. C-Media chipsets) report
	 // mono Int16 as their only supported format. The candidate-rate
	 // loop asks for stereo and gets refused at every rate; we fall
	 // through to preferredFormat which lands on (e.g.) 48 kHz mono.
	 // That's a normal, working path — the modem broadcasts mono to
	 // stereo internally — so log it as info, not a warning.
	 if (usedPreferredFallback)
		 Debugprintf("Input device offers mono only; opened at "
			 "preferredFormat %d Hz / %d ch / sample-format %d "
			 "(modem broadcasts mono → stereo before decimation).",
			 g_audioInputRate, format.channelCount(),
			 (int)format.sampleFormat());

	 qDebug() << "Opening Input Device " << deviceInfo.description();
	 qDebug() << "Sample Rate" << g_audioInputRate
		 << "(decimation factor" << g_audioInputDecim << "to 12 kHz)";

	 // Publish m_audioInput + in under s_audioMutex so the worker-thread
	 // readers in PollQSound see the constructed-and-started object via
	 // the same lock that paired with the previous teardown's release.
	 // Without this, on arm64 the reader's mutex acquire pairs with the
	 // teardown's release but not with these stores, leaving the
	 // publication of the new QAudioSource not happens-before the
	 // worker's read.
	 bool startFailed = false;
	 QAudio::Error startErr = QAudio::NoError;
	 {
		 QMutexLocker locker(&s_audioMutex);
		 // The worker designs the FIR for this rate when it picks the
		 // new generation up.
		 s_capParams = { g_audioInputRate, g_audioInputDecim, g_audioInputChannelCount };
		 ++s_capGen;
		 m_audioInput = new QAudioSource(deviceInfo, format, this);
		 connect(m_audioInput, &QAudioSource::stateChanged, this, &QtSoundModem::audioInStateChanged);

		 // Buffer size scales with rate so PollQSound still gets ~340 ms
		 // of headroom before overrun.
		 m_audioInput->setBufferSize(16384 * g_audioInputDecim);
		 in = m_audioInput->start();

		 // QAudioSource::start() returns nullptr (and/or sets a non-NoError
		 // error) when CoreAudio refuses the stream — device grabbed
		 // exclusively, HAL format mismatch slipping past
		 // isFormatSupported, permission revoked mid-session. The return
		 // value was previously ignored: PollQSound's `in == nullptr`
		 // guard then silently disabled Rx with no log and no dialog, so
		 // the operator just saw a dead waterfall. Detect it here; the
		 // dialog itself is shown after the lock is dropped (a modal
		 // QMessageBox spins the event loop — same reason the REFUSED
		 // dialog above runs outside s_audioMutex).
		 startErr = m_audioInput->error();
		 if (in == nullptr || startErr != QAudio::NoError)
		 {
			 startFailed = true;
			 disconnect(m_audioInput, &QAudioSource::stateChanged,
				 this, &QtSoundModem::audioInStateChanged);
			 m_audioInput->stop();
			 m_audioInput->deleteLater();
			 m_audioInput = nullptr;
			 in = nullptr;
		 }
	 }

	 if (startFailed)
	 {
		 Debugprintf("REFUSED: input device '%s' QAudioSource::start() "
			 "failed (error %d); Rx disabled for this device.",
			 deviceInfo.description().toUtf8().constData(),
			 (int)startErr);

		 // Per-device gate, same as the format-refusal path: warn once
		 // per device so a wedged device doesn't spam modal dialogs.
		 const QByteArray deviceKey = deviceInfo.id();
		 if (s_lastWarnedInDev != deviceKey)
		 {
			 s_lastWarnedInDev = deviceKey;
			 QMessageBox::warning(this, tr("Audio input not supported"),
				 tr("The input device \"%1\" could not be started "
					"(audio system error %2). Decoding has been disabled "
					"for this device.\n\nTry another input, or reconnect "
					"the device and reselect it in the Devices dialog.")
				 .arg(deviceInfo.description())
				 .arg((int)startErr));
		 }
		 return;
	 }

	 // A successful open closes the warning gate — if this same device
	 // later goes back to a refusable rate (or a different bad device
	 // appears), the user gets a fresh dialog rather than silent failure.
	 s_lastWarnedInDev.clear();
 }
 void QtSoundModem::initializeAudioOut(const QAudioDevice &deviceInfo)
 {
	 // A queued device-change replay can open the stream while a caller
	 // that is about to open it sits in a modal dialog; don't open twice.
	 if (m_audioOutput)
	 {
		 Debugprintf("initializeAudioOut: output already open; skipped");
		 return;
	 }
	 QAudioFormat format;
	 format.setSampleRate(12000);
	 format.setChannelCount(2);
	 format.setSampleFormat(QAudioFormat::Int16);

	 qDebug() << "Opening Output Device " << deviceInfo.description();

	 if (!deviceInfo.isFormatSupported(format))
	 {
		 format = deviceInfo.preferredFormat();
		 Debugprintf("WARNING: output device does not support 12 kHz/stereo/Int16; "
			 "preferredFormat is %d Hz / %d ch / sample-format %d.",
			 format.sampleRate(), format.channelCount(),
			 (int)format.sampleFormat());
	 }

	 // Per-direction gate; see initializeAudioIn for the rationale.
	 static QByteArray s_lastWarnedOutDev;

	 // Output is stricter than input: sendSamplestoQSound writes 12 kHz
	 // frames straight to the sink, so anything other than 12 kHz / 2 ch /
	 // Int16 plays TX at the wrong speed or as garbled bytes. CoreAudio
	 // normally accepts 12 kHz on every device via auto-resample, so this
	 // rarely refuses.
	 const bool outRateOk = isSafeModemOutputRate(format.sampleRate());
	 const bool outFmtOk = isCompatibleAudioOutputFormat(format);
	 if (!outRateOk || !outFmtOk)
	 {
		 const int outRate = format.sampleRate();

		 Debugprintf("REFUSED: output device '%s' offered %d Hz / %d ch / "
			 "sample-format %d; modem requires Int16 PCM at 12 kHz. "
			 "Stream not opened.",
			 deviceInfo.description().toUtf8().constData(),
			 outRate, format.channelCount(),
			 (int)format.sampleFormat());

		 const QByteArray deviceKey = deviceInfo.id();
		 if (s_lastWarnedOutDev != deviceKey)
		 {
			 s_lastWarnedOutDev = deviceKey;
			 QString detail;
			 if (!outRateOk)
			 {
				 const double speedRatio = (double)outRate / 12000.0;
				 detail = tr("only offers %1 Hz, not 12 000 Hz "
						"(TX audio would play at %2× speed)")
					 .arg(outRate)
					 .arg(QString::number(speedRatio, 'f', 2));
			 }
			 else if (format.sampleFormat() != QAudioFormat::Int16)
			 {
				 detail = tr("offers a non-Int16 sample format that "
						"the modem cannot produce");
			 }
			 else
			 {
				 detail = tr("offers %1-channel audio, but TX requires "
						"a stereo (2-channel) sink — the modem writes "
						"interleaved L+R frames")
					 .arg(format.channelCount());
			 }

			 QMessageBox::warning(this, tr("Audio output not supported"),
				 tr("The output device \"%1\" %2. TX has been disabled "
					"for this device.\n\n"
					"Pick an output that supports 12 kHz stereo Int16 "
					"PCM (most do — CoreAudio auto-resamples), or pick "
					"a different output device.")
				 .arg(deviceInfo.description())
				 .arg(detail));
		 }
		 // Clear so onAudioDevicesChanged's auto-reopen doesn't loop.
		 outDeviceInfo = QAudioDevice();
		 return;
	 }

	 qDebug() << "Sample Rate" << format.sampleRate();

	 // See initializeAudioIn for the publication-under-lock rationale,
	 // and for why a start() failure is torn down here but reported
	 // only after the lock is dropped.
	 bool startFailed = false;
	 QAudio::Error startErr = QAudio::NoError;
	 {
		 QMutexLocker locker(&s_audioMutex);
		 m_audioOutput = new QAudioSink(deviceInfo, format, this);
		 connect(m_audioOutput, &QAudioSink::stateChanged, this, &QtSoundModem::audioOutStateChanged);

		 m_audioOutput->setBufferSize(16384);
		 int n = m_audioOutput->bufferSize();
		 Debugprintf("Output Buffer Size %d", n);

		 out = m_audioOutput->start();
		 startErr = m_audioOutput->error();
		 if (out == nullptr || startErr != QAudio::NoError)
		 {
			 startFailed = true;
			 disconnect(m_audioOutput, &QAudioSink::stateChanged,
				 this, &QtSoundModem::audioOutStateChanged);
			 m_audioOutput->stop();
			 m_audioOutput->deleteLater();
			 m_audioOutput = nullptr;
			 out = nullptr;
		 }
	 }

	 if (startFailed)
	 {
		 Debugprintf("REFUSED: output device '%s' QAudioSink::start() "
			 "failed (error %d); TX disabled for this device.",
			 deviceInfo.description().toUtf8().constData(),
			 (int)startErr);

		 const QByteArray deviceKey = deviceInfo.id();
		 if (s_lastWarnedOutDev != deviceKey)
		 {
			 s_lastWarnedOutDev = deviceKey;
			 QMessageBox::warning(this, tr("Audio output not supported"),
				 tr("The output device \"%1\" could not be started "
					"(audio system error %2). TX has been disabled "
					"for this device.\n\nTry another output, or reconnect "
					"the device and reselect it in the Devices dialog.")
				 .arg(deviceInfo.description())
				 .arg((int)startErr));
		 }
		 return;
	 }

	 // Successful open — close the warning gate. See initializeAudioIn.
	 s_lastWarnedOutDev.clear();
 }

 // Predicate for the C side (MacBits.c::SoundFlush) to detect that the
 // output sink was never opened (refused by initializeAudioOut, or torn
 // down by hot-unplug). Without it, SoundFlush would wait the full 5 s
 // SoundIsPlaying timeout on every transmit attempt because the
 // audioOutStateChanged slot never fires when there is no QAudioSink.
 extern "C" int isAudioOutputOpen()
 {
	 QMutexLocker locker(&s_audioMutex);
	 return (m_audioOutput && out) ? 1 : 0;
 }

 void QtSoundModem::closeQSound()
 {
	 // Null-safe: onAudioDevicesChanged() may have already torn the
	 // streams down on hot-unplug, leaving these pointers nulled.
	 // The Devices dialog calls closeQSound() before initializeAudio*
	 // for the replacement device, so the null path is normal flow.
	 //
	 // Mirror onAudioDevicesChanged's disconnect+stop+deleteLater+null
	 // teardown. Without this, deviceaccept's subsequent call to
	 // initializeAudio{In,Out} for the new device leaks the previous
	 // QAudioSource/Sink (parented to `this`, so they survive until
	 // window destruction) and a queued stateChanged signal could fire
	 // on the about-to-be-replaced source.
	 QMutexLocker locker(&s_audioMutex);
	 if (m_audioInput)
	 {
		 disconnect(m_audioInput, &QAudioSource::stateChanged,
			 this, &QtSoundModem::audioInStateChanged);
		 m_audioInput->stop();
		 in = nullptr;
		 m_audioInput->deleteLater();
		 m_audioInput = nullptr;
	 }
	 if (m_audioOutput)
	 {
		 disconnect(m_audioOutput, &QAudioSink::stateChanged,
			 this, &QtSoundModem::audioOutStateChanged);
		 m_audioOutput->stop();
		 out = nullptr;
		 m_audioOutput->deleteLater();
		 m_audioOutput = nullptr;
	 }

	 // New capture generation: the worker drops whatever it buffered
	 // for the old stream before touching the next one.
	 ++s_capGen;
 }

 extern "C" void txSleep(int mS);

 // Qt 6 dropped QAudioSink::periodSize() / QAudioSource::periodSize().
 // 1024 bytes = 256 stereo Int16 frames at 12 kHz, ~21 ms. Twice the
 // 512-byte block PollQSound slices Buffer into; large enough to
 // amortise the per-read overhead, small enough that the worker-thread
 // poll cadence stays responsive.
 static const int kAudioPeriodBytes = 1024;

// Set when a TX chunk aborts because the sink stopped or wedged; the
// rest of that transmission's chunks then return at once instead of
// each waiting another 2 s with PTT keyed. Cleared at the next PTT-on
// (RadioPTT -> StartWatchdog -> qtAudioTxStart). Atomic: in UDP-server mode RadioPTT and
// SendtoCard also run on the GUI thread.
static std::atomic<bool> s_txAborted{false};

extern "C" void qtAudioTxStart()
{
	s_txAborted = false;
}

 extern "C" unsigned short * sendSamplestoQSound(unsigned short * buf, int n)
 {
	 if (s_txAborted)
		 return buf;

	 // Hot-unplug guard: onAudioDevicesChanged / closeQSound null
	 // m_audioOutput and out under s_audioMutex when the active
	 // device disappears or is replaced. We hold the lock across
	 // every read/write of those pointers (and across the Qt calls
	 // they target — bytesFree, write — which are non-blocking),
	 // and we DROP the lock for txSleep so a concurrent teardown
	 // can run while we wait.
	 int space;
	 {
		 QMutexLocker locker(&s_audioMutex);
		 if (!m_audioOutput || !out)
			 return buf;
		 space = m_audioOutput->bytesFree();
	 }
	 const int size = kAudioPeriodBytes;
	 int chunks = space / size;

	 Debugprintf("ToSend %d Space %d Period Size %d chunks %d ", n * 4, space, size, chunks);

	 // We are passed Stereo 16 bit samples. I think n is number of samples so send n x 4 
	 //
	 // The busy-wait below previously had no error / state check and no
	 // timeout: if QAudioSink entered StoppedState with a non-NoError
	 // status while a Tx was in flight (USB unplug mid-Tx, CoreAudio
	 // device-lost, sample-rate change rejected by the device), the
	 // worker spun forever in txSleep(10). PTT was keyed before the
	 // first sample (SMMain.c:873) and RadioPTT(Chan, 0) was never
	 // reached. Radio sat keyed on dead air until the user killed the
	 // app.
	 //
	 // Now: exit the loop if the sink reports StoppedState / an error,
	 // or if 2 s elapses without bytesFree() reaching n (a backstop for
	 // the device-wedge case where state() never advances). Returning
	 // early lets SendtoCard return; SoundFlush's IdleState wait fires
	 // its 1 s timeout, clears SoundIsPlaying, and SMMain.c's DoTX
	 // drops PTT cleanly on the next pass.
	 const unsigned int kStuckMs = 2000;
	 unsigned int waitStartedMs = getTicks();
	 for (;;)
	 {
		 int frames;
		 QAudio::State state;
		 QAudio::Error err;
		 {
			 QMutexLocker locker(&s_audioMutex);
			 if (!m_audioOutput || !out)
				 return buf;
			 frames = m_audioOutput->bytesFree() / 4;
			 state = m_audioOutput->state();
			 err = m_audioOutput->error();
		 }
		 if (frames >= n)
			 break;
		 if (state == QAudio::StoppedState || err != QAudio::NoError)
		 {
			 Debugprintf("sendSamplestoQSound: sink stopped (state=%d err=%d) — aborting Tx", (int)state, (int)err);
			 s_txAborted = true;
			 return buf;
		 }
		 if (getTicks() - waitStartedMs > kStuckMs)
		 {
			 Debugprintf("sendSamplestoQSound: bytesFree wedged at %d after %u ms (need %d) — aborting Tx", frames, getTicks() - waitStartedMs, n);
			 s_txAborted = true;
			 return buf;
		 }
		 txSleep(10);
		 Debugprintf("Space %d", frames);
	 }

	 // Software TX attenuation. Plain int snapshot — see UZ7HOStuff.h
	 // comment on txAudioLevel/rxAudioLevel for the threading argument.
	 // Scale in place on the caller's DMABuffer; producers in SMMain.c
	 // / MacBits.c reset or advance after SendtoCard rather than re-reading
	 // the sent buffer (verified during plan review). Since txLvl ≤ 100,
	 // |sample * txLvl / 100| ≤ |sample| ≤ 32767 — no saturation needed.
	 const int txLvl = __atomic_load_n(&txAudioLevel, __ATOMIC_RELAXED);	// GUI slider writes it
	 if (txLvl != 100)
	 {
		 short * s = (short *)buf;
		 const int total = n * 2;       // n stereo frames = 2*n int16 samples
		 for (int k = 0; k < total; k++)
		 {
			 const int v = (int)s[k] * txLvl;
			 s[k] = (short)((v + (v >= 0 ? 50 : -50)) / 100);
		 }
	 }

	 int x;
	 {
		 QMutexLocker locker(&s_audioMutex);
		 if (!m_audioOutput || !out)
			 return buf;
		 x = out->write((char *)buf, n * 4);
		 space = m_audioOutput->bytesFree();
	 }
	 chunks = space / size;

	 Debugprintf("Space %d Period Size %d chunks %d ", space, size, chunks);

	 if (x != n * 4)
		 Debugprintf("%d %d", x, space);

	 return buf;
 }


extern "C" void ProcessNewSamples(short * Samples, int nSamples);

static int minL = 0, maxL = 0, minR = 0, maxR = 0, lastlevelGUI = 0, lastlevelreport = 0;


// Buffer scales with decimation: at 48 kHz we need 4× the bytes
// per output chunk. Size to hold ~64 KiB of the highest input rate.
char Buffer[65536];

int BufferLen = 0;


#if defined(Q_OS_MACOS)
// --dump-input <path> support: write the captured Qt audio to a
// WAV file so we can decode-test it against the same modem code via
// --decode-wav. WAV format: 12 kHz, 2 channels, Int16, little-endian.
extern "C" char * g_dumpInputPath;
static FILE * s_dumpFile = nullptr;
static long s_dumpDataBytes = 0;

static void dumpInputOpen(int sampleRate)
{
	if (s_dumpFile || !g_dumpInputPath) return;
	s_dumpFile = fopen(g_dumpInputPath, "wb");
	if (!s_dumpFile) {
		Debugprintf("dump-input: open %s failed", g_dumpInputPath);
		return;
	}
	const unsigned int byteRate = (unsigned int)sampleRate * 4;  // 2ch * 2byte
	// Canonical 44-byte WAV header. Sizes patched by dumpInputWrite.
	unsigned char hdr[44] = {
		'R','I','F','F', 0,0,0,0,
		'W','A','V','E', 'f','m','t',' ',
		16,0,0,0,           // fmt chunk size
		1,0,                // PCM
		2,0,                // 2 channels
		(unsigned char)(sampleRate & 0xFF), (unsigned char)((sampleRate>>8)&0xFF),
		(unsigned char)((sampleRate>>16)&0xFF), (unsigned char)((sampleRate>>24)&0xFF),
		(unsigned char)(byteRate & 0xFF), (unsigned char)((byteRate>>8)&0xFF),
		(unsigned char)((byteRate>>16)&0xFF), (unsigned char)((byteRate>>24)&0xFF),
		4,0,                // block align (2ch * 2byte)
		16,0,               // bits per sample
		'd','a','t','a', 0,0,0,0
	};
	fwrite(hdr, 1, 44, s_dumpFile);
	s_dumpDataBytes = 0;
	Debugprintf("dump-input: writing to %s @ %d Hz", g_dumpInputPath, sampleRate);
}

static void put32(FILE * f, long pos, long v)
{
	const unsigned char b[4] = {
		(unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF),
		(unsigned char)((v >> 16) & 0xFF), (unsigned char)((v >> 24) & 0xFF) };
	fseek(f, pos, SEEK_SET);
	fwrite(b, 1, 4, f);
}

// Worker thread only. The RIFF/data sizes are patched after every chunk
// and the file flushed, so the WAV is valid whenever the app exits;
// there is no close path to race the worker at shutdown.
static void dumpInputWrite(const void * data, size_t bytes)
{
	if (!s_dumpFile) return;
	fwrite(data, 1, bytes, s_dumpFile);
	s_dumpDataBytes += bytes;
	put32(s_dumpFile, 4, 36 + s_dumpDataBytes);
	put32(s_dumpFile, 40, s_dumpDataBytes);
	fseek(s_dumpFile, 0, SEEK_END);
	fflush(s_dumpFile);
}
#endif

// 127-tap Hamming-windowed sinc low-pass for the native-rate → 12 kHz
// decimation path. Replaces the previous 4-tap boxcar that the README
// flagged as a known limitation. Linear-phase Type I (odd taps), exact
// integer group delay 63 samples (5.25 ms at 12 kHz, well inside any
// DCD/PTT margin). Coefficients are recomputed on every aaFilterInit
// call (cheap; called once at audio-init or device change). Float
// coefficients precomputed at init; per-sample cost is one MAC × 127
// taps × 12 kHz × 2 channels ≈ 3 Mflops — negligible on Apple Silicon.
//
// Cutoff is 4500 Hz absolute, normalized per actual input sample rate
// in aaFilterInit (fc = 4500/sampleRateIn) so the passband is 4.5 kHz
// regardless of decim. Stopband ≥ 50 dB at 6 kHz (Hamming-windowed
// sinc, ~53 dB peak sidelobe) — enough to suppress the 5–7 kHz alias
// band that bit RUH96 / IL2P decode under boxcar. At decim=2 (24 kHz
// native) the transition band sits closer to output Nyquist than at
// 48 kHz; tested fine for the supported modems but 48 kHz native is
// the design point.
//
// For decim==1 (12 kHz native) we'd be filtering inside the modem's
// useful band; short-circuit to a straight stereo memcpy instead.
// For non-integer-decim rates (44.1 kHz → decim=3 truncated) the
// timing drift is the dominant problem, not aliasing — the existing
// initializeAudioIn warning still applies; the FIR still runs and
// helps reject HF content but does not fix the rate mismatch.

#include <math.h>

#define FIR_TAPS 127
static float fir_coeffs[FIR_TAPS];
static int fir_designed_rate = 0;  // 0 = needs design
static float fir_histL[FIR_TAPS - 1];
static float fir_histR[FIR_TAPS - 1];


extern "C" void aaFilterInit(int sampleRateIn)
{
	// Defensive clamp: a misbehaving device negotiating 0 or a
	// negative rate would otherwise give fc = 4500/0 = inf and
	// poison the coefficients with NaN. Caller-side
	// initializeAudioIn clamps decim but not the rate; matching
	// the style of the decim>8 guard in decimateAudioToModem.
	if (sampleRateIn < 12000)
		sampleRateIn = 12000;

	// Called on the worker thread only (PollQSound on a new capture
	// generation, and the --decode-wav harnesses), the thread that owns
	// the FIR state. Flush the history on every (re)open, even at an
	// unchanged rate: its FIR_TAPS-1 samples belong to the previous
	// stream and would otherwise click into the first chunk.
	for (int i = 0; i < FIR_TAPS - 1; i++)
	{
		fir_histL[i] = 0.0f;
		fir_histR[i] = 0.0f;
	}

	if (sampleRateIn == fir_designed_rate)
		return;  // coefficients already valid for this rate
	fir_designed_rate = sampleRateIn;

	const int M = FIR_TAPS - 1;  // 126
	const double fc = 4500.0 / (double)sampleRateIn;  // normalized cutoff
	double sum = 0.0;
	for (int n = 0; n <= M; n++)
	{
		double t = (double)n - (double)M / 2.0;
		double sinc;
		if (t == 0.0)
			sinc = 2.0 * fc;
		else
			sinc = sin(2.0 * M_PI * fc * t) / (M_PI * t);
		double w = 0.54 - 0.46 * cos(2.0 * M_PI * n / (double)M);  // Hamming
		fir_coeffs[n] = (float)(sinc * w);
		sum += fir_coeffs[n];
	}
	// Normalise so DC gain = 1.0 (windowing perturbs the integral).
	for (int n = 0; n < FIR_TAPS; n++)
		fir_coeffs[n] = (float)(fir_coeffs[n] / sum);

	Debugprintf("Antialias FIR: %d taps, cutoff %.0f Hz at %d Hz input "
		"(normalised fc=%.4f, group delay %d samples)",
		FIR_TAPS, fc * sampleRateIn, sampleRateIn, fc, M / 2);
}

static inline short fir_clip16(float x)
{
	if (x >= 32767.0f) return 32767;
	if (x <= -32768.0f) return -32768;
	return (short)lrintf(x);
}

// Decimate one chunk of interleaved stereo Int16 input down by integer
// factor `decim`, producing 512 stereo output frames. Input must be
// 512 * decim stereo frames. Both PollQSound (live audio) and
// debugDecodeWav (--decode-wav harness) route through this so the
// harness exercises the same DSP the live build does.
extern "C" void decimateAudioToModem(const short * src, int decim, short * dst)
{

	if (decim <= 1)
	{
		// Fast path: 12 kHz native, filtering would hit the modem's
		// useful band. Straight stereo memcpy.
		memcpy(dst, src, 512 * 2 * sizeof(short));
		return;
	}

	if (decim > 8)
	{
		// Working buffers below are sized for decim <= 8 (96 kHz → 12 kHz).
		// Anything past that is unsupported — fall back to silence rather
		// than write past the buffer. PollQSound and debugDecodeWav both
		// reject decim > 8 upstream, so this is belt-and-braces.
		static int warned = 0;
		if (!warned) {
			warned = 1;
			Debugprintf("decimateAudioToModem: decim=%d > 8 unsupported; output silenced", decim);
		}
		memset(dst, 0, 512 * 2 * sizeof(short));
		return;
	}

	if (fir_designed_rate == 0)
	{
		// Coefficients have never been designed (no aaFilterInit call
		// on this caller's path). Convolving with the zero-initialised
		// fir_coeffs[] would emit pure silence and kill every
		// co-running FSK channel that item-5 routes through here. On
		// the macOS port both capture entry points — Qt
		// initializeAudioIn and the debugDecodeWav (--decode-wav*)
		// harness — call aaFilterInit before any BufferFull, so this
		// never fires there; it is a safety net for the non-Qt
		// capture paths (Linux ALSA / Windows WaveOut) that don't.
		// Degrade to the legacy crude every-`decim` stereo pick:
		// aliased, but audible and decodable — strictly better than
		// silence. (Codex second-reviewer catch on BUG-rx-audit
		// item 5.)
		static int warnedNoFir = 0;
		if (!warnedNoFir) {
			warnedNoFir = 1;
			Debugprintf("decimateAudioToModem: FIR not designed "
				"(aaFilterInit not called) — using crude decimation");
		}
		for (int i = 0; i < 512; i++)
		{
			dst[i * 2]     = src[i * decim * 2];
			dst[i * 2 + 1] = src[i * decim * 2 + 1];
		}
		return;
	}

	const int inFrames = 512 * decim;

	// Working buffer: history (FIR_TAPS-1 frames) + current chunk.
	// Sized for the max supported decim=8 (96 kHz → 12 kHz). Static
	// to avoid 33 KB of stack churn on every invocation.
	static float bufL[FIR_TAPS - 1 + 512 * 8];
	static float bufR[FIR_TAPS - 1 + 512 * 8];

	// Prepend the saved history and copy the new input as float.
	memcpy(bufL, fir_histL, (FIR_TAPS - 1) * sizeof(float));
	memcpy(bufR, fir_histR, (FIR_TAPS - 1) * sizeof(float));
	for (int i = 0; i < inFrames; i++)
	{
		bufL[(FIR_TAPS - 1) + i] = (float)src[i * 2];
		bufR[(FIR_TAPS - 1) + i] = (float)src[i * 2 + 1];
	}

	// Convolve and decimate. Output sample i corresponds to input
	// position i*decim; the convolution window covers the FIR_TAPS
	// inputs ending at that position.
	for (int i = 0; i < 512; i++)
	{
		const int xi0 = i * decim;  // index into bufL/bufR of first tap
		float accL = 0.0f, accR = 0.0f;
		for (int n = 0; n < FIR_TAPS; n++)
		{
			accL += fir_coeffs[n] * bufL[xi0 + n];
			accR += fir_coeffs[n] * bufR[xi0 + n];
		}
		dst[i * 2]     = fir_clip16(accL);
		dst[i * 2 + 1] = fir_clip16(accR);
	}

	// Save the trailing FIR_TAPS-1 input samples as history for the
	// next chunk. The convolution at the start of next chunk reads
	// this history at positions [0 .. FIR_TAPS-2], so we copy from
	// the tail of bufL/bufR into the history slot.
	memcpy(fir_histL, &bufL[inFrames], (FIR_TAPS - 1) * sizeof(float));
	memcpy(fir_histR, &bufR[inFrames], (FIR_TAPS - 1) * sizeof(float));
}

extern "C" void PollQSound()
{
	// Re-entry guard. The drain loop below calls ProcessNewSamples ->
	// BufferFull -> chk_dcd1 -> RX2TX -> DoTX, and TX's txSleep polls
	// capture again. Buffer, BufferLen and the carry belong to the
	// outer call, so a nested call must not touch them (it used to,
	// driving BufferLen negative). It only keeps the device drained;
	// RX is discarded during TX anyway.
	static int s_inPoll = 0;
	if (s_inPoll)
	{
		QMutexLocker locker(&s_audioMutex);
		if (m_audioInput && in)
		{
			static char scratch[16384];
			// s_capParams describes the stream `in` belongs to.
			const qint64 frame = (s_capParams.channels == 1) ? 2 : 4;
			qint64 n = in->bytesAvailable();
			n -= n % frame;		// whole frames only
			while (n > 0)
			{
				const qint64 r = in->read(scratch, qMin(n, (qint64)sizeof(scratch)));
				if (r <= 0)
					break;
				n -= r;
			}
		}
		return;
	}

	// Process any captured samples
	// Ideally call at least every 100 mS, more than 200 will loose data

	// For level display we want a fairly rapid level average but only want to report 
	// to log every 10 secs or so

	// Each output chunk = 512 stereo Int16 frames at 12 kHz =
	// 2048 bytes. Input chunk scales by decimation:
	//   decim=1 (12 kHz):  2048 bytes
	//   decim=2 (24 kHz):  4096 bytes
	//   decim=4 (48 kHz):  8192 bytes
	// Carry for a sub-frame tail. QAudioSource delivers whole frames
	// in practice, but QIODevice::read carries no such guarantee. If a
	// backend ever returns a non-frame-aligned byte count, dropping
	// the remainder does NOT restore alignment: the device's byte
	// stream is contiguous, so its next bytes continue the same
	// logical frame — discarding our side desyncs framing for the
	// rest of the session. Instead hold the <frameBytes leftover here
	// and prepend it to the next read so the device stream stays
	// byte-contiguous. (Codex second-reviewer catch on
	// BUG-rx-audit item 6.)
	qtAudioConsumeSinkIdle();

	static char s_partialTail[4];   // frameBytes is at most 4
	static int  s_partialTailLen = 0;

	// The worker's view of the capture stream, and the generation it
	// belongs to. Picking up a new generation drops everything buffered
	// from the previous stream and redesigns/flushes the FIR, all on
	// this thread (see s_capGen).
	static CaptureParams s_work = { 12000, 1, 2 };
	static unsigned s_appliedGen = 0;
	bool newStream = false;
	{
		QMutexLocker locker(&s_audioMutex);
		const unsigned gen = s_capGen.load();
		if (gen != s_appliedGen)
		{
			s_appliedGen = gen;
			s_work = s_capParams;
			BufferLen = 0;
			s_partialTailLen = 0;
			newStream = true;
		}
	}
	if (newStream)
		aaFilterInit(s_work.rate);

	const int decim = s_work.decim;
	const int outChunkBytes = 2048;
	const int inChunkBytes = outChunkBytes * decim;

	// Lock only across the audio-pointer reads and the in->read()
	// call. The decimation/processing loop below operates on the
	// captured Buffer and doesn't touch the audio objects, so
	// holding the lock there would needlessly block teardown.
	// For mono input, request half the bytes (one mono sample per
	// output stereo frame instead of two), then expand in place to
	// stereo before the decimator runs. Without this step the modem
	// would read the mono byte stream as stereo, halving the
	// effective sample rate and silently running every demod at
	// half its intended baud — what the AFSK demod was getting
	// away with on Stuart's CM108 is a coincidence of audio centre
	// frequency, not a property of the data path.
	const bool monoInput = (s_work.channels == 1);

	// Frame size in the raw, pre-mono-expansion byte domain: one
	// int16 for mono input, an L/R pair for stereo.
	const qint64 frameBytes =
		monoInput ? (qint64)sizeof(short) : (qint64)(2 * sizeof(short));



	qint64 x;
	{
		QMutexLocker locker(&s_audioMutex);
		// Stream changed since we applied its parameters above: read
		// nothing now; the next call picks the new generation up.
		if (s_capGen.load() != s_appliedGen)
			return;
		if (!m_audioInput)
		{
			s_partialTailLen = 0;   // stale across a device change
			return;
		}

		if (in == nullptr)
		{
			s_partialTailLen = 0;
			return;
		}

		// `size` is the post-expansion (stereo) byte budget. Mono reads
		// half that and the expansion below fills in the missing L/R
		// pairs. The Buffer-overflow clamp uses the post-expansion
		// figure so the expansion can't run past the end.
		int size = kAudioPeriodBytes * decim;
		if (BufferLen + size > (int)sizeof(Buffer))
			size = (int)sizeof(Buffer) - BufferLen;

		// Prepend any carried sub-frame tail, then read after it so
		// [carry][new bytes] reconstructs the device's contiguous
		// stream. The read budget is reduced by the prepend so the
		// post-expansion total still fits the overflow clamp.
		if (s_partialTailLen > 0)
			memcpy(&Buffer[BufferLen], s_partialTail, s_partialTailLen);

		const int budget = (monoInput ? size / 2 : size) - s_partialTailLen;
		const int requestSize = budget > 0 ? budget : 0;
		x = in->read(&Buffer[BufferLen] + s_partialTailLen, requestSize);
	}

	if (x > 0)
	{
		// The carried bytes are now contiguous with the freshly read
		// bytes and part of this chunk.
		x += s_partialTailLen;
		s_partialTailLen = 0;

		const qint64 rem = x % frameBytes;
		if (rem)
		{
			// Stash the trailing partial frame for the next call;
			// process only the whole-frame prefix now.
			static int warnedPartial = 0;
			if (!warnedPartial)
			{
				warnedPartial = 1;
				Debugprintf("PollQSound: backend returned a non-frame-"
					"aligned read; carrying %lld byte(s) to next read "
					"(mono=%d)", (long long)rem, (int)monoInput);
			}
			memcpy(s_partialTail, &Buffer[BufferLen + x - rem], (size_t)rem);
			s_partialTailLen = (int)rem;
			x -= rem;
		}
	}
	// else: read returned nothing — leave s_partialTail/Len intact so
	// the carried bytes are re-prepended on the next call.

	// QIODevice::read returns -1 on error (device gone, hot-unplug
	// race) and 0 if no data was available. Either way, leave
	// BufferLen alone — adding -1 would walk &Buffer[-1] into the
	// heap on the next read.
	if (x > 0)
	{
		if (monoInput)
		{
			// Walk the mono samples backwards so each write lands at
			// a position the loop has not yet sourced from. After this,
			// `x` doubled bytes occupy [BufferLen, BufferLen + 2x).
			short * base = (short *)&Buffer[BufferLen];
			const int monoSamples = (int)(x / sizeof(short));
			for (int i = monoSamples - 1; i >= 0; i--)
			{
				short s = base[i];
				base[i * 2]     = s;
				base[i * 2 + 1] = s;
			}
			x *= 2;
		}
		BufferLen += (int)x;

		// Software RX attenuation. Applied here, before any path branches,
		// so both the FIR-decimated 12 kHz path and the native-rate RUH/dw9600
		// path (which bypasses decimateAudioToModem) see the same gain.
		// Sign-aware rounding minimises low-bit truncation at low levels.
		const int rxLvl = __atomic_load_n(&rxAudioLevel, __ATOMIC_RELAXED);	// GUI slider writes it
		if (rxLvl != 100)
		{
			short * s = (short *)&Buffer[BufferLen - x];
			const int samples = (int)(x / sizeof(short));
			for (int k = 0; k < samples; k++)
			{
				const int v = (int)s[k] * rxLvl;
				s[k] = (short)((v + (v >= 0 ? 50 : -50)) / 100);
			}
		}
	}

	// Earlier code had an early-return when bytesAvailable() exceeded
	// 16384*decim, intended as a "let it drain on subsequent calls"
	// throttle. It was self-defeating: we are the consumer, so Qt's
	// backlog only shrinks when we drain locally. Returning without
	// draining left Buffer to fill until size→0, then we silently
	// stopped reading altogether. The drain loop below is bounded
	// (one chunk per iteration, finite Buffer) and cheap.

	short decimated[1024];  // 512 stereo frames

	// RUH/dw9600 demod is hard-wired at 48 kHz baseband. The FIR-decimated
	// 12 kHz path destroys G3RUH timing — confirmed by the
	// --decode-wav-native harness (commit c7cf301), which decodes RUH
	// content correctly only by feeding raw 48 kHz to BufferFull. The
	// live path needs the same routing: when `using48000` is set (any
	// RUH48/RUH96 modem active) AND the device is genuinely at 48 kHz,
	// hand ProcessNewSamples the native stream and let BufferFull's
	// runModems branch downsample 4× internally for FSK channels while
	// leaving the RUH branch at native rate. If a RUH modem is selected
	// but the device only offers 12 kHz (decim != 4), RUH won't work
	// either way — fall through to the FIR path so non-RUH channels at
	// least decode.
	const bool nativeRUHPath = (using48000 && decim == 4);

	s_inPoll = 1;
	while (BufferLen >= inChunkBytes)
	{
		// Stop on a device change mid-drain; the next call drops the
		// remaining old-stream chunks when it applies the generation.
		if (s_capGen.load() != s_appliedGen)
			break;

		short * src = (short *)Buffer;
		short * processed;
		int     processedFrames;

		if (nativeRUHPath)
		{
			processed       = src;
			processedFrames = 2048;  // 48 kHz stereo, one PollQSound chunk
		}
		else
		{
			decimateAudioToModem(src, decim, decimated);
			processed       = decimated;
			processedFrames = 512;
		}

		// Level tracking on whatever we are actually feeding the modem.
		// Stereo frame loop regardless of rate — peaks are peaks.
		for (int i = 0; i < processedFrames; i++)
		{
			short outL = processed[2 * i];
			short outR = processed[2 * i + 1];
			if (outL < minL) minL = outL;
			else if (outL > maxL) maxL = outL;
			if (outR < minR) minR = outR;
			else if (outR > maxR) maxR = outR;
		}

		CurrentLevel  = ((maxL - minL) * 75) / 32768;
		CurrentLevelR = ((maxR - minR) * 75) / 32768;

		if ((Now - lastlevelGUI) > 2000)
		{
			lastlevelGUI = Now;

			if ((Now - lastlevelreport) > 60000)
			{
				lastlevelreport = Now;
				if (UsingBothChannels)
					Debugprintf("Input peaks L= %d, %d, R= %d, %d", minL, maxL, minR, maxR);
				else
					Debugprintf("Input peaks = %d, %d", minL, maxL);
			}
			minL = maxL = minR = maxR = 0;
		}

#if defined(Q_OS_MACOS)
		// Dump BEFORE ProcessNewSamples. BufferFull mutates the input
		// buffer in place on the using48000=1 path (its 48->12 kHz
		// reduction rewrites the first quarter of Samples[] with the
		// downsampled 12 kHz frames it just produced — see
		// sm_main.c::BufferFull, the `if (using48000)` block), so
		// dumping after would capture a hybrid of
		// downsampled-leading-frames and raw-trailing-frames at a
		// 48 kHz sample-rate header — useless.
		// The decimated path is safe in either order (BufferFull doesn't
		// touch the buffer when using48000=0), but doing the dump
		// uniformly up front keeps the rule simple.
		// dumpInputOpen() is no-op after the first call, so the
		// rate-at-first-call wins for the lifetime of the dump file —
		// matches the live-audio assumption that RUH/non-RUH mode
		// doesn't change mid-capture.
		if (g_dumpInputPath)
		{
			dumpInputOpen(nativeRUHPath ? 48000 : 12000);
			dumpInputWrite(processed, processedFrames * 2 * sizeof(short));
		}
#endif

		// Note: BufferFull reads the global `using48000` directly while
		// `nativeRUHPath` is captured once per PollQSound call. If the
		// user reconfigures a modem into/out of RUH mode mid-call, the
		// routing here and BufferFull's interpretation could disagree
		// for one chunk. Accepted as a known minor inconsistency —
		// modem reconfiguration restarts audio anyway, so the window
		// is small.
		ProcessNewSamples(processed, processedFrames);

		BufferLen -= inChunkBytes;
		memmove(Buffer, Buffer + inChunkBytes, BufferLen);
	}
	s_inPoll = 0;
}



