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

// UZ7HO Soundmodem Port

// Not Working 4psk100 FEC 

// Thoughts on Waterfall Display.

// Original used a 2048 sample FFT giving 5.859375 Hz bins. We plotted 1024 points, giving a 0 to 6000 specrum

// If we want say 300 to 3300 we need about half the bin size so twice the fft size. But should we also fit required range to window size?

// Unless we resize the most displayed bit of the screen in around 900 pixels. So each bin should be 3300 / 900 = 3.66667 Hz or a FFT size of around 3273

#include "QtSoundModem.h"
#include <qheaderview.h>
//#include <QDebug>
#include <QHostAddress>
#include <QAbstractSocket>
#include <QSettings>
#include <QPainter>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QMessageBox>
#include <QTimer>
#include <qevent.h>
#include <QStandardItemModel>
#include <QScrollBar>
#include <QFontDatabase>
#include <QFile>
#include <QMutex>
#include <QStyleHints>

#include "UZ7HOStuff.h"

#include <time.h>

QDialog * constellationDialog;
QImage *Constellation[4];
QImage *Waterfall = 0;
QLabel *DCDLabel[4];
QLineEdit *chanOffsetLabel[4];
QImage *DCDLed[4];

QImage *RXLevel;
QImage *RXLevel2;

QLabel *WaterfallCopy;

QLabel * RXLevelCopy;
QLabel * RXLevel2Copy;

QTextEdit * monWindowCopy;

extern workerThread *t;
extern QtSoundModem * w;
extern QCoreApplication * a;
extern serialThread *serial;

QList<QSerialPortInfo> Ports = QSerialPortInfo::availablePorts();

void saveSettings();
void getSettings();
void DrawModemFreqRange();

extern "C" void CloseSound();
extern "C" void GetSoundDevices();
extern "C" char modes_name[modes_count][21];
extern "C" int speed[5];
extern "C" int KISSPort;
extern "C" short rx_freq[5];

extern "C" int CaptureCount;
extern "C" int PlaybackCount;

extern "C" int CaptureIndex;		// Card number
extern "C" int PlayBackIndex;

extern "C" char CaptureNames[256][256];
extern "C" char PlaybackNames[256][256];

extern "C" int SoundMode;
extern "C" bool onlyMixSnoop;

extern "C" int multiCore;

extern "C" int refreshModems;
int NeedPSKRefresh;

extern "C" int pnt_change[5];
extern "C" int needRSID[4];

extern "C" int needSetOffset[4];

extern "C" float MagOut[4096];
extern "C" float MaxMagOut;
extern "C" int MaxMagIndex;


extern "C" int using48000;			// Set if using 48K sample rate (ie RUH Modem active)
extern "C" int ReceiveSize;
extern "C" int SendSize;		// 100 mS for now

extern "C" int txLatency;

extern "C" int BusyDet;

extern "C"
{ 
	int InitSound(BOOL Report);
	void soundMain();
	void MainLoop();
	void modulator(UCHAR snd_ch, int buf_size);
	void SampleSink(int LR, short Sample);
	void doCalib(int Port, int Act);
	int Freq_Change(int Chan, int Freq);
	void set_speed(int snd_ch, int Modem);
	void init_speed(int snd_ch);
	void FourierTransform(int NumSamples, short * RealIn, float * RealOut, float * ImagOut, int InverseTransform);
	void dofft(short * in, float * outr, float * outi);
	void init_raduga();
	void DrawFreqTicks();
	void AGW_Report_Modem_Change(int port);
	char * strlop(char * buf, char delim);
	void sendRSID(int Chan, int dropTX);
	void RSIDinitfft();
	void il2p_init(int il2p_debug);
	void closeTraceLog();
}

void make_graph_buf(float * buf, short tap, QPainter * bitmap);

int ModemA = 2;
int ModemB = 2;
int ModemC = 2;
int ModemD = 2;
int FreqA = 1500;
int FreqB = 1500;
int FreqC = 1500;
int FreqD = 1500;
int DCD = 50;

char CWIDCall[128] = "";
extern "C" char CWIDMark[32];
int CWIDInterval = 0;
int CWIDLeft = 0;
int CWIDRight = 0;
int CWIDType = 1;			// on/off
bool afterTraffic = 0;
bool cwidtimerisActive = false;

int WaterfallMin = 00;
int WaterfallMax = 6000;

int Configuring = 0;
bool lockWaterfall = false;
bool inWaterfall = false;

int MgmtPort = 0;
int RHPPort = 9000;
bool RHPServ = 0;

int txAudioLevel = 100;
int rxAudioLevel = 100;

extern "C" int NeedWaterfallHeaders;
extern "C" float BinSize;

extern "C" { int RSID_SABM[4]; }
extern "C" { int RSID_UI[4]; }
extern "C" { int RSID_SetModem[4]; }
extern "C" unsigned int pskStates[4];

int Closing = FALSE;				// Set to stop background thread

QRgb white = qRgb(255, 255, 255);
QRgb black = qRgb(0, 0, 0);

QRgb green = qRgb(0, 255, 0);
QRgb red = qRgb(255, 0, 0);
QRgb yellow = qRgb(255, 255, 0);
QRgb cyan = qRgb(0, 255, 255);

QRgb txText = qRgb(192, 0, 0);
QRgb rxText = qRgb(0, 0, 192);

// Tracks the active system colour scheme; refreshed at startup and on
// every QStyleHints::colorSchemeChanged signal. No persisted user setting
// — the macOS Appearance pane is the single source of truth.
bool darkTheme = false;
bool minimizeonStart = true;
extern "C" bool useKISSControls;

// Indexed colour list from ARDOPC

#define WHITE 0
#define Tomato 1
#define Gold 2
#define Lime 3
#define Yellow 4
#define Orange 5
#define Khaki 6
#define Cyan 7
#define DeepSkyBlue 8
#define RoyalBlue 9
#define Navy 10
#define Black 11
#define Goldenrod 12
#define Fuchsia 13

QRgb vbColours[16] = { qRgb(255, 255, 255), qRgb(255, 99, 71), qRgb(255, 215, 0), qRgb(0, 255, 0),
						qRgb(255, 255, 0), qRgb(255, 165, 0), qRgb(240, 240, 140), qRgb(0, 255, 255),
						qRgb(0, 191, 255), qRgb(65, 105, 225), qRgb(0, 0, 128), qRgb(0, 0, 0),
						qRgb(218, 165, 32), qRgb(255, 0, 255) };

unsigned char  WaterfallLines[2][80][4096] = { 0 };
int NextWaterfallLine[2] = {0, 0};

unsigned int LastLevel = 255;
unsigned int LastBusy = 255;

extern "C" int UDPClientPort;
extern "C" int UDPServerPort;
extern "C" int TXPort;
extern char UDPHost[64];

QTimer *cwidtimer;
QTimer *PTTWatchdog;

QWidget * mythis;

QElapsedTimer pttOnTimer;



QSystemTrayIcon * trayIcon = nullptr;

int MintoTray = 1;

int RSID_WF = 0;				// Set to use RSID FFT for Waterfall. 

char SixPackDevice[256] = "";
int SixPackPort = 0;
int SixPackEnable = 0;

// Stats

uint64_t PTTonTime[4] = { 0 };
uint64_t PTTActivemS[4] = { 0 };			// For Stats
uint64_t BusyonTime[4] = { 0 };
uint64_t BusyActivemS[4] = { 0 };

int AvPTT[4] = { 0 };
int AvBusy[4] = { 0 };

// Qt 6 requires a QCoreApplication instance for QMediaDevices to
// enumerate; populating these at file scope (Qt 5 worked there)
// returns empty lists and a "requires QCoreApplication" warning.
// Default-construct here, populate lazily in GetAudioDevices().
QList<QAudioDevice> inputDevices;
QList<QAudioDevice> outputDevices;

// Parallel filtered lists, indices aligned with the Devices-dialog combo
// boxes. inputDevices/outputDevices remain the unfiltered source-of-truth
// (used by the matching loops in GetAudioDevices). The combos are
// populated from CaptureNames/PlaybackNames which skip "surround" entries,
// so a combo currentIndex can't be used to index the unfiltered lists —
// it must index these filtered lists instead.
QList<QAudioDevice> inputDevicesFiltered;
QList<QAudioDevice> outputDevicesFiltered;

QAudioDevice inDeviceInfo;
QAudioDevice outDeviceInfo;

extern "C" void WriteDebugLog(char * Mess)
{
	qDebug() << Mess;
}

void QtSoundModem::doupdateDCD(int Chan, int State)
{
	DCDLabel[Chan]->setVisible(State);

	// This also tries to get a percentage on time over a minute

	uint64_t Time = QDateTime::currentMSecsSinceEpoch();

	if (State)
	{
		BusyonTime[Chan] = Time;
		return;
	}

	if (BusyonTime[Chan])
	{
		BusyActivemS[Chan] += Time - BusyonTime[Chan];
		BusyonTime[Chan] = 0;
	}


}

extern "C" char * frame_monitor(string * frame, char * code, bool tx_stat);
extern "C" char * ShortDateTime();

// Monitor-line buffer: must accommodate the worst-case frame_monitor()
// return (now up to ~2 kB after the IL2P-overflow fix in sm_main.c) plus
// the "%d:" channel prefix and trailing "\r". The malloc passes ownership
// to the receiver via signal/slot, hence the heap allocation.
#define QSM_MON_MSG_SIZE 4096

extern "C" void mon_rsid(int snd_ch, char * RSID)
{
	int Len;
	char * Msg = (char *)malloc(QSM_MON_MSG_SIZE);		// Cant pass local variable via signal/slot

	snprintf(Msg, QSM_MON_MSG_SIZE, "%d:%s [%s%c]", snd_ch + 1, RSID, ShortDateTime(), 'R');

	Len = strlen(Msg);

	if (Len > 0 && Msg[Len - 1] != '\r' && Len < QSM_MON_MSG_SIZE - 1)
	{
		Msg[Len++] = '\r';
		Msg[Len] = 0;
	}

	emit t->sendtoTrace(Msg, 0);
}

extern "C" void put_frame(int snd_ch, string * frame, char * code, int  tx, int excluded)
{
	UNUSED(excluded);

	int Len;
	char * Msg = (char *)malloc(QSM_MON_MSG_SIZE);		// Cant pass local variable via signal/slot

	if (strcmp(code, "NON-AX25") == 0)
		snprintf(Msg, QSM_MON_MSG_SIZE, "%d: <NON-AX25 frame Len = %d [%s%c]\r", snd_ch, frame->Length, ShortDateTime(), 'R');
	else
		snprintf(Msg, QSM_MON_MSG_SIZE, "%d:%s", snd_ch + 1, frame_monitor(frame, code, tx));

	Len = strlen(Msg);

	if (Len > 0 && Msg[Len - 1] != '\r' && Len < QSM_MON_MSG_SIZE - 1)
	{
		Msg[Len++] = '\r';
		Msg[Len] = 0;
	}

	// Echo every frame to stderr so the --decode-wav harness (and
	// anyone running from a terminal) can see decodes without the
	// GUI. The trailing \r in Msg is intentional for the trace pane;
	// strip it from the stderr line for readability.
	int stderrLen = Len;
	while (stderrLen > 0 && (Msg[stderrLen - 1] == '\r' || Msg[stderrLen - 1] == '\n'))
		stderrLen--;
	fprintf(stderr, "DECODED [%s] %.*s\n", tx ? "TX" : "RX", stderrLen, Msg);
	fflush(stderr);

	emit t->sendtoTrace(Msg, tx);
}

extern "C" void updateDCD(int Chan, bool State)
{
	emit t->updateDCD(Chan, State);
}

bool QtSoundModem::eventFilter(QObject* obj, QEvent *evt)
{
	// Sync the View > PSK Constellation menu when the user dismisses
	// the dialog via its title-bar close button. Suppressed during
	// shutdown so the teardown close() doesn't clobber the persisted
	// preference.
	if (obj == constellationDialog && evt->type() == QEvent::Close && !Closing)
	{
		PSKWindow = 0;
		if (actConstellation)
			actConstellation->setChecked(false);
		saveSettings();
		return false;
	}

	// The legacy logic below assumes the filter is installed on the main
	// window itself; for any other watched object pass events through
	// untouched so we don't swallow paint/resize/input on the dialog.
	if (obj != this)
		return false;

	if (evt->type() == QEvent::Resize)
	{
		return QWidget::event(evt);
	}

	if (evt->type() == QEvent::WindowStateChange)
	{
		if (windowState().testFlag(Qt::WindowMinimized) == true)
			w_state = WIN_MINIMIZED;
		else
			w_state = WIN_MAXIMIZED;
	}
//	if (evt->type() == QGuiApplication::applicationStateChanged) - this is a sigma;
//	{
//		qDebug() << "App State changed =" << evt->type() << endl;
//	}

	return QWidget::event(evt);
}

void QtSoundModem::resizeEvent(QResizeEvent* event)
{
	QMainWindow::resizeEvent(event);

	QRect r = geometry();

	int modemBoxHeight = 34;
	
	int Width = r.width();
	int Height = r.height() - 25;

	int monitorTop;
	int monitorHeight;
	int sessionTop;
	int sessionHeight = 0;
	int waterfallsTop;
	int waterfallsHeight = WaterfallImageHeight;

	ui.modeB->setVisible(soundChannel[1]);
	ui.centerB->setVisible(soundChannel[1]);
	ui.labelB->setVisible(soundChannel[1]);
	DCDLabel[1]->setVisible(soundChannel[1]);
	ui.RXOffsetB->setVisible(soundChannel[1]);

	ui.modeC->setVisible(soundChannel[2]);
	ui.centerC->setVisible(soundChannel[2]);
	ui.labelC->setVisible(soundChannel[2]);
	DCDLabel[2]->setVisible(soundChannel[2]);
	ui.RXOffsetC->setVisible(soundChannel[2]);

	ui.modeD->setVisible(soundChannel[3]);
	ui.centerD->setVisible(soundChannel[3]);
	ui.labelD->setVisible(soundChannel[3]);
	DCDLabel[3]->setVisible(soundChannel[3]);
	ui.RXOffsetD->setVisible(soundChannel[3]);

	if (soundChannel[2] || soundChannel[3])
		modemBoxHeight = 60;

	ui.Waterfall->setVisible(0);

	monitorTop = modemBoxHeight + 1;

	// Now have one waterfall label containing headers and waterfalls

		if (Firstwaterfall || Secondwaterfall)
			ui.Waterfall->setVisible(1);

	if (AGWServ || RHPServ)
	{
		sessionTable->setVisible(true);
		sessionHeight = 150;
	}
	else
	{
		sessionTable->setVisible(false);
	}

	// if only displaying one Waterfall, change height of waterfall area

	if (UsingBothChannels == 0  || (Firstwaterfall == 0) || (Secondwaterfall == 0))
	{
		waterfallsHeight /= 2;
	}

	if ((Firstwaterfall == 0) && (Secondwaterfall == 0))
		waterfallsHeight = 0;

	monitorHeight = Height - sessionHeight - waterfallsHeight - modemBoxHeight;
	waterfallsTop = Height - waterfallsHeight;
	sessionTop = Height - (sessionHeight + waterfallsHeight);

	ui.monWindow->setGeometry(QRect(0, monitorTop, Width, monitorHeight));

	if (AGWServ || RHPServ)
		sessionTable->setGeometry(QRect(0, sessionTop, Width, sessionHeight));

	if (waterfallsHeight)
		ui.Waterfall->setGeometry(QRect(0, waterfallsTop, Width, waterfallsHeight + 2));

	// Anchor top-row right cluster to the right edge so it doesn't leave a
	// wide empty band when fullscreen widens the window beyond the design
	// width. Order left → right: RX Offset, DCD Level, TX Audio, RX Audio,
	// Rcv Level (meter at the far right). Design base 1200 — clamp at zero.
	int rightDelta = Width > 1200 ? Width - 1200 : 0;
	ui.RXOffsetLabel->setGeometry(600 + rightDelta, 2, 87, 13);
	ui.RXOffset->setGeometry(600 + rightDelta, 18, 63, 14);
	ui.label->setGeometry(690 + rightDelta, 2, 73, 13);
	ui.DCDSlider->setGeometry(690 + rightDelta, 18, 73, 14);
	ui.TXAudioLabel->setGeometry(780 + rightDelta, 2, 100, 13);
	ui.TXAudio->setGeometry(780 + rightDelta, 18, 90, 14);
	ui.RXAudioLabel->setGeometry(890 + rightDelta, 2, 100, 13);
	ui.RXAudio->setGeometry(890 + rightDelta, 18, 90, 14);
	ui.label_7->setGeometry(1000 + rightDelta, 0, 91, 13);
	ui.RXLevel->setGeometry(1000 + rightDelta, 14, 150, 11);
	ui.RXLevel2->setGeometry(1000 + rightDelta, 23, 150, 11);
}

QAction * setupMenuLine(QMenu * Menu, char * Label, QObject * parent, int State)
{
	QAction * Act = new QAction(Label, parent);
	Menu->addAction(Act);

	Act->setCheckable(true);
	if (State)
		Act->setChecked(true);

	parent->connect(Act, SIGNAL(triggered()), parent, SLOT(menuChecked()));

	return Act;
}

void QtSoundModem::menuChecked()
{
	QAction * Act = static_cast<QAction*>(QObject::sender());

	int state = Act->isChecked();

	if (Act == actWaterfall1)
	{
		Firstwaterfall = state;
		initWaterfall(Firstwaterfall | Secondwaterfall);
	}
	else if (Act == actWaterfall2)
	{
		Secondwaterfall = state;
		initWaterfall(Firstwaterfall | Secondwaterfall);
	}
	else if (Act == actConstellation)
	{
		PSKWindow = state;
		constellationDialog->setVisible(state);
	}

	saveSettings();
}

void QtSoundModem::initWaterfall(int state)
{
	if (state == 1)
	{
	//	if (ui.Waterfall)
	//	{
	//		delete ui.Waterfall;
	//		ui.Waterfall = new QLabel(ui.centralWidget);
	//	}
		WaterfallCopy = ui.Waterfall;

		Waterfall = new QImage(1024, WaterfallImageHeight + 2, QImage::Format_RGB32);

		NeedWaterfallHeaders = 1;

	}
	else
	{
		delete(Waterfall);
		Waterfall = 0;
	}

	QSize Size(800, 602);						// Not actually used, but Event constructor needs it

	QResizeEvent *event = new QResizeEvent(Size, Size);
	QApplication::sendEvent(this, event);
}

QRect PSKRect = { 100,100,100,100 };

QLabel * constellationLabel[4];
QLabel * QualLabel[4];

// Local copies

QLabel *RXOffsetLabel;
QSlider *RXOffset;

QFont Font;

extern "C" void CheckPSKWindows()
{
	NeedPSKRefresh = 1;
}
void DoPSKWindows()
{
	// Display Constellation for PSK Window;

	int NextX = 0;
	int i;

	for (i = 0; i < 4; i++)
	{
		if (soundChannel[i] && pskStates[i])
		{
			constellationLabel[i]->setGeometry(QRect(NextX, 19, 121, 121));
			QualLabel[i]->setGeometry(QRect(1 + NextX, 1, 120, 15));
			constellationLabel[i]->setVisible(1);
			QualLabel[i]->setVisible(1);

			NextX += 122;
		}
		else
		{
			constellationLabel[i]->setVisible(0);
			QualLabel[i]->setVisible(0);
		}
	}
	constellationDialog->resize(NextX, 140);
}

extern "C" struct timespec pttclk;

QtSoundModem::QtSoundModem(QWidget *parent) : QMainWindow(parent)
{
	QString family;
	int csize;
	QFont::Weight weight;

	ui.setupUi(this);

	mythis = this;

	getSettings();

	serial = new serialThread;

	QSettings mysettings("QtSoundModem.ini", QSettings::IniFormat);

	QFont sysFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);

	family = mysettings.value("FontFamily", sysFont.family()).toString();
	csize = mysettings.value("PointSize", sysFont.pointSize()).toInt();
	weight = (QFont::Weight)mysettings.value("Weight", sysFont.weight()).toInt();

	Font = QFont(family);
	if (csize > 0)
		Font.setPointSize(csize);
	Font.setWeight(weight);

	QApplication::setFont(Font);

	constellationDialog = new QDialog(nullptr, Qt::WindowTitleHint | Qt::WindowSystemMenuHint);
	constellationDialog->resize(488, 140);
	constellationDialog->setGeometry(PSKRect);
	constellationDialog->installEventFilter(this);

	QFont f("Arial", 8, QFont::Normal);

	for (int i = 0; i < 4; i++)
	{
		char Text[16];
		sprintf(Text, "Chan %c", i + 'A');

		constellationLabel[i] = new QLabel(constellationDialog);
		constellationDialog->setWindowTitle("PSK Constellations");

		QualLabel[i] = new QLabel(constellationDialog);
		QualLabel[i]->setText(Text);
		QualLabel[i]->setFont(f);

		Constellation[i] = new QImage(121, 121, QImage::Format_RGB32);
		Constellation[i]->fill(black);
		constellationLabel[i]->setPixmap(QPixmap::fromImage(*Constellation[i]));
	}
	if (PSKWindow)
		constellationDialog->show();

#if !defined(Q_OS_MACOS)
	// macOS uses the Dock for minimised windows; a QSystemTrayIcon would
	// land in the menu bar, which is not the platform idiom. The whole
	// minimise-to-tray feature is suppressed on macOS — see also the
	// guarded "Minimize to Tray" menu entry below.
	if (MintoTray)
	{
		char popUp[256];
		sprintf(popUp, "QtSoundModem %d %d", AGWPort, KISSPort);
		trayIcon = new QSystemTrayIcon(QIcon(":/QtSoundModem/soundmodem.ico"), this);
		trayIcon->setToolTip(popUp);
		trayIcon->show();

		connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(TrayActivated(QSystemTrayIcon::ActivationReason)));
	}
#endif

	using48000 = 0;			// Set if using 48K sample rate (ie RUH Modem active)
	ReceiveSize = 512;
	SendSize = 1024;		// 100 mS for now

	for (int i = 0; i < 4; i++)
	{
		if (soundChannel[i] && (speed[i] == SPEED_RUH48 || speed[i] == SPEED_RUH96))
		{
			using48000 = 1;			// Set if using 48K sample rate (ie RUH Modem active)
			ReceiveSize = 2048;
			SendSize = 4096;		// 100 mS for now
		}
	}

	float FFTCalc = 12000.0f / ((WaterfallMax - WaterfallMin) / 900.0f);

	FFTSize = FFTCalc + 0.4999;

	if (FFTSize > 8191)
		FFTSize = 8190;

	if (FFTSize & 1)		// odd
		FFTSize--;

	BinSize = 12000.0 / FFTSize;

	restoreGeometry(mysettings.value("geometry").toByteArray());
	restoreState(mysettings.value("windowState").toByteArray());

	// restoreGeometry can leave the window narrower than the new design
	// minimum (e.g. when an .ini saved by an earlier build with a smaller
	// minimumSize is loaded). Force-grow so the right-cluster anchoring
	// in resizeEvent has room for RX Offset / DCD / Rcv Level / TX Audio /
	// RX Audio without colliding.
	if (width() < 1200)
		resize(1200, height());

	constellationDialog->restoreGeometry(mysettings.value("constellationgeometry").toByteArray());

	sessionTable = new QTableWidget(ui.centralWidget);

	sessionTable->verticalHeader()->setVisible(FALSE);
	sessionTable->verticalHeader()->setDefaultSectionSize(20);
	sessionTable->horizontalHeader()->setDefaultSectionSize(68);
	sessionTable->setRowCount(1);
	sessionTable->setColumnCount(12);
	m_TableHeader << "MyCall" << "DestCall" << "Status" << "Sent pkts" << "Sent Bytes" << "Rcvd pkts" << "Rcvd bytes" << "Rcvd FC" << "FEC corr" << "CPS TX" << "CPS RX" << "Direction";

	// Track the OS colour scheme. Qt 6.5+ surfaces the live system value
	// via QStyleHints; on macOS this follows System Settings → Appearance
	// and re-fires when the user toggles light/dark.
	darkTheme = (qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);
	connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged,
		this, [this](Qt::ColorScheme scheme)
		{
			darkTheme = (scheme == Qt::ColorScheme::Dark);
			mysetstyle();
		});

	mysetstyle();

	sessionTable->setHorizontalHeaderLabels(m_TableHeader);
	sessionTable->setColumnWidth(0, 80);
	sessionTable->setColumnWidth(1, 80);
	sessionTable->setColumnWidth(4, 76);
	sessionTable->setColumnWidth(5, 76);
	sessionTable->setColumnWidth(6, 80);
	sessionTable->setColumnWidth(11, 72);

	for (int i = 0; i < modes_count; i++)
	{
		ui.modeA->addItem(modes_name[i]);
		ui.modeB->addItem(modes_name[i]);
		ui.modeC->addItem(modes_name[i]);
		ui.modeD->addItem(modes_name[i]);
	}

	// Set up Menus

	setupMenu = ui.menuBar->addMenu(tr("Settings"));

	actDevices = new QAction("Setup Devices", this);
	// Qt's macOS heuristic auto-moves QActions whose title contains
	// "setup", "settings", "preferences", "options", "config",
	// "about" or "quit" into the Apple-style application menu's
	// Preferences/About/Quit slots — invisible to users who expect
	// them under the Settings menu where the code added them.
	// Force NoRole on every action that would otherwise be stolen.
	actDevices->setMenuRole(QAction::NoRole);
	setupMenu->addAction(actDevices);

	connect(actDevices, SIGNAL(triggered()), this, SLOT(clickedSlot()));
	actDevices->setObjectName("actDevices");
	actModems = new QAction("Setup Modems", this);
	actModems->setMenuRole(QAction::NoRole);
	actModems->setObjectName("actModems");
	setupMenu->addAction(actModems);

	connect(actModems, SIGNAL(triggered()), this, SLOT(clickedSlot()));

	// Font picker disabled on macOS port: QFontDialog crashes inside
	// QtWidgets on Qt 6.11 + macOS 26.x. Font defaults to the system
	// font (set up in startup via QFontDatabase::systemFont).

#if !defined(Q_OS_MACOS)
	actMintoTray = setupMenu->addAction("Minimize to Tray", this, SLOT(MinimizetoTray()));
	actMintoTray->setCheckable(1);
	actMintoTray->setChecked(MintoTray);
#endif

	viewMenu = ui.menuBar->addMenu(tr("&View"));

	actWaterfall1 = setupMenuLine(viewMenu, (char *)"First waterfall", this, Firstwaterfall);
	actWaterfall2 = setupMenuLine(viewMenu, (char *)"Second Waterfall", this, Secondwaterfall);
	actConstellation = setupMenuLine(viewMenu, (char *)"PSK Constellation", this, PSKWindow);

	// macOS QMenuBar only renders QMenu submenus at the top level —
	// QActions added directly via addAction() are silently dropped.
	// Group action-style entries under a real submenu so they appear.
	QMenu *toolsMenu = ui.menuBar->addMenu(tr("&Tools"));

	actCalib = toolsMenu->addAction("&Calibration");
	actCalib->setMenuRole(QAction::NoRole);
	connect(actCalib, SIGNAL(triggered()), this, SLOT(doCalibrate()));

	actAbout = ui.menuBar->addAction("&About");
	// About is fine to keep Qt's default TextHeuristicRole — the
	// "About" text triggers the macOS-native heuristic that routes
	// it into the application menu as "About QtSoundModem", where
	// users expect it.
	connect(actAbout, SIGNAL(triggered()), this, SLOT(doAbout()));

	RXLevel = new QImage(150, 10, QImage::Format_RGB32);
	RXLevel->fill(white);
	ui.RXLevel->setPixmap(QPixmap::fromImage(*RXLevel));
	RXLevelCopy = ui.RXLevel;

	RXLevel2 = new QImage(150, 10, QImage::Format_RGB32);
	RXLevel2->fill(white);
	ui.RXLevel2->setPixmap(QPixmap::fromImage(*RXLevel2));
	RXLevel2Copy = ui.RXLevel2;

	// DCD red-square indicators. Upstream hard-codes them at
	// (280, 31) / (575, 31) / (280, 61) / (575, 61) — Y values that
	// land in the gap between modem rows (row 1 is y=6-28 in
	// QtSoundModem.ui, row 2 is y=31-53), so the LEDs always
	// appeared visibly displaced below the modem they belong to.
	// Position them dynamically against the actual rendered
	// row geometry so the LED is vertically centred next to its
	// modem regardless of any future .ui tweaks.
	const int dcdSize = 12;
	auto centerOnRow = [dcdSize](QWidget * row) {
		return row->y() + (row->height() - dcdSize) / 2;
	};
	const int rowAY = centerOnRow(ui.modeA);
	const int rowCY = centerOnRow(ui.modeC);

	DCDLabel[0] = new QLabel(this);
	DCDLabel[0]->setObjectName(QString::fromUtf8("DCDLedA"));
	DCDLabel[0]->setGeometry(QRect(280, rowAY, dcdSize, dcdSize));
	DCDLabel[0]->setVisible(TRUE);

	DCDLabel[1] = new QLabel(this);
	DCDLabel[1]->setObjectName(QString::fromUtf8("DCDLedB"));
	DCDLabel[1]->setGeometry(QRect(575, rowAY, dcdSize, dcdSize));
	DCDLabel[1]->setVisible(TRUE);

	DCDLabel[2] = new QLabel(this);
	DCDLabel[2]->setObjectName(QString::fromUtf8("DCDLedC"));
	DCDLabel[2]->setGeometry(QRect(280, rowCY, dcdSize, dcdSize));
	DCDLabel[2]->setVisible(FALSE);

	DCDLabel[3] = new QLabel(this);
	DCDLabel[3]->setObjectName(QString::fromUtf8("DCDLedD"));
	DCDLabel[3]->setGeometry(QRect(575, rowCY, dcdSize, dcdSize));
	DCDLabel[3]->setVisible(FALSE);
	
	DCDLed[0] = new QImage(12, 12, QImage::Format_RGB32);
	DCDLed[1] = new QImage(12, 12, QImage::Format_RGB32);
	DCDLed[2] = new QImage(12, 12, QImage::Format_RGB32);
	DCDLed[3] = new QImage(12, 12, QImage::Format_RGB32);

	DCDLed[0]->fill(red);
	DCDLed[1]->fill(red);
	DCDLed[2]->fill(red);
	DCDLed[3]->fill(red);

	DCDLabel[0]->setPixmap(QPixmap::fromImage(*DCDLed[0]));
	DCDLabel[1]->setPixmap(QPixmap::fromImage(*DCDLed[1]));
	DCDLabel[2]->setPixmap(QPixmap::fromImage(*DCDLed[2]));
	DCDLabel[3]->setPixmap(QPixmap::fromImage(*DCDLed[3]));

	chanOffsetLabel[0] = ui.RXOffsetA;
	chanOffsetLabel[1] = ui.RXOffsetB;
	chanOffsetLabel[2] = ui.RXOffsetC;
	chanOffsetLabel[3] = ui.RXOffsetD;

	WaterfallCopy = ui.Waterfall;

	initWaterfall(Firstwaterfall | Secondwaterfall);

	monWindowCopy = ui.monWindow;

	ui.monWindow->document()->setMaximumBlockCount(10000);

//	connect(ui.monWindow, SIGNAL(selectionChanged()), this, SLOT(onTEselectionChanged()));

	connect(ui.modeA, SIGNAL(currentIndexChanged(int)), this, SLOT(clickedSlotI(int)));
	connect(ui.modeB, SIGNAL(currentIndexChanged(int)), this, SLOT(clickedSlotI(int)));
	connect(ui.modeC, SIGNAL(currentIndexChanged(int)), this, SLOT(clickedSlotI(int)));
	connect(ui.modeD, SIGNAL(currentIndexChanged(int)), this, SLOT(clickedSlotI(int)));

	ModemA = speed[0];
	ModemB = speed[1];
	ModemC = speed[2];
	ModemD = speed[3];

	ui.modeA->setCurrentIndex(speed[0]);
	ui.modeB->setCurrentIndex(speed[1]);
	ui.modeC->setCurrentIndex(speed[2]);
	ui.modeD->setCurrentIndex(speed[3]);

	ModemA = ui.modeA->currentIndex();

	ui.centerA->setValue(rx_freq[0]);
	ui.centerB->setValue(rx_freq[1]);
	ui.centerC->setValue(rx_freq[2]);
	ui.centerD->setValue(rx_freq[3]);

	connect(ui.centerA, SIGNAL(valueChanged(int)), this, SLOT(clickedSlotI(int)));
	connect(ui.centerB, SIGNAL(valueChanged(int)), this, SLOT(clickedSlotI(int)));
	connect(ui.centerC, SIGNAL(valueChanged(int)), this, SLOT(clickedSlotI(int)));
	connect(ui.centerD, SIGNAL(valueChanged(int)), this, SLOT(clickedSlotI(int)));

	ui.DCDSlider->setValue(dcd_threshold);

	char valChar[32];
	sprintf(valChar, "RX Offset %d", rxOffset);
	ui.RXOffsetLabel->setText(valChar);
	ui.RXOffset->setValue(rxOffset);

	RXOffsetLabel = ui.RXOffsetLabel;
	RXOffset = ui.RXOffset;

	connect(ui.DCDSlider, SIGNAL(sliderMoved(int)), this, SLOT(clickedSlotI(int)));
	connect(ui.RXOffset, SIGNAL(valueChanged(int)), this, SLOT(clickedSlotI(int)));

	sprintf(valChar, "TX Audio %d%%", txAudioLevel);
	ui.TXAudioLabel->setText(valChar);
	ui.TXAudio->setValue(txAudioLevel);

	sprintf(valChar, "RX Audio %d%%", rxAudioLevel);
	ui.RXAudioLabel->setText(valChar);
	ui.RXAudio->setValue(rxAudioLevel);

	connect(ui.TXAudio, SIGNAL(valueChanged(int)), this, SLOT(clickedSlotI(int)));
	connect(ui.RXAudio, SIGNAL(valueChanged(int)), this, SLOT(clickedSlotI(int)));

	QObject::connect(t, SIGNAL(sendtoTrace(char *, int)), this, SLOT(sendtoTrace(char *, int)), Qt::QueuedConnection);
	QObject::connect(t, SIGNAL(updateDCD(int, int)), this, SLOT(doupdateDCD(int, int)), Qt::QueuedConnection);

	QObject::connect(t, SIGNAL(startCWIDTimer()), this, SLOT(startCWIDTimerSlot()), Qt::QueuedConnection);
	QObject::connect(t, SIGNAL(setWaterfallImage()), this, SLOT(setWaterfallImage()), Qt::QueuedConnection);
	QObject::connect(t, SIGNAL(setLevelImage()), this, SLOT(setLevelImage()), Qt::QueuedConnection);
	QObject::connect(t, SIGNAL(setConstellationImage(int, int)), this, SLOT(setConstellationImage(int, int)), Qt::QueuedConnection);
	QObject::connect(t, SIGNAL(startWatchdog()), this, SLOT(StartWatchdog()), Qt::QueuedConnection);
	QObject::connect(t, SIGNAL(stopWatchdog()), this, SLOT(StopWatchdog()), Qt::QueuedConnection);

	connect(ui.RXOffsetA, SIGNAL(returnPressed()), this, SLOT(returnPressed()));
	connect(ui.RXOffsetB, SIGNAL(returnPressed()), this, SLOT(returnPressed()));
	connect(ui.RXOffsetC, SIGNAL(returnPressed()), this, SLOT(returnPressed()));
	connect(ui.RXOffsetD, SIGNAL(returnPressed()), this, SLOT(returnPressed()));

	QTimer *timer = new QTimer(this);
	connect(timer, SIGNAL(timeout()), this, SLOT(MyTimerSlot()));
	timer->start(100);

	QTimer *statstimer = new QTimer(this);
	connect(statstimer, SIGNAL(timeout()), this, SLOT(StatsTimer()));
	statstimer->start(60000);		// One Minute

	cwidtimer = new QTimer(this);
	connect(cwidtimer, SIGNAL(timeout()), this, SLOT(CWIDTimer()));

	PTTWatchdog = new QTimer(this);
	connect(PTTWatchdog, SIGNAL(timeout()), this, SLOT(PTTWatchdogExpired()));

	if (CWIDInterval && afterTraffic == false)
		cwidtimer->start(CWIDInterval * 60000);

	if (RSID_SetModem[0])
	{
		RSID_WF = 1;
		RSIDinitfft();
	}
//	il2p_init(1);

	QTimer::singleShot(200, this, &QtSoundModem::updateFont);

	connect(serial, &serialThread::request, this, &QtSoundModem::showRequest);

	if (SoundMode == 5)
		QtSoundInit();


}

void QtSoundModem::updateFont()
{
	QApplication::setFont(Font);
}

void QtSoundModem::MinimizetoTray()
{
	MintoTray = actMintoTray->isChecked();
	saveSettings();
	QMessageBox::about(this, tr("QtSoundModem"),
	tr("Program must be restarted to change Minimize mode"));
}


void QtSoundModem::TrayActivated(QSystemTrayIcon::ActivationReason reason)
{
	if (reason == 3)
	{
		showNormal();
		w->setWindowState((w->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
	} 
}

extern "C" void sendCWID(char * strID, BOOL blnPlay, int Chan);

extern "C" void checkforCWID()
{
	emit(t->startCWIDTimer());
};

extern "C" void QtSoundModem::startCWIDTimerSlot()
{
	if (CWIDInterval && afterTraffic == 1 && cwidtimerisActive == false)
	{
		cwidtimerisActive = true;
		QTimer::singleShot(CWIDInterval * 60000, this, &QtSoundModem::CWIDTimer);
	}
}

void QtSoundModem::CWIDTimer()
{
	cwidtimerisActive = false;
	sendCWID(CWIDCall, CWIDType, 0);
	calib_mode[0] = 4;
}

void extSetOffset(int chan)
{
	char valChar[32];
	sprintf(valChar, "%d", chanOffset[chan]);
	chanOffsetLabel[chan]->setText(valChar);

	NeedWaterfallHeaders = true;

	pnt_change[0] = 1;
	pnt_change[1] = 1;
	pnt_change[2] = 1;
	pnt_change[3] = 1;

	return;
}

extern TMgmtMode ** MgmtConnections;
extern int MgmtConCount;
extern QList<QTcpSocket*>  _MgmtSockets;
extern "C" void doAGW2MinTimer();

#define FEND 0xc0
#define QTSMKISSCMD 7

int AGW2MinTimer = 0;

void QtSoundModem::StatsTimer()
{
	// Calculate % Busy over last minute

	for (int n = 0; n < 4; n++)
	{
		if (soundChannel[n] == 0)	// Channel not used
			continue;
		
		AvPTT[n] = PTTActivemS[n] / 600;		// ms but  want %

		PTTActivemS[n] = 0;

		AvBusy[n] = BusyActivemS[n] / 600;
		BusyActivemS[n] = 0;
	
	
	// send to any connected Mgmt streams

		char Msg[64];

		if (!useKISSControls)
		{

			for (QTcpSocket* socket : _MgmtSockets)
			{
				// Find Session

				TMgmtMode * MGMT = NULL;

				for (int i = 0; i < MgmtConCount; i++)
				{
					if (MgmtConnections[i]->Socket == socket)
					{
						MGMT = MgmtConnections[i];
						break;
					}
				}

				if (MGMT == NULL)
					continue;

				if (MGMT->BPQPort[n])
				{
					sprintf(Msg, "STATS %d %d %d\r", MGMT->BPQPort[n], AvPTT[n], AvBusy[n]);
					socket->write(Msg);
				}
			}
		}
		else			// useKISSControls set
		{
			UCHAR * Control = (UCHAR *)malloc(32);

			int len = sprintf((char *)Control, "%c%cSTATS %d %d%c", FEND, (n) << 4 | QTSMKISSCMD, AvPTT[n], AvBusy[n], FEND);
			KISSSendtoServer(NULL, Control, len);
		}
	}

	AGW2MinTimer++;

	if (AGW2MinTimer > 1)
	{
		AGW2MinTimer = 0;
		doAGW2MinTimer();
	}
}

// PTT Stats

extern "C" void UpdatePTTStats(int Chan, int State)
{
	uint64_t Time = QDateTime::currentMSecsSinceEpoch();

	if (State)
	{
		PTTonTime[Chan] = Time;

		// Cancel Busy timer (stats include ptt on time in port active

		if (BusyonTime[Chan])
		{
			BusyActivemS[Chan] += (Time - BusyonTime[Chan]);
			BusyonTime[Chan] = 0;
		}
	}
	else
	{
		if (PTTonTime[Chan])
		{
			PTTActivemS[Chan] += (Time - PTTonTime[Chan]);
			PTTonTime[Chan] = 0;
		}
	}
}

void QtSoundModem::MyTimerSlot()
{
	// 100 mS Timer Event

	for (int i = 0; i < 4; i++)
	{

		if (needSetOffset[i])
		{
			needSetOffset[i] = 0;
			extSetOffset(i);						// Update GUI
		}
	}

	if (refreshModems)
	{
		refreshModems = 0;

		ui.modeA->setCurrentIndex(speed[0]);
		ui.modeB->setCurrentIndex(speed[1]);
		ui.modeC->setCurrentIndex(speed[2]);
		ui.modeD->setCurrentIndex(speed[3]);
		ui.centerA->setValue(rx_freq[0]);
		ui.centerB->setValue(rx_freq[1]);
		ui.centerC->setValue(rx_freq[2]);
		ui.centerD->setValue(rx_freq[3]);
	}
	if (NeedPSKRefresh)
	{
		NeedPSKRefresh = 0;
		DoPSKWindows();
	}

	if (NeedWaterfallHeaders)
	{
		NeedWaterfallHeaders = 0;

		if (Waterfall)
		{
			Waterfall->fill(black);
			DrawModemFreqRange();
			DrawFreqTicks();
		}
	}

	show_grid();
}

void QtSoundModem::returnPressed()
{
	char Name[32];
	int Chan;
	QString val;
	
	strcpy(Name, sender()->objectName().toUtf8());

	Chan = Name[8] - 'A';

	val = chanOffsetLabel[Chan]->text();

	chanOffset[Chan] = val.toInt();
	needSetOffset[Chan] = 1;				// Update GUI


}

void CheckforChanges(int Mode, int OldMode)
{
	int old48000 = using48000;

	if (OldMode != Mode && Mode == 15)
	{
		QMessageBox msgBox;

		msgBox.setText("Warning!!\nARDOP Packet is NOT the same as ARDOP\n"
			"It is an experimental mode for sending ax.25 frames using ARDOP packet formats\n");

		msgBox.setStandardButtons(QMessageBox::Ok);

		msgBox.exec();
	}

	// See if need to switch beween 12000 and 48000

	using48000 = 0;			// Set if using 48K sample rate (ie RUH Modem active)
	ReceiveSize = 512;
	SendSize = 1024;		// 100 mS for now

	for (int i = 0; i < 4; i++)
	{
		if (soundChannel[i] && (speed[i] == SPEED_RUH48 || speed[i] == SPEED_RUH96))
		{
			using48000 = 1;			// Set if using 48K sample rate (ie RUH Modem active)
			ReceiveSize = 2048;
			SendSize = 4096;		// 100 mS for now
		}
	}

	if (using48000 != old48000)
	{
		InitSound(1);
	}
}


void QtSoundModem::clickedSlotI(int i)
{
	char Name[32];

	strcpy(Name, sender()->objectName().toUtf8());

	if (strcmp(Name, "modeA") == 0)
	{
		int OldModem = ModemA;
		ModemA = ui.modeA->currentIndex();
		set_speed(0, ModemA);
		CheckforChanges(ModemA, OldModem);
		saveSettings();
		AGW_Report_Modem_Change(0);
		return;
	}

	if (strcmp(Name, "modeB") == 0)
	{
		int OldModem = ModemB;
		ModemB = ui.modeB->currentIndex();
		set_speed(1, ModemB);
		CheckforChanges(ModemB, OldModem);
		saveSettings();
		AGW_Report_Modem_Change(1);
		return;
	}

	if (strcmp(Name, "modeC") == 0)
	{
		int OldModem = ModemC;
		ModemC = ui.modeC->currentIndex();
		set_speed(2, ModemC);
		CheckforChanges(ModemC, OldModem);
		saveSettings();
		AGW_Report_Modem_Change(2);
		return;
	}

	if (strcmp(Name, "modeD") == 0)
	{
		int OldModem = ModemD;
		ModemD = ui.modeD->currentIndex();
		set_speed(3, ModemD);
		CheckforChanges(ModemD, OldModem);
		saveSettings();
		AGW_Report_Modem_Change(3);
		return;
	}

	if (strcmp(Name, "centerA") == 0)
	{
		if (i > 299)
		{
			QSettings * settings = new QSettings("QtSoundModem.ini", QSettings::IniFormat);
			ui.centerA->setValue(Freq_Change(0, i));
			settings->setValue("Modem/RXFreq1", ui.centerA->value());
			AGW_Report_Modem_Change(0);

		}
		return;
	}

	if (strcmp(Name, "centerB") == 0)
	{
		if (i > 300)
		{
			QSettings * settings = new QSettings("QtSoundModem.ini", QSettings::IniFormat);
			ui.centerB->setValue(Freq_Change(1, i));
			settings->setValue("Modem/RXFreq2", ui.centerB->value());
			AGW_Report_Modem_Change(1);
		}
		return;
	}

	if (strcmp(Name, "centerC") == 0)
	{
		if (i > 299)
		{
			QSettings * settings = new QSettings("QtSoundModem.ini", QSettings::IniFormat);
			ui.centerC->setValue(Freq_Change(2, i));
			settings->setValue("Modem/RXFreq3", ui.centerC->value());
			AGW_Report_Modem_Change(2);
		}
		return;
	}

	if (strcmp(Name, "centerD") == 0)
	{
		if (i > 299)
		{
			QSettings * settings = new QSettings("QtSoundModem.ini", QSettings::IniFormat);
			ui.centerD->setValue(Freq_Change(3, i));
			settings->setValue("Modem/RXFreq4", ui.centerD->value());
			AGW_Report_Modem_Change(3);
		}
		return;
	}

	if (strcmp(Name, "DCDSlider") == 0)
	{
		dcd_threshold = i;
		BusyDet = i / 10;		// for ardop busy detect code

		saveSettings();
		return;
	}
	
	if (strcmp(Name, "RXOffset") == 0)
	{
		char valChar[32];
		rxOffset = i;
		sprintf(valChar, "RX Offset %d",rxOffset);
		ui.RXOffsetLabel->setText(valChar);

		NeedWaterfallHeaders = true;

		pnt_change[0] = 1;
		pnt_change[1] = 1;
		pnt_change[2] = 1;
		pnt_change[3] = 1;

		saveSettings();
		return;
	}

	if (strcmp(Name, "TXAudio") == 0)
	{
		char valChar[32];
		txAudioLevel = i;
		sprintf(valChar, "TX Audio %d%%", txAudioLevel);
		ui.TXAudioLabel->setText(valChar);
		saveSettings();
		return;
	}

	if (strcmp(Name, "RXAudio") == 0)
	{
		char valChar[32];
		rxAudioLevel = i;
		sprintf(valChar, "RX Audio %d%%", rxAudioLevel);
		ui.RXAudioLabel->setText(valChar);
		saveSettings();
		return;
	}

	QMessageBox msgBox;
	msgBox.setWindowTitle("MessageBox Title");
	msgBox.setText("You Clicked " + ((QPushButton*)sender())->objectName());
	msgBox.exec();
}


void QtSoundModem::clickedSlot()
{
	char Name[32];

	strcpy(Name, sender()->objectName().toUtf8());

	if (strcmp(Name, "actDevices") == 0)
	{
		doDevices();
		return;
	}

	if (strcmp(Name, "actModems") == 0)
	{
		doModems();
		return;
	}

	if (strcmp(Name, "showBPF_A") == 0)
	{
		doFilter(0, 0);
		return;
	}

	if (strcmp(Name, "showTXBPF_A") == 0)
	{
		doFilter(0, 1);
		return;
	}

	if (strcmp(Name, "showLPF_A") == 0)
	{
		doFilter(0, 2);
		return;
	}
	

	if (strcmp(Name, "showBPF_B") == 0)
	{
		doFilter(1, 0);
		return;
	}

	if (strcmp(Name, "showTXBPF_B") == 0)
	{
		doFilter(1, 1);
		return;
	}

	if (strcmp(Name, "showLPF_B") == 0)
	{
		doFilter(1, 2);
		return;
	}

	if (strcmp(Name, "Low_A") == 0)
	{
		handleButton(0, 1);
		return;
	}

	if (strcmp(Name, "High_A") == 0)
	{
		handleButton(0, 2);
		return;
	}

	if (strcmp(Name, "Both_A") == 0)
	{
		handleButton(0, 3);
		return;
	}

	if (strcmp(Name, "Stop_A") == 0)
	{
		handleButton(0, 0);
		return;
	}


	if (strcmp(Name, "Low_B") == 0)
	{
		handleButton(1, 1);
		return;
	}

	if (strcmp(Name, "High_B") == 0)
	{
		handleButton(1, 2);
		return;
	}

	if (strcmp(Name, "Both_B") == 0)
	{
		handleButton(1, 3);
		return;
	}

	if (strcmp(Name, "Stop_B") == 0)
	{
		handleButton(1, 0);
		return;
	}

	if (strcmp(Name, "Low_C") == 0)
	{
		handleButton(2, 1);
		return;
	}

	if (strcmp(Name, "High_C") == 0)
	{
		handleButton(2, 2);
		return;
	}

	if (strcmp(Name, "Both_C") == 0)
	{
		handleButton(2, 3);
		return;
	}

	if (strcmp(Name, "Stop_C") == 0)
	{
		handleButton(2, 0);
		return;
	}

	if (strcmp(Name, "Low_D") == 0)
	{
		handleButton(3, 1);
		return;
	}

	if (strcmp(Name, "High_D") == 0)
	{
		handleButton(3, 2);
		return;
	}

	if (strcmp(Name, "Both_D") == 0)
	{
		handleButton(3, 3);
		return;
	}

	if (strcmp(Name, "Stop_D") == 0)
	{
		handleButton(3, 0);
		return;
	}

	if (strcmp(Name, "Cal1500") == 0)
	{
		char call[] = "1500TONE";
		sendCWID(call, 0, 0);
		calib_mode[0] = 4;
		return;
	}




	QMessageBox msgBox;
	msgBox.setWindowTitle("MessageBox Title");
	msgBox.setText("You Clicked " + ((QPushButton*)sender())->objectName());
	msgBox.exec();
}

Ui_ModemDialog * Dlg;

QDialog * modemUI;
QDialog * deviceUI;

void QtSoundModem::doModems()
{
	Dlg = new(Ui_ModemDialog);

	QDialog UI;
	char valChar[10];

	Dlg->setupUi(&UI);

	modemUI = &UI;
	deviceUI = 0;

	myResize *resize = new myResize();

	UI.installEventFilter(resize);

	sprintf(valChar, "%d", bpf[0]);
	Dlg->BPFWidthA->setText(valChar);
	sprintf(valChar, "%d", bpf[1]);
	Dlg->BPFWidthB->setText(valChar);
	sprintf(valChar, "%d", bpf[2]);
	Dlg->BPFWidthC->setText(valChar);
	sprintf(valChar, "%d", bpf[3]);
	Dlg->BPFWidthD->setText(valChar);

	sprintf(valChar, "%d", txbpf[0]);
	Dlg->TXBPFWidthA->setText(valChar);
	sprintf(valChar, "%d", txbpf[1]);
	Dlg->TXBPFWidthB->setText(valChar);
	sprintf(valChar, "%d", txbpf[2]);
	Dlg->TXBPFWidthC->setText(valChar);
	sprintf(valChar, "%d", txbpf[3]);
	Dlg->TXBPFWidthD->setText(valChar);

	sprintf(valChar, "%d", lpf[0]);
	Dlg->LPFWidthA->setText(valChar);
	sprintf(valChar, "%d", lpf[1]);
	Dlg->LPFWidthB->setText(valChar);
	sprintf(valChar, "%d", lpf[2]);
	Dlg->LPFWidthC->setText(valChar);
	sprintf(valChar, "%d", lpf[4]);
	Dlg->LPFWidthD->setText(valChar);

	sprintf(valChar, "%d", BPF_tap[0]);
	Dlg->BPFTapsA->setText(valChar);
	sprintf(valChar, "%d", BPF_tap[1]);
	Dlg->BPFTapsB->setText(valChar);
	sprintf(valChar, "%d", BPF_tap[2]);
	Dlg->BPFTapsC->setText(valChar);
	sprintf(valChar, "%d", BPF_tap[3]);
	Dlg->BPFTapsD->setText(valChar);

	sprintf(valChar, "%d", LPF_tap[0]);
	Dlg->LPFTapsA->setText(valChar);
	sprintf(valChar, "%d", LPF_tap[1]);
	Dlg->LPFTapsB->setText(valChar);
	sprintf(valChar, "%d", LPF_tap[2]);
	Dlg->LPFTapsC->setText(valChar);
	sprintf(valChar, "%d", LPF_tap[3]);
	Dlg->LPFTapsD->setText(valChar);

	Dlg->preEmphAllA->setChecked(emph_all[0]);

	if (emph_all[0])
		Dlg->preEmphA->setDisabled(TRUE);
	else
		Dlg->preEmphA->setCurrentIndex(emph_db[0]);

	Dlg->preEmphAllB->setChecked(emph_all[1]);

	if (emph_all[1])
		Dlg->preEmphB->setDisabled(TRUE);
	else
		Dlg->preEmphB->setCurrentIndex(emph_db[1]);

	Dlg->preEmphAllC->setChecked(emph_all[2]);

	if (emph_all[2])
		Dlg->preEmphC->setDisabled(TRUE);
	else
		Dlg->preEmphC->setCurrentIndex(emph_db[2]);

	Dlg->preEmphAllD->setChecked(emph_all[3]);

	if (emph_all[3])
		Dlg->preEmphD->setDisabled(TRUE);
	else
		Dlg->preEmphD->setCurrentIndex(emph_db[3]);


	Dlg->nonAX25A->setChecked(NonAX25[0]);
	Dlg->nonAX25B->setChecked(NonAX25[1]);
	Dlg->nonAX25C->setChecked(NonAX25[2]);
	Dlg->nonAX25D->setChecked(NonAX25[3]);

	Dlg->KISSOptA->setChecked(KISS_opt[0]);
	Dlg->KISSOptB->setChecked(KISS_opt[1]);
	Dlg->KISSOptC->setChecked(KISS_opt[2]);
	Dlg->KISSOptD->setChecked(KISS_opt[3]);

	sprintf(valChar, "%d", maxframe[0]);
	Dlg->MaxFrameA->setText(valChar);
	sprintf(valChar, "%d", maxframe[1]);
	Dlg->MaxFrameB->setText(valChar);
	sprintf(valChar, "%d", maxframe[2]);
	Dlg->MaxFrameC->setText(valChar);
	sprintf(valChar, "%d", maxframe[3]);
	Dlg->MaxFrameD->setText(valChar);

	sprintf(valChar, "%d", txdelay[0]);
	Dlg->TXDelayA->setText(valChar);
	sprintf(valChar, "%d", txdelay[1]);
	Dlg->TXDelayB->setText(valChar);
	sprintf(valChar, "%d", txdelay[2]);
	Dlg->TXDelayC->setText(valChar);
	sprintf(valChar, "%d", txdelay[3]);
	Dlg->TXDelayD->setText(valChar);


	sprintf(valChar, "%d", txtail[0]);
	Dlg->TXTailA->setText(valChar);
	sprintf(valChar, "%d", txtail[1]);
	Dlg->TXTailB->setText(valChar);
	sprintf(valChar, "%d", txtail[2]);
	Dlg->TXTailC->setText(valChar);
	sprintf(valChar, "%d", txtail[3]);
	Dlg->TXTailD->setText(valChar);

	Dlg->FrackA->setText(QString::number(frack_time[0]));
	Dlg->FrackB->setText(QString::number(frack_time[1]));
	Dlg->FrackC->setText(QString::number(frack_time[2]));
	Dlg->FrackD->setText(QString::number(frack_time[3]));

	Dlg->RetriesA->setText(QString::number(fracks[0]));
	Dlg->RetriesB->setText(QString::number(fracks[1]));
	Dlg->RetriesC->setText(QString::number(fracks[2]));
	Dlg->RetriesD->setText(QString::number(fracks[3]));

	sprintf(valChar, "%d", RCVR[0]);
	Dlg->AddRXA->setText(valChar);
	sprintf(valChar, "%d", RCVR[1]);
	Dlg->AddRXB->setText(valChar);
	sprintf(valChar, "%d", RCVR[2]);
	Dlg->AddRXC->setText(valChar);
	sprintf(valChar, "%d", RCVR[3]);
	Dlg->AddRXD->setText(valChar);

	sprintf(valChar, "%d", rcvr_offset[0]);
	Dlg->RXShiftA->setText(valChar);

	sprintf(valChar, "%d", rcvr_offset[1]);
	Dlg->RXShiftB->setText(valChar);

	sprintf(valChar, "%d", rcvr_offset[2]);
	Dlg->RXShiftC->setText(valChar);
	sprintf(valChar, "%d", rcvr_offset[3]);
	Dlg->RXShiftD->setText(valChar);

	//	speed[1]
	//	speed[2];

	Dlg->recoverBitA->setCurrentIndex(recovery[0]);
	Dlg->recoverBitB->setCurrentIndex(recovery[1]);
	Dlg->recoverBitC->setCurrentIndex(recovery[2]);
	Dlg->recoverBitD->setCurrentIndex(recovery[3]);

	Dlg->fx25ModeA->setCurrentIndex(fx25_mode[0]);
	Dlg->fx25ModeB->setCurrentIndex(fx25_mode[1]);
	Dlg->fx25ModeC->setCurrentIndex(fx25_mode[2]);
	Dlg->fx25ModeD->setCurrentIndex(fx25_mode[3]);

	Dlg->IL2PModeA->setCurrentIndex(il2p_mode[0]);
	Dlg->IL2PModeB->setCurrentIndex(il2p_mode[1]);
	Dlg->IL2PModeC->setCurrentIndex(il2p_mode[2]);
	Dlg->IL2PModeD->setCurrentIndex(il2p_mode[3]);

	Dlg->CRCTX_A->setChecked((il2p_crc[0] & 1));
	Dlg->CRCRX_A->setChecked((il2p_crc[0] & 2));
	Dlg->CRCTX_B->setChecked((il2p_crc[1] & 1));
	Dlg->CRCRX_B->setChecked((il2p_crc[1] & 2));
	Dlg->CRCTX_C->setChecked((il2p_crc[2] & 1));
	Dlg->CRCRX_C->setChecked((il2p_crc[2] & 2));
	Dlg->CRCTX_D->setChecked((il2p_crc[3] & 1));
	Dlg->CRCRX_D->setChecked((il2p_crc[3] & 2));

	Dlg->CWIDCall->setText(CWIDCall);
	Dlg->CWIDInterval->setText(QString::number(CWIDInterval));
	Dlg->CWIDMark->setText(CWIDMark);

	if (CWIDType)
		Dlg->radioButton_2->setChecked(1);
	else
		Dlg->CWIDType->setChecked(1);

	Dlg->afterTraffic->setChecked(afterTraffic);

	Dlg->RSIDSABM_A->setChecked(RSID_SABM[0]);
	Dlg->RSIDSABM_B->setChecked(RSID_SABM[1]);
	Dlg->RSIDSABM_C->setChecked(RSID_SABM[2]);
	Dlg->RSIDSABM_D->setChecked(RSID_SABM[3]);

	Dlg->RSIDUI_A->setChecked(RSID_UI[0]);
	Dlg->RSIDUI_B->setChecked(RSID_UI[1]);
	Dlg->RSIDUI_C->setChecked(RSID_UI[2]);
	Dlg->RSIDUI_D->setChecked(RSID_UI[3]);

	Dlg->DigiCallsA->setText(MyDigiCall[0]);
	Dlg->DigiCallsB->setText(MyDigiCall[1]);
	Dlg->DigiCallsC->setText(MyDigiCall[2]);
	Dlg->DigiCallsD->setText(MyDigiCall[3]);

	Dlg->RSID_1_SETMODEM->setChecked(RSID_SetModem[0]);
	Dlg->RSID_2_SETMODEM->setChecked(RSID_SetModem[1]);
	Dlg->RSID_3_SETMODEM->setChecked(RSID_SetModem[2]);
	Dlg->RSID_4_SETMODEM->setChecked(RSID_SetModem[3]);
	
	connect(Dlg->showBPF_A, SIGNAL(released()), this, SLOT(clickedSlot()));
	connect(Dlg->showTXBPF_A, SIGNAL(released()), this, SLOT(clickedSlot()));
	connect(Dlg->showLPF_A, SIGNAL(released()), this, SLOT(clickedSlot()));

	connect(Dlg->showBPF_B, SIGNAL(released()), this, SLOT(clickedSlot()));
	connect(Dlg->showTXBPF_B, SIGNAL(released()), this, SLOT(clickedSlot()));
	connect(Dlg->showLPF_B, SIGNAL(released()), this, SLOT(clickedSlot()));

	connect(Dlg->showBPF_C, SIGNAL(released()), this, SLOT(clickedSlot()));
	connect(Dlg->showTXBPF_C, SIGNAL(released()), this, SLOT(clickedSlot()));
	connect(Dlg->showLPF_C, SIGNAL(released()), this, SLOT(clickedSlot()));

	connect(Dlg->showBPF_D, SIGNAL(released()), this, SLOT(clickedSlot()));
	connect(Dlg->showTXBPF_D, SIGNAL(released()), this, SLOT(clickedSlot()));
	connect(Dlg->showLPF_D, SIGNAL(released()), this, SLOT(clickedSlot()));

	connect(Dlg->okButton, SIGNAL(clicked()), this, SLOT(modemaccept()));
	connect(Dlg->modemSave, SIGNAL(clicked()), this, SLOT(modemSave()));
	connect(Dlg->cancelButton, SIGNAL(clicked()), this, SLOT(modemreject()));

	connect(Dlg->SendRSID_1, SIGNAL(clicked()), this, SLOT(doRSIDA()));
	connect(Dlg->SendRSID_2, SIGNAL(clicked()), this, SLOT(doRSIDB()));
	connect(Dlg->SendRSID_3, SIGNAL(clicked()), this, SLOT(doRSIDC()));
	connect(Dlg->SendRSID_4, SIGNAL(clicked()), this, SLOT(doRSIDD()));

	connect(Dlg->preEmphAllA, SIGNAL(stateChanged(int)), this, SLOT(preEmphAllAChanged(int)));
	connect(Dlg->preEmphAllB, SIGNAL(stateChanged(int)), this, SLOT(preEmphAllBChanged(int)));
	connect(Dlg->preEmphAllC, SIGNAL(stateChanged(int)), this, SLOT(preEmphAllCChanged(int)));
	connect(Dlg->preEmphAllD, SIGNAL(stateChanged(int)), this, SLOT(preEmphAllDChanged(int)));

	UI.exec();
}

void QtSoundModem::preEmphAllAChanged(int state)
{
	Dlg->preEmphA->setDisabled(state);
}

void QtSoundModem::preEmphAllBChanged(int state)
{
	Dlg->preEmphB->setDisabled(state);
}

void QtSoundModem::preEmphAllCChanged(int state)
{
	Dlg->preEmphC->setDisabled(state);
}

void QtSoundModem::preEmphAllDChanged(int state)
{
	Dlg->preEmphD->setDisabled(state);
}

extern "C" void get_exclude_list(char * line, TStringList * list);

void QtSoundModem::modemaccept()
{
	modemSave();

	AGW_Report_Modem_Change(0);
	AGW_Report_Modem_Change(1);
	AGW_Report_Modem_Change(2);
	AGW_Report_Modem_Change(3);

	delete(Dlg);
	saveSettings();

	modemUI->accept();

}

void QtSoundModem::modemSave()
{
	QVariant Q;
	
	emph_all[0] = Dlg->preEmphAllA->isChecked();
	emph_db[0] = Dlg->preEmphA->currentIndex();

	emph_all[1] = Dlg->preEmphAllB->isChecked();
	emph_db[1] = Dlg->preEmphB->currentIndex();

	emph_all[2] = Dlg->preEmphAllC->isChecked();
	emph_db[2] = Dlg->preEmphC->currentIndex();

	emph_all[3] = Dlg->preEmphAllD->isChecked();
	emph_db[3] = Dlg->preEmphD->currentIndex();

	NonAX25[0] = Dlg->nonAX25A->isChecked();
	NonAX25[1] = Dlg->nonAX25B->isChecked();
	NonAX25[2] = Dlg->nonAX25C->isChecked();
	NonAX25[3] = Dlg->nonAX25D->isChecked();

	KISS_opt[0] = Dlg->KISSOptA->isChecked();
	KISS_opt[1] = Dlg->KISSOptB->isChecked();
	KISS_opt[2] = Dlg->KISSOptC->isChecked();
	KISS_opt[3] = Dlg->KISSOptD->isChecked();

	if (emph_db[0] < 0 || emph_db[0] > nr_emph)
		emph_db[0] = 0;

	if (emph_db[1] < 0 || emph_db[1] > nr_emph)
		emph_db[1] = 0;

	if (emph_db[2] < 0 || emph_db[2] > nr_emph)
		emph_db[2] = 0;

	if (emph_db[3] < 0 || emph_db[3] > nr_emph)
		emph_db[3] = 0;

	Q = Dlg->TXDelayA->text();
	txdelay[0] = Q.toInt();

	Q = Dlg->TXDelayB->text();
	txdelay[1] = Q.toInt();
	
	Q = Dlg->TXDelayC->text();
	txdelay[2] = Q.toInt();

	Q = Dlg->TXDelayD->text();
	txdelay[3] = Q.toInt();

	Q = Dlg->MaxFrameA->text();
	maxframe[0] = Q.toInt();

	Q = Dlg->MaxFrameB->text();
	maxframe[1] = Q.toInt();

	Q = Dlg->MaxFrameC->text();
	maxframe[2] = Q.toInt();

	Q = Dlg->MaxFrameD->text();
	maxframe[3] = Q.toInt();

	if (maxframe[0] == 0 || maxframe[0] > 7) maxframe[0] = 3;
	if (maxframe[1] == 0 || maxframe[1] > 7) maxframe[1] = 3;
	if (maxframe[2] == 0 || maxframe[2] > 7) maxframe[2] = 3;
	if (maxframe[3] == 0 || maxframe[3] > 7) maxframe[3] = 3;

	Q = Dlg->TXTailA->text();
	txtail[0] = Q.toInt();

	Q = Dlg->TXTailB->text();
	txtail[1] = Q.toInt();

	Q = Dlg->TXTailC->text();
	txtail[2] = Q.toInt();

	txtail[3] = Dlg->TXTailD->text().toInt();

	frack_time[0] = Dlg->FrackA->text().toInt();
	frack_time[1] = Dlg->FrackB->text().toInt();
	frack_time[2] = Dlg->FrackC->text().toInt();
	frack_time[3] = Dlg->FrackD->text().toInt();

	fracks[0] = Dlg->RetriesA->text().toInt();
	fracks[1] = Dlg->RetriesB->text().toInt();
	fracks[2] = Dlg->RetriesC->text().toInt();
	fracks[3] = Dlg->RetriesD->text().toInt();

	Q = Dlg->AddRXA->text();
	RCVR[0] = Q.toInt();

	Q = Dlg->AddRXB->text();
	RCVR[1] = Q.toInt();

	Q = Dlg->AddRXC->text();
	RCVR[2] = Q.toInt();

	Q = Dlg->AddRXD->text();
	RCVR[3] = Q.toInt();

	Q = Dlg->RXShiftA->text();
	rcvr_offset[0] = Q.toInt();

	Q = Dlg->RXShiftB->text();
	rcvr_offset[1] = Q.toInt();

	Q = Dlg->RXShiftC->text();
	rcvr_offset[2] = Q.toInt();

	Q = Dlg->RXShiftD->text();
	rcvr_offset[3] = Q.toInt();

	fx25_mode[0] = Dlg->fx25ModeA->currentIndex();
	fx25_mode[1] = Dlg->fx25ModeB->currentIndex();
	fx25_mode[2] = Dlg->fx25ModeC->currentIndex();
	fx25_mode[3] = Dlg->fx25ModeD->currentIndex();

	il2p_mode[0] = Dlg->IL2PModeA->currentIndex();
	il2p_mode[1] = Dlg->IL2PModeB->currentIndex();
	il2p_mode[2] = Dlg->IL2PModeC->currentIndex();
	il2p_mode[3] = Dlg->IL2PModeD->currentIndex();

	il2p_crc[0] = Dlg->CRCTX_A->isChecked();
	if (Dlg->CRCRX_A->isChecked())
		il2p_crc[0] |= 2;

	il2p_crc[1] = Dlg->CRCTX_B->isChecked();
	if (Dlg->CRCRX_B->isChecked())
		il2p_crc[1] |= 2;

	il2p_crc[2] = Dlg->CRCTX_C->isChecked();
	if (Dlg->CRCRX_C->isChecked())
		il2p_crc[2] |= 2;

	il2p_crc[3] = Dlg->CRCTX_D->isChecked();
	if (Dlg->CRCRX_D->isChecked())
		il2p_crc[3] |= 2;

	recovery[0] = Dlg->recoverBitA->currentIndex();
	recovery[1] = Dlg->recoverBitB->currentIndex();
	recovery[2] = Dlg->recoverBitC->currentIndex();
	recovery[3] = Dlg->recoverBitD->currentIndex();


	strcpy(CWIDCall, Dlg->CWIDCall->text().toUtf8().toUpper());
	strcpy(CWIDMark, Dlg->CWIDMark->text().toUtf8().toUpper());
	CWIDInterval = Dlg->CWIDInterval->text().toInt();
	CWIDType = Dlg->radioButton_2->isChecked();

	afterTraffic = Dlg->afterTraffic->isChecked();

	if (CWIDInterval && afterTraffic == false)
		cwidtimer->start(CWIDInterval * 60000);
	else
		cwidtimer->stop();


	RSID_SABM[0] = Dlg->RSIDSABM_A->isChecked();
	RSID_SABM[1] = Dlg->RSIDSABM_B->isChecked();
	RSID_SABM[2] = Dlg->RSIDSABM_C->isChecked();
	RSID_SABM[3] = Dlg->RSIDSABM_D->isChecked();

	RSID_UI[0] = Dlg->RSIDUI_A->isChecked();
	RSID_UI[1] = Dlg->RSIDUI_B->isChecked();
	RSID_UI[2] = Dlg->RSIDUI_C->isChecked();
	RSID_UI[3] = Dlg->RSIDUI_D->isChecked();

	RSID_SetModem[0] = Dlg->RSID_1_SETMODEM->isChecked();
	RSID_SetModem[1] = Dlg->RSID_2_SETMODEM->isChecked();
	RSID_SetModem[2] = Dlg->RSID_3_SETMODEM->isChecked();
	RSID_SetModem[3] = Dlg->RSID_4_SETMODEM->isChecked();

	Q = Dlg->DigiCallsA->text();
	strcpy(MyDigiCall[0], Q.toString().toUtf8().toUpper());

	Q = Dlg->DigiCallsB->text();
	strcpy(MyDigiCall[1], Q.toString().toUtf8().toUpper());

	Q = Dlg->DigiCallsC->text();
	strcpy(MyDigiCall[2], Q.toString().toUtf8().toUpper());

	Q = Dlg->DigiCallsD->text();
	strcpy(MyDigiCall[3], Q.toString().toUtf8().toUpper());

	int i;

	for (i = 0; i < 4; i++)
	{
		initTStringList(&list_digi_callsigns[i]);
		get_exclude_list(MyDigiCall[i], &list_digi_callsigns[i]);
	}

/*
	Q = Dlg->LPFWidthA->text();
	lpf[0] = Q.toInt();

	Q = Dlg->LPFWidthB->text();
	lpf[1] = Q.toInt();

	Q = Dlg->LPFWidthC->text();
	lpf[2] = Q.toInt();

	Q = Dlg->LPFWidthD->text();
	lpf[3] = Q.toInt();
*/

}

void QtSoundModem::modemreject()
{
	delete(Dlg);
	modemUI->reject();
}

void QtSoundModem::doRSIDA()
{
	needRSID[0] = 1;
}

void QtSoundModem::doRSIDB()
{
	needRSID[1] = 1;
}

void QtSoundModem::doRSIDC()
{
	needRSID[2] = 1;
}

void QtSoundModem::doRSIDD()
{
	needRSID[3] = 1;
}




void QtSoundModem::doFilter(int Chan, int Filter)
{
	Ui_Dialog Dev;
	QImage * bitmap;

	QDialog UI;

	Dev.setupUi(&UI);

	bitmap = new QImage(642, 312, QImage::Format_RGB32);

	bitmap->fill(qRgb(255, 255, 255));

	QPainter qPainter(bitmap);
	qPainter.setBrush(Qt::NoBrush);
	qPainter.setPen(Qt::black);

	if (Filter == 0)
		make_graph_buf(DET[0][0].BPF_core[Chan], BPF_tap[Chan], &qPainter);
	else if (Filter == 1)
		make_graph_buf(tx_BPF_core[Chan], tx_BPF_tap[Chan], &qPainter);
	else
		make_graph_buf(LPF_core[Chan], LPF_tap[Chan], &qPainter);

	qPainter.end();
	Dev.label->setPixmap(QPixmap::fromImage(*bitmap));

	UI.exec();

}

Ui_devicesDialog * Dev;

char NewPTTPort[80];

int newSoundMode = 0;
int oldSoundMode = 0;
int oldSnoopMix = 0;
int newSnoopMix = 0;

void QtSoundModem::SoundModeChanged(bool State)
{
	UNUSED(State);

	// Mustn't change SoundMode until dialog is accepted

	newSnoopMix = Dev->onlyMixSnoop->isChecked();

	if (Dev->QSOUND->isChecked())
		newSoundMode = 5;
	else if (Dev->UDP->isChecked())
		newSoundMode = 3;
	else if (Dev->PULSE->isChecked())
		newSoundMode = 2;
	else
		newSoundMode = Dev->OSS->isChecked();

}

void QtSoundModem::DualPTTChanged(bool State)
{
	UNUSED(State);

	// Forse Evaluation of Cat Port setting

	PTTPortChanged(0);
}

void QtSoundModem::CATChanged(bool State)
{
	UNUSED(State);
	PTTPortChanged(0);
}

void QtSoundModem::PTTPortChanged(int Selected)
{
	UNUSED(Selected);

	QVariant Q = Dev->PTTPort->currentText();
	strcpy(NewPTTPort, Q.toString().toUtf8());

	Dev->RTSDTR->setVisible(false);
	Dev->CAT->setVisible(false);
	Dev->RTS->setVisible(false);
	Dev->DTR->setVisible(false);

	Dev->PTTOnLab->setVisible(false);
	Dev->PTTOn->setVisible(false);
	Dev->PTTOff->setVisible(false);
	Dev->PTTOffLab->setVisible(false);
	Dev->CATLabel->setVisible(false);
	Dev->CATSpeed->setVisible(false);

	Dev->GPIOLab->setVisible(false);
	Dev->GPIOLeft->setVisible(false);
	Dev->GPIORight->setVisible(false);
	Dev->GPIOLab2->setVisible(false);

	Dev->CM108Label->setVisible(false);
	Dev->VIDPID->setVisible(false);

	if (strcmp(NewPTTPort, "None") == 0)
	{
	}
	else if (strcmp(NewPTTPort, "GPIO") == 0)
	{
		Dev->GPIOLab->setVisible(true);
		Dev->GPIOLeft->setVisible(true);
		if (Dev->DualPTT->isChecked())
		{
			Dev->GPIORight->setVisible(true);
			Dev->GPIOLab2->setVisible(true);
		}
	}

	else if (strcmp(NewPTTPort, "CM108") == 0)
	{
		Dev->CM108Label->setVisible(true);
//#ifdef __ARM_ARCHX
		Dev->CM108Label->setText("CM108 Device");
//#else
//		Dev->CM108Label->setText("CM108 VID/PID");
//#endif
		Dev->VIDPID->setText(CM108Addr);
		Dev->VIDPID->setVisible(true);
	}
	else if (strcmp(NewPTTPort, "HAMLIB") == 0)
	{
		Dev->CM108Label->setVisible(true);
		Dev->CM108Label->setText("rigctrld Port");
		Dev->VIDPID->setText(QString::number(HamLibPort));
		Dev->VIDPID->setVisible(true);
		Dev->PTTOnLab->setText("rigctrld Host");
		Dev->PTTOnLab->setVisible(true);
		Dev->PTTOn->setText(HamLibHost);
		Dev->PTTOn->setVisible(true);
	}
	else if (strcmp(NewPTTPort, "FLRIG") == 0)
	{
		Dev->CM108Label->setVisible(true);
		Dev->CM108Label->setText("FLRig Port");
		Dev->VIDPID->setText(QString::number(FLRigPort));
		Dev->VIDPID->setVisible(true);
		Dev->PTTOnLab->setText("FLRig Host");
		Dev->PTTOnLab->setVisible(true);
		Dev->PTTOn->setText(FLRigHost);
		Dev->PTTOn->setVisible(true);
	}
	else
	{
		Dev->RTSDTR->setVisible(true);
		Dev->RTS->setVisible(true);
		Dev->DTR->setVisible(true);
		Dev->CAT->setVisible(true);

		if (Dev->CAT->isChecked())
		{
			Dev->PTTOnLab->setVisible(true);
			Dev->PTTOnLab->setText("PTT On String");
			Dev->PTTOn->setText(PTTOnString);
			Dev->PTTOn->setVisible(true);
			Dev->PTTOff->setVisible(true);
			Dev->PTTOff->setText(PTTOffString);
			Dev->PTTOffLab->setVisible(true);
			Dev->CATLabel->setVisible(true);
			Dev->CATSpeed->setVisible(true);
		}
	}
}

bool myResize::eventFilter(QObject *obj, QEvent *event)
{
	if (event->type() == QEvent::Resize)
	{
		QResizeEvent *resizeEvent = static_cast<QResizeEvent *>(event);
		QSize size = resizeEvent->size();
		int h = size.height();
		int w = size.width();

		if (obj == deviceUI)
			Dev->scrollArea->setGeometry(QRect(5, 5, w - 10, h - 10));
		else
			Dlg->scrollArea->setGeometry(QRect(5, 5, w - 10, h - 10));

		return true;
	}
	return QObject::eventFilter(obj, event);
}

void QtSoundModem::doDevices()
{
	char valChar[10];
	QStringList items;

	Dev = new(Ui_devicesDialog);

	QDialog UI;

	int i;

	Dev->setupUi(&UI);

	deviceUI = &UI;
	modemUI = 0;

	myResize *resize = new myResize();

	UI.installEventFilter(resize);

	// Set serial names

	for (const QSerialPortInfo &info : Ports)
	{
		items.append(info.portName());
	}

	items.sort();

	Dev->SixPackSerial->addItem("None");

	for (const QString &info : items)
	{
		Dev->SixPackSerial->addItem(info);
	}

	newSoundMode = SoundMode;
	oldSoundMode = SoundMode;
	oldSnoopMix = newSnoopMix = onlyMixSnoop;

	if (SoundMode == 5)
	{
		Debugprintf("CaptureCount %d PlaybackCount %d", CaptureCount, PlaybackCount);
//		GetAudioDevices();
		Dev->QSOUND->setChecked(1);
	}

#ifdef WIN32
	Dev->ALSA->setText("WaveOut");
	Dev->OSS->setVisible(0);
	Dev->PULSE->setVisible(0);
	Dev->onlyMixSnoop->setVisible(0);

	if (SoundMode == 0)
		Dev->ALSA->setChecked(1);
	else if (SoundMode == 2)
		Dev->UDP->setChecked(1);
#elif defined(Q_OS_MACOS)
	// Qt audio (SoundMode 5) is the only working backend on macOS.
	// Hide the other radios so the user cannot pick a mode whose
	// backend is not built; QSOUND stays the only option.
	Dev->ALSA->setVisible(0);
	Dev->OSS->setVisible(0);
	Dev->PULSE->setVisible(0);
	Dev->UDP->setVisible(0);
	Dev->onlyMixSnoop->setVisible(0);
	Dev->QSOUND->setChecked(1);
#else
	if (SoundMode == 0)
	{
		Dev->onlyMixSnoop->setVisible(1);
		Dev->ALSA->setChecked(1);
	}
	else if (SoundMode == 1)
		Dev->OSS->setChecked(1);
	else if (SoundMode == 2)
		Dev->PULSE->setChecked(1);
	else if (SoundMode == 2)
		Dev->UDP->setChecked(1);
#endif

	Dev->onlyMixSnoop->setChecked(onlyMixSnoop);

	connect(Dev->ALSA, SIGNAL(toggled(bool)), this, SLOT(SoundModeChanged(bool)));
	connect(Dev->OSS, SIGNAL(toggled(bool)), this, SLOT(SoundModeChanged(bool)));
	connect(Dev->PULSE, SIGNAL(toggled(bool)), this, SLOT(SoundModeChanged(bool)));
	connect(Dev->UDP, SIGNAL(toggled(bool)), this, SLOT(SoundModeChanged(bool)));
	connect(Dev->onlyMixSnoop, SIGNAL(toggled(bool)), this, SLOT(SoundModeChanged(bool)));

	for (i = 0; i < PlaybackCount; i++)
		Dev->outputDevice->addItem(&PlaybackNames[i][0]);

	i = Dev->outputDevice->findText(PlaybackDevice, Qt::MatchContains);


	if (i == -1)
	{
		// Add device to list

		Dev->outputDevice->addItem(PlaybackDevice);
		i = Dev->outputDevice->findText(PlaybackDevice, Qt::MatchContains);
	}

	Dev->outputDevice->setCurrentIndex(i);

	for (i = 0; i < CaptureCount; i++)
		Dev->inputDevice->addItem(&CaptureNames[i][0]);

	i = Dev->inputDevice->findText(CaptureDevice, Qt::MatchContains);

	if (i == -1)
	{
		// Add device to list

		Dev->inputDevice->addItem(CaptureDevice);
		i = Dev->inputDevice->findText(CaptureDevice, Qt::MatchContains);
	}
	Dev->inputDevice->setCurrentIndex(i);

	Dev->txLatency->setText(QString::number(txLatency));

	Dev->Modem_1_Chan->setCurrentIndex(soundChannel[0]);
	Dev->Modem_2_Chan->setCurrentIndex(soundChannel[1]);
	Dev->Modem_3_Chan->setCurrentIndex(soundChannel[2]);
	Dev->Modem_4_Chan->setCurrentIndex(soundChannel[3]);

	// Disable "None" option in first modem

	QStandardItemModel *model = dynamic_cast<QStandardItemModel *>(Dev->Modem_1_Chan->model());
	QStandardItem * item = model->item(0, 0);
	item->setEnabled(false);

	Dev->useKISSControls->setChecked(useKISSControls);
	Dev->singleChannelOutput->setChecked(SCO);
	Dev->colourWaterfall->setChecked(raduga);

	sprintf(valChar, "%d", KISSPort);
	Dev->KISSPort->setText(valChar);
	Dev->KISSEnabled->setChecked(KISSServ);

	sprintf(valChar, "%d", AGWPort);
	Dev->AGWPort->setText(valChar);
	Dev->AGWEnabled->setChecked(AGWServ);

	sprintf(valChar, "%d", RHPPort);
	Dev->RHPPort->setText(valChar);
	Dev->RHPEnabled->setChecked(RHPServ);


	Dev->MgmtPort->setText(QString::number(MgmtPort));

	// If we are using a user specifed device add it

	i = Dev->SixPackSerial->findText(SixPackDevice, Qt::MatchFixedString);

	if (i == -1)
	{
		// Add our device to list

		Dev->SixPackSerial->insertItem(0, SixPackDevice);
		i = Dev->SixPackSerial->findText(SixPackDevice, Qt::MatchContains);
	}

	Dev->SixPackSerial->setCurrentIndex(i);

	sprintf(valChar, "%d", SixPackPort);
	Dev->SixPackTCP->setText(valChar);
	Dev->SixPackEnable->setChecked(SixPackEnable);

	Dev->PTTOn->setText(PTTOnString);
	Dev->PTTOff->setText(PTTOffString);

	sprintf(valChar, "%d", PTTBAUD);
	Dev->CATSpeed->setText(valChar);

	sprintf(valChar, "%d", UDPClientPort);
	Dev->UDPPort->setText(valChar);
	Dev->UDPTXHost->setText(UDPHost);

	if (UDPServerPort != TXPort)
		sprintf(valChar, "%d/%d", UDPServerPort, TXPort);
	else
		sprintf(valChar, "%d", UDPServerPort);

	Dev->UDPTXPort->setText(valChar);

	Dev->UDPEnabled->setChecked(UDPServ);

	sprintf(valChar, "%d", pttGPIOPin);
	Dev->GPIOLeft->setText(valChar);
	sprintf(valChar, "%d", pttGPIOPinR);
	Dev->GPIORight->setText(valChar);

	Dev->VIDPID->setText(CM108Addr);

	connect(Dev->CAT, SIGNAL(toggled(bool)), this, SLOT(CATChanged(bool)));
	connect(Dev->DualPTT, SIGNAL(toggled(bool)), this, SLOT(DualPTTChanged(bool)));
	connect(Dev->PTTPort, SIGNAL(currentIndexChanged(int)), this, SLOT(PTTPortChanged(int)));

	if (PTTMode == PTTCAT)
		Dev->CAT->setChecked(true);
	else if (PTTMode == PTTRTS)
		Dev->RTS->setChecked(true);
	else if (PTTMode == PTTDTR)
		Dev->DTR->setChecked(true); 
	else
		Dev->RTSDTR->setChecked(true);

	Dev->PTTPort->addItem("None");
	Dev->PTTPort->addItem("CM108");

	//#ifdef __ARM_ARCH

	Dev->PTTPort->addItem("GPIO");

	//#endif

	Dev->PTTPort->addItem("HAMLIB");
	Dev->PTTPort->addItem("FLRIG");

	for (const QString &info : items)
	{
		Dev->PTTPort->addItem(info);
	}

	// If we are using a user specifed device add it

	i = Dev->PTTPort->findText(PTTPort, Qt::MatchFixedString);

	if (i == -1)
	{
		// Add our device to list

		Dev->PTTPort->insertItem(0, PTTPort);
		i = Dev->PTTPort->findText(PTTPort, Qt::MatchContains);
	}

	Dev->PTTPort->setCurrentIndex(i);

	PTTPortChanged(0);				// Force reevaluation

	Dev->txRotation->setChecked(TX_rotate);
	Dev->DualPTT->setChecked(DualPTT);

	Dev->multiCore->setChecked(multiCore);

	Dev->WaterfallMin->setCurrentIndex(Dev->WaterfallMin->findText(QString::number(WaterfallMin), Qt::MatchFixedString));
	Dev->WaterfallMax->setCurrentIndex(Dev->WaterfallMax->findText(QString::number(WaterfallMax), Qt::MatchFixedString));

	QObject::connect(Dev->okButton, SIGNAL(clicked()), this, SLOT(deviceaccept()));
	QObject::connect(Dev->cancelButton, SIGNAL(clicked()), this, SLOT(devicereject()));

	UI.exec();

}

void QtSoundModem::mysetstyle()
{
	if (darkTheme)
	{
		qApp->setStyleSheet(
			"QWidget {color: white; background-color: black}"
			"QTabBar::tab {color: rgb(127, 127, 127); background-color: black}"
			"QTabBar::tab::selected {color: white}"
			"QPushButton {border-style: outset; border-width: 2px; border-color: rgb(127, 127, 127)}"
			"QPushButton::default {border-style: outset; border-width: 2px; border-color: white}");

		sessionTable->setStyleSheet("QHeaderView::section { background-color:rgb(40, 40, 40) }");

		txText = qRgb(255, 127, 127);
		rxText = qRgb(173, 216, 230);
	}
	else
	{
		qApp->setStyleSheet("");

		sessionTable->setStyleSheet("QHeaderView::section { background-color:rgb(224, 224, 224) }");

		txText = qRgb(192, 0, 0);
		rxText = qRgb(0, 0, 192);
	}
}

void QtSoundModem::deviceaccept()
{
	QVariant Q = Dev->inputDevice->currentText();
	int cardChanged = 0;
	char portString[32];
	int newMax;
	int newMin;

	if (Dev->UDP->isChecked())
	{
		// cant have server and slave

		if (Dev->UDPEnabled->isChecked())
		{
			QMessageBox::about(this, tr("QtSoundModem"),
				tr("Can't have UDP sound source and UDP server at same time"));
			return;
		}
	}

	if (oldSoundMode != newSoundMode || oldSnoopMix != newSnoopMix)
	{
		QMessageBox msgBox;

		msgBox.setText("QtSoundModem must restart to change Sound Mode.\n"
			"Program will close if you hit Ok\n"
			"You will need to reselect audio devices after restarting");

		msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);

		int i = msgBox.exec();

		if (i == QMessageBox::Ok)
		{			
			SoundMode = newSoundMode;
			onlyMixSnoop = newSnoopMix;
			saveSettings();

			Closing = 1;
			return;
		}

		if (oldSoundMode == 0)
			Dev->ALSA->setChecked(1);
		else if (oldSoundMode == 1)
			Dev->OSS->setChecked(1);
		else if (oldSoundMode == 2)
			Dev->PULSE->setChecked(1);
		else if (oldSoundMode == 3)
			Dev->UDP->setChecked(1);

		QMessageBox::about(this, tr("Info"),
			tr("<p align = 'center'>Changes not saved</p>"));

		return;

	}

	{
		QByteArray captureUtf8 = Q.toString().toUtf8();
		if (strcmp(CaptureDevice, captureUtf8.constData()) != 0)
		{
			qstrncpy(CaptureDevice, captureUtf8.constData(), sizeof(CaptureDevice));
			cardChanged = 1;
		}
	}

	if (onlyMixSnoop != Dev->onlyMixSnoop->isChecked())
	{
		onlyMixSnoop = Dev->onlyMixSnoop->isChecked();
		cardChanged = 1;
	}

	CaptureIndex = Dev->inputDevice->currentIndex();

#if defined(Q_OS_MACOS)
	// Capture the stable id alongside the description. Picking a
	// different duplicate-named device only changes the id (the
	// description string is identical), so the id-changed test is
	// the only thing that trips cardChanged for that case — without
	// it the in-session reopen below is skipped and the new device
	// is not opened until the next launch.
	// Combo index lines up with the filtered list (which has surround
	// devices skipped, mirroring the combo population). Using the
	// unfiltered list here would silently shift selections by the count
	// of surround entries before this device, so the saved id is for
	// the wrong device.
	if (CaptureIndex >= 0 && CaptureIndex < inputDevicesFiltered.size())
	{
		QByteArray idB64 = inputDevicesFiltered[CaptureIndex].id().toBase64();
		if (idB64.size() >= (qsizetype)sizeof(CaptureDeviceId))
		{
			Debugprintf("WARNING: capture device id (%lld bytes) exceeds "
				"%zu-byte buffer; truncated id will not match on next "
				"launch and persistence will fall back to description match",
				(long long)idB64.size(), sizeof(CaptureDeviceId));
		}
		if (strcmp(CaptureDeviceId, idB64.constData()) != 0)
		{
			qstrncpy(CaptureDeviceId, idB64.constData(), sizeof(CaptureDeviceId));
			cardChanged = 1;
		}
	}
#endif

	Q = Dev->outputDevice->currentText();

	{
		QByteArray playbackUtf8 = Q.toString().toUtf8();
		if (strcmp(PlaybackDevice, playbackUtf8.constData()) != 0)
		{
			qstrncpy(PlaybackDevice, playbackUtf8.constData(), sizeof(PlaybackDevice));
			cardChanged = 1;
		}
	}

	PlayBackIndex = Dev->outputDevice->currentIndex();

#if defined(Q_OS_MACOS)
	// See CaptureDeviceId comment above for the cardChanged rationale.
	// Index against the filtered list (combo is populated from
	// PlaybackNames, which skips surround entries); see CaptureIndex
	// site above for the rationale.
	if (PlayBackIndex >= 0 && PlayBackIndex < outputDevicesFiltered.size())
	{
		QByteArray idB64 = outputDevicesFiltered[PlayBackIndex].id().toBase64();
		if (idB64.size() >= (qsizetype)sizeof(PlaybackDeviceId))
		{
			Debugprintf("WARNING: playback device id (%lld bytes) exceeds "
				"%zu-byte buffer; truncated id will not match on next "
				"launch and persistence will fall back to description match",
				(long long)idB64.size(), sizeof(PlaybackDeviceId));
		}
		if (strcmp(PlaybackDeviceId, idB64.constData()) != 0)
		{
			qstrncpy(PlaybackDeviceId, idB64.constData(), sizeof(PlaybackDeviceId));
			cardChanged = 1;
		}
	}
#endif

	Q = Dev->txLatency->text();
	txLatency = Q.toInt();

	soundChannel[0] = Dev->Modem_1_Chan->currentIndex();
	soundChannel[1] = Dev->Modem_2_Chan->currentIndex();
	soundChannel[2] = Dev->Modem_3_Chan->currentIndex();
	soundChannel[3] = Dev->Modem_4_Chan->currentIndex();

	UsingLeft = 0;
	UsingRight = 0;
	UsingBothChannels = 0;

	for (int i = 0; i < 4; i++)
	{
		if (soundChannel[i] == LEFT)
		{
			UsingLeft = 1;
			modemtoSoundLR[i] = 0;
		}
		else if (soundChannel[i] == RIGHT)
		{
			UsingRight = 1;
			modemtoSoundLR[i] = 1;
		}
	}

	if (UsingLeft && UsingRight)
		UsingBothChannels = 1;


	useKISSControls = Dev->useKISSControls->isChecked();
	SCO = Dev->singleChannelOutput->isChecked();
	raduga = Dev->colourWaterfall->isChecked();
	AGWServ = Dev->AGWEnabled->isChecked();
	KISSServ = Dev->KISSEnabled->isChecked();
	RHPServ = Dev->RHPEnabled->isChecked();

	Q = Dev->KISSPort->text();
	KISSPort = Q.toInt();

	Q = Dev->AGWPort->text();
	AGWPort = Q.toInt();

	Q = Dev->RHPPort->text();
	RHPPort = Q.toInt();

	Q = Dev->MgmtPort->text();
	MgmtPort = Q.toInt();

	Q = Dev->SixPackSerial->currentText();

	char temp[256];

	strcpy(temp, Q.toString().toUtf8());

	if (strlen(temp))
		strcpy(SixPackDevice, temp);

	Q = Dev->SixPackTCP->text();
	SixPackPort = Q.toInt();

	SixPackEnable = Dev->SixPackEnable->isChecked();


	Q = Dev->PTTPort->currentText();
	

	strcpy(temp, Q.toString().toUtf8());

	if (strlen(temp))
		strcpy(PTTPort, temp);

	DualPTT = Dev->DualPTT->isChecked();
	TX_rotate = Dev->txRotation->isChecked();
	multiCore = Dev->multiCore->isChecked();

	if (Dev->RTS->isChecked())
		PTTMode = PTTRTS;
	else  if (Dev->DTR->isChecked())
		PTTMode = PTTDTR;
	else if (Dev->CAT->isChecked())
		PTTMode = PTTCAT;
	else if (Dev->RTSDTR->isChecked())
		PTTMode = PTTRTSDTR;

	Q = Dev->PTTOn->text();
	strcpy(PTTOnString, Q.toString().toUtf8());
	Q = Dev->PTTOff->text();
	strcpy(PTTOffString, Q.toString().toUtf8());

	Q = Dev->CATSpeed->text();
	PTTBAUD = Q.toInt();

	Q = Dev->UDPPort->text();
	UDPClientPort = Q.toInt();


	Q = Dev->UDPTXPort->text();
	strcpy(portString, Q.toString().toUtf8());
	UDPServerPort = atoi(portString);

	if (strchr(portString, '/'))
	{
		char * ptr = strlop(portString, '/');
		TXPort = atoi(ptr);
	}
	else
		TXPort = UDPServerPort;

	Q = Dev->UDPTXHost->text();
	strcpy(UDPHost, Q.toString().toUtf8());

	UDPServ = Dev->UDPEnabled->isChecked();

	Q = Dev->GPIOLeft->text();
	pttGPIOPin = Q.toInt();

	Q = Dev->GPIORight->text();
	pttGPIOPinR = Q.toInt();

	Q = Dev->VIDPID->text();

	if (strcmp(PTTPort, "CM108") == 0)
		strcpy(CM108Addr, Q.toString().toUtf8());
	else if (strcmp(PTTPort, "HAMLIB") == 0)
	{
		HamLibPort = Q.toInt();
		Q = Dev->PTTOn->text();
		strcpy(HamLibHost, Q.toString().toUtf8());
	}
	else if (strcmp(PTTPort, "FLRIG") == 0)
	{
		FLRigPort = Q.toInt();
		Q = Dev->PTTOn->text();
		strcpy(FLRigHost, Q.toString().toUtf8());
	}

	Q = Dev->WaterfallMax->currentText();
	newMax = Q.toInt();

	Q = Dev->WaterfallMin->currentText();
	newMin = Q.toInt();

	if (newMax != WaterfallMax || newMin != WaterfallMin)
	{
		QMessageBox msgBox;

		msgBox.setText("QtSoundModem must restart to change Waterfall range. Program will close if you hit Ok\n"
		"It may take up to 30 seconds for the program to start for the first time after changing settings");

		msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);

		int i = msgBox.exec();

		if (i == QMessageBox::Ok)
		{
			Configuring = 1;			// Stop Waterfall

			WaterfallMax = newMax;
			WaterfallMin = newMin;
			saveSettings();
			Closing = 1;
			return;
		}
	}


	ClosePTTPort();
	OpenPTTPort();

	NeedWaterfallHeaders = true;

	delete(Dev);
	saveSettings();
	deviceUI->accept();

	if (cardChanged)
	{
		if (SoundMode == 5)
		{
			// A hot-unplug delivered while the Devices dialog was open
			// could have shrunk inputDevicesFiltered / outputDevicesFiltered
			// through onAudioDevicesChanged → GetAudioDevices, leaving the
			// indices we captured at OK time pointing past the end.
			// Skip the reopen rather than crashing in operator[]; the
			// user can reopen the dialog and re-pick from the current
			// list. Settings are already saved at this point so the
			// id-based persistence will pick up the right device on
			// next launch.
			//
			// Index against the *filtered* lists — CaptureIndex came
			// from a combo populated from CaptureNames (surround
			// entries skipped), so the unfiltered inputDevices list
			// would shift selections by the count of skipped entries.
			if (CaptureIndex < 0 || CaptureIndex >= inputDevicesFiltered.size() ||
				PlayBackIndex < 0 || PlayBackIndex >= outputDevicesFiltered.size())
			{
				Debugprintf("Audio device list changed while Devices dialog "
					"was open (CaptureIndex=%d, count=%lld; PlayBackIndex=%d, "
					"count=%lld); skipping reopen — please re-pick from "
					"Settings → Devices",
					CaptureIndex, (long long)inputDevicesFiltered.size(),
					PlayBackIndex, (long long)outputDevicesFiltered.size());
			}
			else
			{
				closeQSound();

				inDeviceInfo = inputDevicesFiltered[CaptureIndex];
				outDeviceInfo = outputDevicesFiltered[PlayBackIndex];
				initializeAudioOut(outDeviceInfo);
				initializeAudioIn(inDeviceInfo);
			}

			// QtSoundInit() was called here historically but it
			// re-runs initializeAudio{In,Out} a second time against
			// the same just-opened devices. CoreAudio refuses the
			// second open silently on hardware devices (e.g. Yaesu
			// FT-710 USB codec, USB sound dongles), leaving the
			// app with no working stream. The one-time setup that
			// QtSoundInit owns (mic permission probe, DMABuffer
			// wire-up, QMediaDevices change signals) has already
			// run from the QtSoundModem constructor — re-running
			// it on every device switch is wrong.
		}

		else
			InitSound(1);
	}

	// Reset title and tooltip in case ports changed 

	char Title[128];
	sprintf(Title, "QtSoundModem Version %s Ports %d%s/%d%s", VersionString, AGWPort, AGWServ ? "*": "", KISSPort, KISSServ ? "*" : "");
	w->setWindowTitle(Title);

	sprintf(Title, "QtSoundModem %d %d", AGWPort, KISSPort);
	if (trayIcon)
		trayIcon->setToolTip(Title);

	QSize newSize(this->size());
	QSize oldSize(this->size());

	QResizeEvent *myResizeEvent = new QResizeEvent(newSize, oldSize);

	QCoreApplication::postEvent(this, myResizeEvent);  
}

void QtSoundModem::devicereject()
{	
	delete(Dev);
	deviceUI->reject();
}

void QtSoundModem::handleButton(int Port, int Type)
{
	// interlock calib with CWID

	if (calib_mode[0] == 4)		// CWID
		return;
	
	doCalib(Port, Type);
}





void QtSoundModem::doAbout()
{
	QMessageBox::about(this, tr("About"),
		tr("G8BPQ's port of UZ7HO's Soundmodem\n\nCopyright (C) 2019-2020 Andrei Kopanchuk UZ7HO"));
}

void QtSoundModem::doCalibrate()
{
	Ui_calDialog Calibrate;
	{
		QDialog UI;
		QRect MainRect = geometry();
		QRect Rect = {MainRect.x() + 80, MainRect.y() + 150, 270, 453};

		Calibrate.setupUi(&UI);

		connect(Calibrate.Low_A, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.High_A, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.Both_A, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.Stop_A, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.Low_B, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.High_B, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.Both_B, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.Stop_B, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.Low_C, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.High_C, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.Both_C, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.Stop_C, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.Low_D, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.High_D, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.Both_D, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.Stop_D, SIGNAL(released()), this, SLOT(clickedSlot()));
		connect(Calibrate.Cal1500, SIGNAL(released()), this, SLOT(clickedSlot()));

		/*
		
		connect(Calibrate.Low_A, &QPushButton::released, this, [=] { handleButton(0, 1); });
		connect(Calibrate.High_A, &QPushButton::released, this, [=] { handleButton(0, 2); });
		connect(Calibrate.Both_A, &QPushButton::released, this, [=] { handleButton(0, 3); });
		connect(Calibrate.Stop_A, &QPushButton::released, this, [=] { handleButton(0, 0); });
		connect(Calibrate.Low_B, &QPushButton::released, this, [=] { handleButton(1, 1); });
		connect(Calibrate.High_B, &QPushButton::released, this, [=] { handleButton(1, 2); });
		connect(Calibrate.Both_B, &QPushButton::released, this, [=] { handleButton(1, 3); });
		connect(Calibrate.Stop_B, &QPushButton::released, this, [=] { handleButton(1, 0); });

//		connect(Calibrate.High_A, SIGNAL(released()), this, SLOT(handleButton(1, 2)));
*/
		UI.setGeometry(Rect);
		UI.resize(270, 453);
		UI.exec();
	}
}

void QtSoundModem::RefreshSpectrum(unsigned char * Data)
{
	int i;

	// Last 4 bytes are level busy and Tuning lines

	Waterfall->fill(Black);

	if (Data[206] != LastLevel)
	{
		LastLevel = Data[206];
//		RefreshLevel(LastLevel);
	}

	if (Data[207] != LastBusy)
	{
		LastBusy = Data[207];
//		Busy->setVisible(LastBusy);
	}

	for (i = 0; i < 205; i++)
	{
		int val = Data[0];

		if (val > 63)
			val = 63;

		Waterfall->setPixel(i, val, Yellow);
		if (val < 62)
			Waterfall->setPixel(i, val + 1, Gold);
		Data++;
	}

	ui.Waterfall->setPixmap(QPixmap::fromImage(*Waterfall));

}

void RefreshLevel(unsigned int Level, unsigned int LevelR)
{
	// Redraw the RX Level Bar Graph

	unsigned int  x, y;

	for (x = 0; x < 150; x++)
	{
		for (y = 0; y < 10; y++)
		{
			if (x < Level)
			{
				if (Level < 50)
					RXLevel->setPixel(x, y, yellow);
				else if (Level > 135)
					RXLevel->setPixel(x, y, red);
				else
					RXLevel->setPixel(x, y, green);
			}
			else
				RXLevel->setPixel(x, y, white);
		}
	}
//	RXLevelCopy->setPixmap(QPixmap::fromImage(*RXLevel));

	for (x = 0; x < 150; x++)
	{
		for (y = 0; y < 10; y++)
		{
			if (x < LevelR)
			{
				if (LevelR < 50)
					RXLevel2->setPixel(x, y, yellow);
				else if (LevelR > 135)
					RXLevel2->setPixel(x, y, red);
				else
					RXLevel2->setPixel(x, y, green);
			}
			else
				RXLevel2->setPixel(x, y, white);
		}
	}

	emit t->setLevelImage();

///	RXLevel2Copy->setPixmap(QPixmap::fromImage(*RXLevel2));
}

extern "C" unsigned char CurrentLevel;
extern "C" unsigned char CurrentLevelR;

void QtSoundModem::RefreshWaterfall(int snd_ch, unsigned char * Data)
{
	int j;
	unsigned char * Line;
	int len = Waterfall->bytesPerLine();
	int TopLine = NextWaterfallLine[snd_ch];

	// Write line to cyclic buffer then draw starting with the line just written

	// Length is 208 bytes, including Level and Busy flags

	memcpy(&WaterfallLines[snd_ch][NextWaterfallLine[snd_ch]++][0], Data, 206);
	if (NextWaterfallLine[snd_ch] > 63)
		NextWaterfallLine[snd_ch] = 0;

	for (j = 63; j > 0; j--)
	{
		Line = Waterfall->scanLine(j);
		memcpy(Line, &WaterfallLines[snd_ch][TopLine++][0], len);
		if (TopLine > 63)
			TopLine = 0;
	}

	ui.Waterfall->setPixmap(QPixmap::fromImage(*Waterfall));
}

void QtSoundModem::sendtoTrace(char * Msg, int tx)
{
	const QTextCursor old_cursor = monWindowCopy->textCursor();
	const int old_scrollbar_value = monWindowCopy->verticalScrollBar()->value();
	const bool is_scrolled_down = old_scrollbar_value == monWindowCopy->verticalScrollBar()->maximum();

	// Move the cursor to the end of the document.
	monWindowCopy->moveCursor(QTextCursor::End);

	// Insert the text at the position of the cursor (which is the end of the document).

	if (tx)
		monWindowCopy->setTextColor(txText);
	else
		monWindowCopy->setTextColor(rxText);

	monWindowCopy->textCursor().insertText(Msg);

	if (old_cursor.hasSelection() || !is_scrolled_down)
	{
		// The user has selected text or scrolled away from the bottom: maintain position.
		monWindowCopy->setTextCursor(old_cursor);
		monWindowCopy->verticalScrollBar()->setValue(old_scrollbar_value);
	}
	else
	{
		// The user hasn't selected any text and the scrollbar is at the bottom: scroll to the bottom.
		monWindowCopy->moveCursor(QTextCursor::End);
		monWindowCopy->verticalScrollBar()->setValue(monWindowCopy->verticalScrollBar()->maximum());
	}

	free(Msg);
}


// I think this does the waterfall

typedef struct TRGBQ_t
{
	Byte b, g, r, re;

} TRGBWQ;

typedef struct tagRECT
{
	int    left;
	int    top;
	int    right;
	int    bottom;
} RECT;

unsigned int RGBWF[256] ;


extern "C" void init_raduga()
{
	Byte offset[6] = {0, 51, 102, 153, 204};
	Byte i, n;

	for (n = 0; n < 52; n++)
	{
		i = n * 5;

		RGBWF[n + offset[0]] = qRgb(0, 0, i);
		RGBWF[n + offset[1]] = qRgb(0, i, 255);
		RGBWF[n + offset[2]] = qRgb(0, 255, 255 - i);
		RGBWF[n + offset[3]] = qRgb(1, 255, 0);
		RGBWF[n + offset[4]] = qRgb(255, 255 - 1, 0);
	}
}

extern "C" int nonGUIMode;


// This draws the Frequency Scale on Waterfall

extern "C" void DrawFreqTicks()
{
	if (nonGUIMode)
		return;

	// Draw Frequency Markers on waterfall header(s);

	int x, i;
	char Textxx[20];
	QImage * bm = Waterfall;

	QPainter qPainter(bm);
	qPainter.setBrush(Qt::black);
	qPainter.setPen(Qt::white);

	int Chan;

#ifdef WIN32
		int Top = 3;
#else
		int Top = 4;
#endif
		int Base = 0;

		for (Chan = 0; Chan < 2; Chan++)
		{
			if (Chan == 1 || ((UsingBothChannels == 0) && (UsingRight == 1)))
				sprintf(Textxx, "Right");
			else
				sprintf(Textxx, "Left");

			qPainter.drawText(2, Top, 100, 20, 0, Textxx);

			// We drew markers every 100 Hz or 100 / binsize pixels

			int Markers = ((WaterfallMax - WaterfallMin) / 100) + 5;			// Number of Markers to draw
			int Freq = WaterfallMin;
			float PixelsPerMarker = 100.0 / BinSize;

			for (i = 0; i < Markers; i++)
			{
				x = round(PixelsPerMarker * i);
				if (x < 1025)
				{
					if ((Freq % 500) == 0)
						qPainter.drawLine(x, Base + 22, x, Base + 15);
					else
						qPainter.drawLine(x, Base + 22, x, Base + 18);

					if ((Freq % 500) == 0)
					{
						sprintf(Textxx, "%d", Freq);

						if (x < 924)
							qPainter.drawText(x - 12, Top, 100, 20, 0, Textxx);
					}
				}
				Freq += 100;
			}

			if (UsingBothChannels == 0)
				break;

			Top += WaterfallTotalPixels;
			Base = WaterfallTotalPixels;
		}

}

// These draws the frequency Markers on the Waterfall

void DrawModemFreqRange()
{
	if (nonGUIMode)
		return;

	// Draws the modem freq bars on waterfall header(s)


	int x1, x2, k, pos1, pos2, pos3;
	QImage * bm = Waterfall;

	QPainter qPainter(bm);
	qPainter.setBrush(Qt::NoBrush);
	qPainter.setPen(Qt::white);

	int Chan;
	int LRtoDisplay = LEFT;
	int top = 0;

	for (Chan = 0; Chan < 2; Chan++)
	{
		if (Chan == 1 || ((UsingBothChannels == 0) && (UsingRight == 1)))
			LRtoDisplay = RIGHT;

		//	bm->fill(black);

	//	qPainter.fillRect(top, 23, 1024, 10, Qt::black);

		// We drew markers every 100 Hz or 100 / binsize pixels

		float PixelsPerHz = 1.0 / BinSize;
		k = 26 + top;

		// draw all enabled ports on the ports on this soundcard

		// First Modem is always on the first waterfall
		// If second is enabled it is on the first unless different
		//		channel from first

		for (int i = 0; i < 4; i++)
		{
			if (soundChannel[i] != LRtoDisplay)
					continue;

			pos1 = roundf((((rxOffset + chanOffset[i] + rx_freq[i]) - 0.5*rx_shift[i]) - WaterfallMin) * PixelsPerHz) - 5;
			pos2 = roundf((((rxOffset + chanOffset[i] + rx_freq[i]) + 0.5*rx_shift[i]) - WaterfallMin) * PixelsPerHz) - 5;
			pos3 = roundf(((rxOffset + chanOffset[i] + rx_freq[i])) - WaterfallMin * PixelsPerHz);
			x1 = pos1 + 5;
			x2 = pos2 + 5;

			qPainter.setPen(Qt::white);
			qPainter.drawLine(x1, k, x2, k);
			qPainter.drawLine(x1, k - 3, x1, k + 3);
			qPainter.drawLine(x2, k - 3, x2, k + 3);
			//		qPainter.drawLine(pos3, k - 3, pos3, k + 3);

			if (rxOffset || chanOffset[i])
			{
				// Draw TX posn if rxOffset used

				pos3 = roundf((rx_freq[i] - WaterfallMin) * PixelsPerHz);
				qPainter.setPen(Qt::magenta);
				qPainter.drawLine(pos3, k - 3, pos3, k + 3);
				qPainter.drawLine(pos3, k - 3, pos3, k + 3);
				qPainter.drawLine(pos3 - 2, k - 3, pos3 + 2, k - 3);
			}

			k += 3;
		}
		if (UsingBothChannels == 0)
			break;

		LRtoDisplay = RIGHT;
		top = WaterfallTotalPixels;
	}
}


void doWaterfallThread(void * param);

extern "C" void doWaterfall(int snd_ch)
{
	if (nonGUIMode)
		return;

	if (Closing)
		return;

	if (lockWaterfall)
		return;

//	if (multiCore)			// Run modems in separate threads
//		_beginthread(doWaterfallThread, 0, xx);
//	else
		doWaterfallThread((void *)(size_t)snd_ch);

}

extern "C" void displayWaterfall()
{
	// if we are using both channels but only want right need to extract correct half of Image

	if (Waterfall == nullptr)
		return;

	if (UsingBothChannels && (Firstwaterfall == 0))
		WaterfallCopy->setAlignment(Qt::AlignBottom | Qt::AlignLeft);
	else
		WaterfallCopy->setAlignment(Qt::AlignTop | Qt::AlignLeft);

//	WaterfallCopy->setPixmap(QPixmap::fromImage(*Waterfall));

	emit t->setWaterfallImage();
}

extern "C" float aFFTAmpl[1024];
extern "C" void SMUpdateBusyDetector(int LR, float * Real, float *Imag);

void doWaterfallThread(void * param)
{
	int snd_ch = (int)(size_t)param;

	if (lockWaterfall)
		return;

	if (Configuring)
		return;

	if (inWaterfall)
		return;

	inWaterfall = true;					// don't allow restart waterfall

	if (snd_ch == 1 && UsingLeft == 0)	// Only using right
		snd_ch = 0;						// Samples are in first buffer

	QImage * bm = Waterfall;

	int  i;
	single  mag;
	UCHAR * p;
	UCHAR Line[4096] = "";			// 4 bytes per pixel

	int lineLen, Start, End;
	word  hFFTSize;
	Byte  n;
	float RealOut[8192] = { 0 };
	float ImagOut[8192];


	RefreshLevel(CurrentLevel, CurrentLevelR);	// Signal Level

	hFFTSize = FFTSize / 2;


	// I think an FFT should produce n/2 bins, each of Samp/n Hz
	// Looks like my code only works with n a power of 2

	// So can use 1024 or 4096. 1024 gives 512 bins of 11.71875 and a 512 pixel 
	// display (is this enough?)

	Start = (WaterfallMin / BinSize);		// First and last bins to process
	End = (WaterfallMax / BinSize);

	if (0)	//RSID_WF
	{
		// Use the Magnitudes in float aFFTAmpl[RSID_FFT_SIZE];

		for (i = 0; i < hFFTSize; i++)
		{
			mag = aFFTAmpl[i];

			mag *= 0.00000042f;

			if (mag < 0.00001f)
				mag = 0.00001f;

			if (mag > 1.0f)
				mag = 1.0f;

			mag = 22 * log2f(mag) + 255;

			if (mag < 0)
				mag = 0;

			fft_disp[snd_ch][i] = round(mag);
		}
	}
	else
	{
		dofft(&fft_buf[snd_ch][0], RealOut, ImagOut);

		//	FourierTransform(1024, &fft_buf[snd_ch][0], RealOut, ImagOut, 0);

		for (i = Start; i < End; i++)
		{
			//mag: = ComplexMag(fft_d[i])*0.00000042;

	//		mag = sqrtf(powf(RealOut[i], 2) + powf(ImagOut[i], 2)) * 0.00000042f;

			mag = powf(RealOut[i], 2);
			mag += powf(ImagOut[i], 2);
			mag = sqrtf(mag);
			mag *= 0.00000042f;


			if (mag > MaxMagOut)
			{
				MaxMagOut = mag;
				MaxMagIndex = i;
			}

			if (mag < 0.00001f)
				mag = 0.00001f;

			if (mag > 1.0f)
				mag = 1.0f;

			mag = 22 * log2f(mag) + 255;

			if (mag < 0)
				mag = 0;

			MagOut[i] = mag;					// for Freq Guess
			fft_disp[snd_ch][i] = round(mag);
		}
	}

	SMUpdateBusyDetector(snd_ch, RealOut, ImagOut);



	// we always do fft so we can get centre freq and do busy detect. But only update waterfall if on display

	if (bm == 0)
	{
		inWaterfall = false;
		return;
	}
	if ((Firstwaterfall == 0 && snd_ch == 0) || (Secondwaterfall == 0 && snd_ch == 1))
	{
		inWaterfall = false;
		return;
	}

	p = Line;
	lineLen = 4096;

	if (raduga == DISP_MONO)
	{
		for (i = Start; i < End; i++)
		{
			n = fft_disp[snd_ch][i];
			*(p++) = n;					// all colours the same
			*(p++) = n;
			*(p++) = n;
			p++;
		}
	}
	else
	{
		for (i = Start; i < End; i++)
		{
			n = fft_disp[snd_ch][i];
			memcpy(p, &RGBWF[n], 4);
			p += 4;
		}
	}

	// Scroll



	int TopLine = NextWaterfallLine[snd_ch];
	int TopScanLine = WaterfallHeaderPixels;

	if (snd_ch)
		TopScanLine += WaterfallTotalPixels;

	// Write line to cyclic buffer then draw starting with the line just written

	memcpy(&WaterfallLines[snd_ch][NextWaterfallLine[snd_ch]++][0], Line, 4096);
	if (NextWaterfallLine[snd_ch] > 79)
		NextWaterfallLine[snd_ch] = 0;
	
	// Sanity check

	if ((79 + TopScanLine) >= bm->height())
	{
		printf("Invalid WFMaxLine %d \n", bm->height());
			exit(1);
	}


	for (int j = 79; j > 0; j--)
	{
		p = bm->scanLine(j + TopScanLine);
		if (p == nullptr)
		{
			printf("Invalid WF Pointer \n");
			exit(1);
		}
		memcpy(p, &WaterfallLines[snd_ch][TopLine][0], lineLen);
		TopLine++;
		if (TopLine > 79)
			TopLine = 0;
	}

	inWaterfall = false;
}

void QtSoundModem::setWaterfallImage()
{
	ui.Waterfall->setPixmap(QPixmap::fromImage(*Waterfall));
}

void QtSoundModem::setLevelImage()
{
	RXLevelCopy->setPixmap(QPixmap::fromImage(*RXLevel));
	RXLevel2Copy->setPixmap(QPixmap::fromImage(*RXLevel2));
}

void QtSoundModem::setConstellationImage(int chan, int Qual)
{
	char QualText[64];
	sprintf(QualText, "Chan %c Qual = %d", chan + 'A', Qual);
	QualLabel[chan]->setText(QualText);
	constellationLabel[chan]->setPixmap(QPixmap::fromImage(*Constellation[chan]));
}



void QtSoundModem::changeEvent(QEvent* e)
{
	if (e->type() == QEvent::WindowStateChange)
	{
		QWindowStateChangeEvent* ev = static_cast<QWindowStateChangeEvent*>(e);

		qDebug() << windowState();

		if (!(ev->oldState() & Qt::WindowMinimized) && windowState() & Qt::WindowMinimized)
		{
			if (trayIcon)
				setVisible(false);
		}
//		if (!(ev->oldState() != Qt::WindowNoState) && windowState() == Qt::WindowNoState)
//		{
//			QMessageBox::information(this, "", "Window has been restored");
//		}

	}
	QWidget::changeEvent(e);
}

#include <QCloseEvent>

void QtSoundModem::closeEvent(QCloseEvent *event)
{
	UNUSED(event);

	Closing = TRUE;

	QSettings mysettings("QtSoundModem.ini", QSettings::IniFormat);
	mysettings.setValue("geometry", QWidget::saveGeometry());
	mysettings.setValue("windowState", saveState());

	mysettings.setValue("Constellationgeometry", constellationDialog->saveGeometry());
	constellationDialog->close();

	qDebug() << "Closing";

	QThread::msleep(100);
}

	
QtSoundModem::~QtSoundModem()
{
	qDebug() << "Saving Settings";

	Closing = TRUE;

	closeTraceLog();

	QSettings mysettings("QtSoundModem.ini", QSettings::IniFormat);
	mysettings.setValue("geometry", saveGeometry());
	mysettings.setValue("windowState", saveState());

	mysettings.setValue("Constellationgeometry", constellationDialog->saveGeometry());
	constellationDialog->close();

	saveSettings();
	qDebug() << "Closing";

	QThread::msleep(100);
}

extern "C" void QSleep(int ms)
{
	QThread::msleep(ms);
}

int upd_time = 30;

void QtSoundModem::show_grid()
{
	// This refeshes the session list

	int  snd_ch, i, num_rows, row_idx;
	QTableWidgetItem *item;
	const char * msg = "";

	int  speed_tx, speed_rx;

	if (grid_time < 10)
	{
		grid_time++;
		return;
	}

	grid_time = 0;

	//label7.Caption = inttostr(stat_r_mem); mem_arq

	num_rows = 0;
	row_idx = 0;

	for (snd_ch = 0; snd_ch < 4; snd_ch++)
	{
		for (i = 0; i < port_num; i++)
		{
			if (AX25Port[snd_ch][i].status != STAT_NO_LINK)
				num_rows++;
		}
	}

	if (num_rows == 0)
	{
		sessionTable->clearContents();
		sessionTable->setRowCount(0);
		sessionTable->setRowCount(1);
	}
	else
		sessionTable->setRowCount(num_rows);


	for (snd_ch = 0; snd_ch < 4; snd_ch++)
	{
		for (i = 0; i < port_num; i++)
		{
			if (AX25Port[snd_ch][i].status != STAT_NO_LINK)
			{
				switch (AX25Port[snd_ch][i].status)
				{
				case STAT_NO_LINK:

					msg = "No link";
					break;

				case STAT_LINK:

					msg = "Link";
					break;

				case STAT_CHK_LINK:

					msg = "Chk link";
					break;

				case STAT_WAIT_ANS:

					msg = "Wait ack";
					break;

				case STAT_TRY_LINK:

					msg = "Try link";
					break;

				case STAT_TRY_UNLINK:

					msg = "Try unlink";
				}


				item = new QTableWidgetItem((char *)AX25Port[snd_ch][i].mycall);
				sessionTable->setItem(row_idx, 0, item);

				item = new QTableWidgetItem(AX25Port[snd_ch][i].kind);
				sessionTable->setItem(row_idx, 11, item);

				item = new QTableWidgetItem((char *)AX25Port[snd_ch][i].corrcall);
				sessionTable->setItem(row_idx, 1, item);

				item = new QTableWidgetItem(msg);
				sessionTable->setItem(row_idx, 2, item);

				item = new QTableWidgetItem(QString::number(AX25Port[snd_ch][i].info.stat_s_pkt));
				sessionTable->setItem(row_idx, 3, item);

				item = new QTableWidgetItem(QString::number(AX25Port[snd_ch][i].info.stat_s_byte));
				sessionTable->setItem(row_idx, 4, item);

				item = new QTableWidgetItem(QString::number(AX25Port[snd_ch][i].info.stat_r_pkt));
				sessionTable->setItem(row_idx, 5, item);

				item = new QTableWidgetItem(QString::number(AX25Port[snd_ch][i].info.stat_r_byte));
				sessionTable->setItem(row_idx, 6, item);

				item = new QTableWidgetItem(QString::number(AX25Port[snd_ch][i].info.stat_r_fc));
				sessionTable->setItem(row_idx, 7, item);

				item = new QTableWidgetItem(QString::number(AX25Port[snd_ch][i].info.stat_fec_count));
				sessionTable->setItem(row_idx, 8, item);

				if (grid_timer != upd_time)
					grid_timer++;
				else
				{
					grid_timer = 0;
					speed_tx = round(abs(AX25Port[snd_ch][i].info.stat_s_byte - AX25Port[snd_ch][i].info.stat_l_s_byte) / upd_time);
					speed_rx = round(abs(AX25Port[snd_ch][i].info.stat_r_byte - AX25Port[snd_ch][i].info.stat_l_r_byte) / upd_time);

					item = new QTableWidgetItem(QString::number(speed_tx));
					sessionTable->setItem(row_idx, 9, item);

					item = new QTableWidgetItem(QString::number(speed_rx));
					sessionTable->setItem(row_idx, 10, item);

					AX25Port[snd_ch][i].info.stat_l_r_byte = AX25Port[snd_ch][i].info.stat_r_byte;
					AX25Port[snd_ch][i].info.stat_l_s_byte = AX25Port[snd_ch][i].info.stat_s_byte;	
				}

				row_idx++;
			}
		}
	}
}

// "Copy on Select" Code

void QtSoundModem::onTEselectionChanged()
{
	QTextEdit * x = static_cast<QTextEdit*>(QObject::sender());
	x->copy();
}

#define ConstellationHeight 121
#define ConstellationWidth 121
#define PLOTRADIUS 60

#define MAX(x, y) ((x) > (y) ? (x) : (y))

extern "C" int SMUpdatePhaseConstellation(int chan, float * Phases, float * Mags, int intPSKPhase, int Count)
{
	// Subroutine to update Constellation plot for PSK modes...
	// Skip plotting and calculations of intPSKPhase(0) as this is a reference phase (9/30/2014)

	float dblPhaseError;
	float dblPhaseErrorSum = 0;
	float intP = 0;
	float dblRad = 0;
	float dblAvgRad = 0;
	float dbPhaseStep;
	float MagMax = 0;
	float dblPlotRotation = 0;

	int i, intQuality;

	int intX, intY;
	int yCenter = (ConstellationHeight - 2) / 2;
	int xCenter = (ConstellationWidth - 2) / 2;

	if (Count == 0)
		return 0;

	if (nonGUIMode == 0)
	{
		Constellation[chan]->fill(black);

		for (i = 0; i < 120; i++)
		{
			Constellation[chan]->setPixel(xCenter, i, cyan);
			Constellation[chan]->setPixel(i, xCenter, cyan);
		}
	}
	dbPhaseStep = 2 * M_PI / intPSKPhase;

	for (i = 1; i < Count; i++)  // Don't plot the first phase (reference)
	{
		MagMax = MAX(MagMax, Mags[i]); // find the max magnitude to auto scale
		dblAvgRad += Mags[i];
	}

	dblAvgRad = dblAvgRad / Count; // the average radius

	for (i = 0; i < Count; i++)
	{
		dblRad = PLOTRADIUS * Mags[i] / MagMax; //  scale the radius dblRad based on intMag
		intP = round((Phases[i]) / dbPhaseStep);

		// compute the Phase error

		dblPhaseError = fabsf(Phases[i] - intP * dbPhaseStep); // always positive and < .5 *  dblPhaseStep
		dblPhaseErrorSum += dblPhaseError;

		if (nonGUIMode == 0)
		{
			intX = xCenter + dblRad * cosf(dblPlotRotation + Phases[i]);
			intY = yCenter + dblRad * sinf(dblPlotRotation + Phases[i]);

			if (intX > 0 && intY > 0)
				if (intX != xCenter && intY != yCenter)
					Constellation[chan]->setPixel(intX, intY, yellow);
		}
	}

	dblAvgRad = dblAvgRad / Count; // the average radius

	intQuality = MAX(0, ((100 - 200 * (dblPhaseErrorSum / (Count)) / dbPhaseStep))); // ignore radius error for (PSK) but include for QAM

	if (nonGUIMode == 0)
	{
		emit t->setConstellationImage(chan, intQuality);
//		char QualText[64];
//		sprintf(QualText, "Chan %c Qual = %d", chan + 'A', intQuality);
//		QualLabel[chan]->setText(QualText);
//		constellationLabel[chan]->setPixmap(QPixmap::fromImage(*Constellation[chan]));
	}
	return intQuality;
}


QFile tracefile("Tracelog.txt");


extern "C" int openTraceLog()
{
	if (!tracefile.open(QIODevice::Append | QIODevice::Text))
		return 0;

	return 1;
}

extern "C" qint64 writeTraceLog(char * Data)
{
	return tracefile.write(Data);
}

extern "C" void closeTraceLog()
{
	tracefile.close();
}

extern "C" void debugTimeStamp(char * Text, char Dirn)
{
#ifndef LOGTX

	if (Dirn == 'T')
		return;

#endif

#ifndef LOGRX

	if (Dirn == 'R')
		return;

#endif


	QTime Time(QTime::currentTime());
	QString String = Time.toString("hh:mm:ss.zzz");
	char Msg[2048];

	sprintf(Msg, "%s %s\n", String.toUtf8().data(), Text);
	writeTraceLog(Msg);
}



// Timer functions need to run in GUI Thread

extern "C" int SampleNo;


extern "C" int pttOnTime()
{
	return pttOnTimer.elapsed();
}

extern "C" void startpttOnTimer()
{
	pttOnTimer.start();
}


extern "C" void StartWatchdog()
{
	// Get Monotonic clock for PTT drop time calculation

#ifndef WIN32
	clock_gettime(CLOCK_MONOTONIC, &pttclk);
#endif	
	debugTimeStamp((char *)"PTT On", 'T');
	emit t->startWatchdog();
	pttOnTimer.start();
}

extern "C" void StopWatchdog()
{
	int txlenMs = (1000 * SampleNo / TX_Samplerate);

	Debugprintf("Samples Sent %d, Calc Time %d, PTT Time %d", SampleNo, txlenMs, pttOnTime());
	debugTimeStamp((char *)"PTT Off", 'T');
	closeTraceLog();
	openTraceLog();
	debugTimeStamp((char *)"Log Reopened", 'T');

	emit t->stopWatchdog();
}



void QtSoundModem::StartWatchdog()
{
	PTTWatchdog->start(60 * 1000);
}

 void QtSoundModem::StopWatchdog()
{
	 PTTWatchdog->stop();
}


 void QtSoundModem::PTTWatchdogExpired()
 {
	 PTTWatchdog->stop();
 }


 // KISS Serial Port code - mainly for 6Pack but should work with KISS as well

 // Serial Read needs to block and signal the main thread whenever a character is received. TX can probably be uncontrolled

 void serialThread::startSlave(const QString &portName, int waitTimeout, const QString &response)
 {
	 QMutexLocker locker(&mutex);
	 this->portName = portName;
	 this->waitTimeout = waitTimeout;
	 this->response = response;
	 if (!isRunning())
		 start();
 }

 void serialThread::run()
 {
	 QSerialPort serial;

	 mutex.lock();
	 QString currentPortName;
	 if (currentPortName != portName)
	 {
		 currentPortName = portName;
	 }

	 int currentWaitTimeout = waitTimeout;
	 QString currentRespone = response;
	 mutex.unlock();

	 if (currentPortName.isEmpty())
	 {
		 Debugprintf("Port not set");
		 return;
	 }

	 serial.setPortName(currentPortName);

	 if (!serial.open(QIODevice::ReadWrite))
	 {
		 Debugprintf("Can't open %s, error code %d", qPrintable(portName), serial.error());
		 return;
	 }
 
	 while (1)
	 {
		
		 if (serial.waitForReadyRead(currentWaitTimeout))
		 {
			 // read request
			 QByteArray requestData = serial.readAll();
			 while (serial.waitForReadyRead(10))
				 requestData += serial.readAll();

			 // Pass data to 6pack handler

			 emit this->request(requestData);

		 }
		 else {
			 Debugprintf("Serial read request timeout");
		 }
	 }
 }

 void Process6PackData(unsigned char * Bytes, int Len);

 void QtSoundModem::showRequest(QByteArray Data)
 {
	 Process6PackData((unsigned char *)Data.data(), Data.length());
 }

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
 static QMutex s_audioMutex;

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
			 // Error handling
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
			 // Error handling
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

		 if (SoundIsPlaying)
		 {
			SoundIsPlaying = 0;
		 }
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
 #endif

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

	 initializeAudioOut(outDeviceInfo);
	 initializeAudioIn(inDeviceInfo);

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
			 extern int SoundIsPlaying;
			 SoundIsPlaying = 0;
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
	 if (g_audioInputRate == 0)
	 {
		 format = deviceInfo.preferredFormat();
		 g_audioInputRate = format.sampleRate();
		 Debugprintf("WARNING: input device does not support 12/24/48 kHz "
			 "stereo Int16; preferredFormat is %d Hz / %d ch / sample-format %d.",
			 format.sampleRate(), format.channelCount(),
			 (int)format.sampleFormat());
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

	 // Design (or re-design) the antialias FIR for the negotiated rate.
	 // No-op if the rate hasn't changed since last call.
	 aaFilterInit(g_audioInputRate);

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
	 QMutexLocker locker(&s_audioMutex);
	 m_audioInput = new QAudioSource(deviceInfo, format, this);
	 connect(m_audioInput, &QAudioSource::stateChanged, this, &QtSoundModem::audioInStateChanged);

	 // Buffer size scales with rate so PollQSound still gets ~340 ms
	 // of headroom before overrun.
	 m_audioInput->setBufferSize(16384 * g_audioInputDecim);
	 in = m_audioInput->start();

	 // A successful open closes the warning gate — if this same device
	 // later goes back to a refusable rate (or a different bad device
	 // appears), the user gets a fresh dialog rather than silent failure.
	 s_lastWarnedInDev.clear();
 }
 void QtSoundModem::initializeAudioOut(const QAudioDevice &deviceInfo)
 {
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

	 // See initializeAudioIn for the publication-under-lock rationale.
	 QMutexLocker locker(&s_audioMutex);
	 m_audioOutput = new QAudioSink(deviceInfo, format, this);
	 connect(m_audioOutput, &QAudioSink::stateChanged, this, &QtSoundModem::audioOutStateChanged);

	 m_audioOutput->setBufferSize(16384);
	 int n = m_audioOutput->bufferSize();
	 Debugprintf("Output Buffer Size %d", n);

	 out = m_audioOutput->start();

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
 }

 extern "C" void txSleep(int mS);

 // Qt 6 dropped QAudioSink::periodSize() / QAudioSource::periodSize().
 // 1024 bytes = 256 stereo Int16 frames at 12 kHz, ~21 ms. Twice the
 // 512-byte block PollQSound slices Buffer into; large enough to
 // amortise the per-read overhead, small enough that the worker-thread
 // poll cadence stays responsive.
 static const int kAudioPeriodBytes = 1024;

 extern "C" unsigned short * sendSamplestoQSound(unsigned short * buf, int n)
 {
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

	 for (;;)
	 {
		 int frames;
		 {
			 QMutexLocker locker(&s_audioMutex);
			 if (!m_audioOutput || !out)
				 return buf;
			 frames = m_audioOutput->bytesFree() / 4;
		 }
		 if (frames >= n)
			 break;
		 txSleep(10);
		 Debugprintf("Space %d", frames);
	 }

	 // Software TX attenuation. Plain int snapshot — see UZ7HOStuff.h
	 // comment on txAudioLevel/rxAudioLevel for the threading argument.
	 // Scale in place on the caller's DMABuffer; producers in SMMain.c
	 // / MacBits.c reset or advance after SendtoCard rather than re-reading
	 // the sent buffer (verified during plan review). Since txLvl ≤ 100,
	 // |sample * txLvl / 100| ≤ |sample| ≤ 32767 — no saturation needed.
	 const int txLvl = txAudioLevel;
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

extern int g_audioInputRate;
extern int g_audioInputDecim;

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
	// Canonical 44-byte WAV header. Chunk sizes patched at close.
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

static void dumpInputWrite(const void * data, size_t bytes)
{
	if (!s_dumpFile) return;
	fwrite(data, 1, bytes, s_dumpFile);
	s_dumpDataBytes += bytes;
}

extern "C" void dumpInputClose()
{
	if (!s_dumpFile) return;
	long fileSize = 36 + s_dumpDataBytes;
	fseek(s_dumpFile, 4, SEEK_SET);
	unsigned char b[4] = {
		(unsigned char)(fileSize & 0xFF),
		(unsigned char)((fileSize >> 8) & 0xFF),
		(unsigned char)((fileSize >> 16) & 0xFF),
		(unsigned char)((fileSize >> 24) & 0xFF)
	};
	fwrite(b, 1, 4, s_dumpFile);
	fseek(s_dumpFile, 40, SEEK_SET);
	b[0] = (unsigned char)(s_dumpDataBytes & 0xFF);
	b[1] = (unsigned char)((s_dumpDataBytes >> 8) & 0xFF);
	b[2] = (unsigned char)((s_dumpDataBytes >> 16) & 0xFF);
	b[3] = (unsigned char)((s_dumpDataBytes >> 24) & 0xFF);
	fwrite(b, 1, 4, s_dumpFile);
	fclose(s_dumpFile);
	s_dumpFile = nullptr;
	Debugprintf("dump-input: closed, %ld bytes data (%.2f s)",
		s_dumpDataBytes, (double)s_dumpDataBytes / 48000.0);
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

	if (sampleRateIn == fir_designed_rate)
		return;  // already designed for this rate
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

	// Reset history on rate change so a stale tail from the previous
	// rate doesn't bleed into the first few output frames.
	for (int i = 0; i < FIR_TAPS - 1; i++)
	{
		fir_histL[i] = 0.0f;
		fir_histR[i] = 0.0f;
	}

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
	// Process any captured samples
	// Ideally call at least every 100 mS, more than 200 will loose data

	// For level display we want a fairly rapid level average but only want to report
	// to log every 10 secs or so

	// Each output chunk = 512 stereo Int16 frames at 12 kHz =
	// 2048 bytes. Input chunk scales by decimation:
	//   decim=1 (12 kHz):  2048 bytes
	//   decim=2 (24 kHz):  4096 bytes
	//   decim=4 (48 kHz):  8192 bytes
	const int decim = g_audioInputDecim;
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
	const bool monoInput = (g_audioInputChannelCount == 1);

	qint64 x;
	{
		QMutexLocker locker(&s_audioMutex);
		if (!m_audioInput)
			return;

		if (in == nullptr)
			return;

		// `size` is the post-expansion (stereo) byte budget. Mono reads
		// half that and the expansion below fills in the missing L/R
		// pairs. The Buffer-overflow clamp uses the post-expansion
		// figure so the expansion can't run past the end.
		int size = kAudioPeriodBytes * decim;
		if (BufferLen + size > (int)sizeof(Buffer))
			size = (int)sizeof(Buffer) - BufferLen;

		const int requestSize = monoInput ? size / 2 : size;
		x = in->read(&Buffer[BufferLen], requestSize);
	}

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
		const int rxLvl = rxAudioLevel;
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

	while (BufferLen >= inChunkBytes)
	{
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
		// buffer in place on the using48000=1 path (sm_main.c:1003-1006
		// rewrites the first quarter of Samples[] with the downsampled
		// 12 kHz frames it just produced), so dumping after would
		// capture a hybrid of downsampled-leading-frames and
		// raw-trailing-frames at a 48 kHz sample-rate header — useless.
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
}



