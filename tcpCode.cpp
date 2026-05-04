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

#include <QMessageBox>
#include "QtSoundModem.h"
#include "UZ7HOStuff.h"
#include <QTimer>

#define CONNECT(sndr, sig, rcvr, slt) connect(sndr, SIGNAL(sig), rcvr, SLOT(slt))

QList<QTcpSocket*>  _MgmtSockets;
QList<QTcpSocket*>  _KISSSockets;
QList<QTcpSocket*>  _AGWSockets;

QTcpServer * _KISSserver;
QTcpServer * _AGWserver;
QTcpServer * SixPackServer;
QTcpSocket *SixPackSocket;
QTcpServer * _MgmtServer;

QTcpServer * RHPServer;
QTcpSocket * RHPSocket;

QTcpServer * RHPAPIServer;
QTcpSocket * RHPAPISocket;


TMgmtMode ** MgmtConnections = NULL;
int MgmtConCount = 0;

extern workerThread *t;
extern mynet m1;
extern serialThread *serial;

#if defined(__APPLE__)
extern "C" char * g_wavInputPath;
extern "C" void debugDecodeWav(const char * path);
#endif

QString Response;

extern int MgmtPort;
extern int RHPPort;
extern bool RHPServ;
extern "C" int KISSPort;
extern "C" void * initPulse();
extern "C" int SoundMode;

extern "C" int UDPClientPort;
extern "C" int UDPServerPort;
extern "C" int TXPort;

extern char SixPackDevice[256];
extern int SixPackPort;
extern int SixPackEnable;

char UDPHost[64] = "";

int UDPServ = 0;				// UDP Server Active (ie broadcast sound frams as UDP packets)

QMutex mutex;

extern void saveSettings();

extern int Closing;				// Set to stop background thread

extern "C"
{
	void KISSDataReceived(void * sender, char * data, int length);
	void AGW_explode_frame(void * soket, char * data, int len);
	void KISS_add_stream(void * Socket);
	void KISS_del_socket(void * Socket);
	void AGW_add_socket(void * Socket);
	void AGW_del_socket(void * socket);
	void Debugprintf(const char * format, ...);
	int InitSound(BOOL Report);
	void soundMain();
	void MainLoop();
	void set_speed(int snd_ch, int Modem);
	void init_speed(int snd_ch);
}

void Process6PackData(unsigned char * Bytes, int Len);
void RHPProcessLine(QTcpSocket * sender);
void RHPAPIProcessLine(QTcpSocket * sender);


extern "C" int nonGUIMode;

QTimer *timer;
QTimer *timercopy;

void mynet::start()
{
	if (SoundMode == 3)
		OpenUDP();

	if (UDPServ)
		OpenUDP();

	if (KISSServ)
	{
		_KISSserver = new(QTcpServer);

		if (_KISSserver->listen(QHostAddress::Any, KISSPort))
			connect(_KISSserver, SIGNAL(newConnection()), this, SLOT(onKISSConnection()));
		else
		{
			if (nonGUIMode)
				Debugprintf("Listen failed for KISS Port");
			else
			{
				QMessageBox msgBox;
				msgBox.setText("Listen failed for KISS Port.");
				msgBox.exec();
			}
		}
	}

	if (AGWServ)
	{
		_AGWserver = new(QTcpServer);
		if (_AGWserver->listen(QHostAddress::Any, AGWPort))
			connect(_AGWserver, SIGNAL(newConnection()), this, SLOT(onAGWConnection()));
		else
		{
			if (nonGUIMode)
				Debugprintf("Listen failed for AGW Port");
			else
			{
				QMessageBox msgBox;
				msgBox.setText("Listen failed for AGW Port.");
				msgBox.exec();
			}
		}
	}


	if (MgmtPort)
	{
		_MgmtServer = new(QTcpServer);

		if (_MgmtServer->listen(QHostAddress::Any, MgmtPort))
			connect(_MgmtServer, SIGNAL(newConnection()), this, SLOT(onMgmtConnection()));
		else
		{
			if (nonGUIMode)
				Debugprintf("Listen failed for Mgmt Port");
			else
			{
				QMessageBox msgBox;
				msgBox.setText("Listen failed for Mgmt Port.");
				msgBox.exec();
			}
		}
	}

	if (RHPPort && RHPServ)
	{
		RHPServer = new(QTcpServer);

		if (RHPServer->listen(QHostAddress::Any, RHPPort + 1))
			connect(RHPServer, SIGNAL(newConnection()), this, SLOT(onRHPConnection()));
		else
		{
			if (nonGUIMode)
				Debugprintf("Listen failed for RHP Port");
			else
			{
				QMessageBox msgBox;
				msgBox.setText("Listen failed for RHP Port.");
				msgBox.exec();
			}
		}
	
		RHPAPIServer = new(QTcpServer);

		if (RHPAPIServer->listen(QHostAddress::Any, RHPPort))
			connect(RHPAPIServer, SIGNAL(newConnection()), this, SLOT(onRHPAPIConnection()));
		else
		{
			if (nonGUIMode)
				Debugprintf("Listen failed for RHP API Port");
			else
			{
				QMessageBox msgBox;
				msgBox.setText("Listen failed for RHP API Port.");
				msgBox.exec();
			}
		}

		QObject::connect(t, SIGNAL(sendtoRHP(void *, char *, int)), this, SLOT(sendtoRHP(void *, char *, int)), Qt::QueuedConnection);

	}

	QObject::connect(t, SIGNAL(sendtoKISS(void *, unsigned char *, int)), this, SLOT(sendtoKISS(void *, unsigned char *, int)), Qt::QueuedConnection);


	QTimer *timer = new QTimer(this);
	connect(timer, SIGNAL(timeout()), this, SLOT(MyTimerSlot()));
	timer->start(100);

	if (SixPackEnable)
	{
		if (SixPackDevice[0] && strcmp(SixPackDevice, "None") != 0)	// Using serial
		{
			serial->startSlave(SixPackDevice, 30000, Response);
			serial->start();

//			connect(serial, &serialThread::request, this, &QtSoundModem::showRequest);
//			connect(serial, &serialThread::error, this, &QtSoundModem::processError);
//			connect(serial, &serialThread::timeout, this, &QtSoundModem::processTimeout);

		}

		else if (SixPackPort)		// using TCP
		{
			SixPackServer = new(QTcpServer);
			if (SixPackServer->listen(QHostAddress::Any, SixPackPort))
				connect(SixPackServer, SIGNAL(newConnection()), this, SLOT(on6PackConnection()));

		}
	}

}

void mynet::MyTimerSlot()
{
	// 100 mS Timer Event

	TimerEvent = TIMER_EVENT_ON;
}


void mynet::onAGWConnection()
{
	QTcpSocket *clientSocket = _AGWserver->nextPendingConnection();
	connect(clientSocket, SIGNAL(readyRead()), this, SLOT(onAGWReadyRead()));
	connect(clientSocket, SIGNAL(stateChanged(QAbstractSocket::SocketState)), this, SLOT(onAGWSocketStateChanged(QAbstractSocket::SocketState)));

	_AGWSockets.push_back(clientSocket);

	AGW_add_socket(clientSocket);

	Debugprintf("AGW Connect Sock %x", clientSocket);
}



void mynet::onAGWSocketStateChanged(QAbstractSocket::SocketState socketState)
{
	if (socketState == QAbstractSocket::UnconnectedState)
	{
		QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());

		AGW_del_socket(sender);

		_AGWSockets.removeOne(sender);
	}
}

void mynet::onAGWReadyRead()
{
	QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
	QByteArray datas = sender->readAll();

	AGW_explode_frame(sender, datas.data(), datas.length());
}

void mynet::on6PackReadyRead()
{
	QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
	QByteArray datas = sender->readAll();
	Process6PackData((unsigned char *)datas.data(), datas.length());
}


void mynet::on6PackSocketStateChanged(QAbstractSocket::SocketState socketState)
{
	if (socketState == QAbstractSocket::UnconnectedState)
	{
		QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());

	}
}

void mynet::on6PackConnection()
{
	QTcpSocket *clientSocket = SixPackServer->nextPendingConnection();
	connect(clientSocket, SIGNAL(readyRead()), this, SLOT(on6PackReadyRead()));
	connect(clientSocket, SIGNAL(stateChanged(QAbstractSocket::SocketState)), this, SLOT(on6PackSocketStateChanged(QAbstractSocket::SocketState)));

	Debugprintf("6Pack Connect Sock %x", clientSocket);
}


void mynet::onKISSConnection()
{
	QTcpSocket *clientSocket = _KISSserver->nextPendingConnection();
	connect(clientSocket, SIGNAL(readyRead()), this, SLOT(onKISSReadyRead()));
	connect(clientSocket, SIGNAL(stateChanged(QAbstractSocket::SocketState)), this, SLOT(onKISSSocketStateChanged(QAbstractSocket::SocketState)));

	_KISSSockets.push_back(clientSocket);

	KISS_add_stream(clientSocket);

	Debugprintf("KISS Connect Sock %x", clientSocket);
}

void Mgmt_del_socket(void * socket)
{
	int i;

	TMgmtMode * MGMT = NULL;

	if (MgmtConCount == 0)
		return;

	for (i = 0; i < MgmtConCount; i++)
	{
		if (MgmtConnections[i]->Socket == socket)
		{
			MGMT = MgmtConnections[i];
			break;
		}
	}

	if (MGMT == NULL)
		return;

	// Need to remove entry and move others down

	MgmtConCount--;

	while (i < MgmtConCount)
	{
		MgmtConnections[i] = MgmtConnections[i + 1];
		i++;
	}
}



void mynet::onMgmtConnection()
{
	QTcpSocket *clientSocket = (QTcpSocket *)_MgmtServer->nextPendingConnection();
	connect(clientSocket, SIGNAL(readyRead()), this, SLOT(onMgmtReadyRead()));
	connect(clientSocket, SIGNAL(stateChanged(QAbstractSocket::SocketState)), this, SLOT(onMgmtSocketStateChanged(QAbstractSocket::SocketState)));

	_MgmtSockets.append(clientSocket);

	// Create a data structure to hold session info

	TMgmtMode * MGMT;

	MgmtConnections = (TMgmtMode **)realloc(MgmtConnections, (MgmtConCount + 1) * sizeof(void *));

	MGMT = MgmtConnections[MgmtConCount++] = (TMgmtMode *)malloc(sizeof(*MGMT));
	memset(MGMT, 0, sizeof(*MGMT));

	MGMT->Socket = clientSocket;

	Debugprintf("Mgmt Connect Sock %x", clientSocket);
	clientSocket->write("Connected to QtSM\r");
}


void mynet::onMgmtSocketStateChanged(QAbstractSocket::SocketState socketState)
{
	if (socketState == QAbstractSocket::UnconnectedState)
	{
		QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());

		Mgmt_del_socket(sender);

//		free(sender->Msg);
		_MgmtSockets.removeOne(sender);
		Debugprintf("Mgmt Disconnect Sock %x", sender);
	}
}

void mynet::onMgmtReadyRead()
{
	QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());

	MgmtProcessLine(sender);
}



extern "C" void SendMgmtPTT(int snd_ch, int PTTState)
{
	// Won't work in non=gui mode

	emit m1.mgmtSetPTT(snd_ch, PTTState);
}


extern "C" char * strlop(char * buf, char delim);
extern "C" char modes_name[modes_count][21];
extern "C" int speed[5];

#ifndef WIN32
extern "C" int memicmp(char *a, char *b, int n);

int _stricmp(char * pStr1, char *pStr2)
{
	unsigned char c1, c2;
	int  v;

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
		/* The casts are necessary when pStr1 is shorter & char is signed */
		v = tolower(c1) - tolower(c2);
	} while ((v == 0) && (c1 != '\0') && (c2 != '\0'));

	return v;
}

#endif

void mynet::MgmtProcessLine(QTcpSocket* socket)
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
	{
		socket->readAll();
		return;
	}

	while (socket->bytesAvailable())
	{

		QByteArray datas = socket->peek(512);

		char * Line = datas.data();

		if (strchr(Line, '\r') == 0)
			return;

		char * rest = strlop(Line, '\r');

		int used = rest - Line;

		// Read the upto the cr Null

		datas = socket->read(used);

		Line = datas.data();
		rest = strlop(Line, '\r');

		if (memicmp(Line, "QtSMPort ", 8) == 0)
		{
			if (strlen(Line) > 10)
			{
				int port = atoi(&Line[8]);
				int bpqport = atoi(&Line[10]);

				if ((port > 0 && port < 5) && (bpqport > 0 && bpqport < 64))
				{
					MGMT->BPQPort[port - 1] = bpqport;
					socket->write("Ok\r");
				}
			}
		}

		else if (memicmp(Line, "Modem ", 6) == 0)
		{
			int port = atoi(&Line[6]);

			if (port > 0 && port < 5)
			{
				// if any more params - if a name follows, set it else return it

				char reply[80];

				sprintf(reply, "Port %d Chan %d Freq %d Modem %s \r", MGMT->BPQPort[port - 1], port, rx_freq[port - 1], modes_name[speed[port - 1]]);

				socket->write(reply);
			}
			else
				socket->write("Invalid Port\r");
		}
		else
		{
			socket->write("Invalid command\r");

		}
	}
}

void mynet::onKISSSocketStateChanged(QAbstractSocket::SocketState socketState)
{
	if (socketState == QAbstractSocket::UnconnectedState)
	{
		QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());

		KISS_del_socket(sender);

		_KISSSockets.removeOne(sender);
		Debugprintf("KISS Disconnect Sock %x", sender);
	}
}

void mynet::onKISSReadyRead()
{
	QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
	QByteArray datas = sender->readAll();

	KISSDataReceived(sender, datas.data(), datas.length());
}



void mynet::displayError(QAbstractSocket::SocketError socketError)
{
	if (socketError == QTcpSocket::RemoteHostClosedError)
		return;

	qDebug() << tcpClient->errorString();

	tcpClient->close();
	tcpServer->close();
}



void mynet::sendtoKISS(void * sock, unsigned char * Msg, int Len)
{
	if (sock == NULL)
	{
		for (QTcpSocket* socket : _KISSSockets)
		{
			socket->write((char *)Msg, Len);
		}
	}
	else
	{
		QTcpSocket* socket = (QTcpSocket*)sock;
		socket->write((char *)Msg, Len);
	}
	free(Msg);
}



QTcpSocket * HAMLIBsock;
int HAMLIBConnected = 0;
int HAMLIBConnecting = 0;

QTcpSocket * FLRIGsock;
int FLRIGConnected = 0;
int FLRIGConnecting = 0;

void mynet::HAMLIBdisplayError(QAbstractSocket::SocketError socketError)
{
	switch (socketError)
	{
	case QAbstractSocket::RemoteHostClosedError:
		break;

	case QAbstractSocket::HostNotFoundError:
		if (nonGUIMode)
			qDebug() << "HAMLIB host was not found. Please check the host name and port settings.";
		else
		{
			QMessageBox::information(nullptr, tr("QtSM"),
				tr("HAMLIB host was not found. Please check the "
					"host name and port settings."));
		}

		break;

	case QAbstractSocket::ConnectionRefusedError:

		qDebug() << "HAMLIB Connection Refused";
		break;

	default:

		qDebug() << "HAMLIB Connection Failed";
		break;

	}

	HAMLIBConnecting = 0;
	HAMLIBConnected = 0;
}

void mynet::HAMLIBreadyRead()
{
	unsigned char Buffer[4096];
	QTcpSocket* Socket = static_cast<QTcpSocket*>(QObject::sender());

	// read the data from the socket. Don't do anyhing with it at the moment

	Socket->read((char *)Buffer, 4095);
}

void mynet::onHAMLIBSocketStateChanged(QAbstractSocket::SocketState socketState)
{
	if (socketState == QAbstractSocket::UnconnectedState)
	{
		// Close any connections

		HAMLIBConnected = 0;
		qDebug() << "HAMLIB Connection Closed";
	}
	else if (socketState == QAbstractSocket::ConnectedState)
	{
		HAMLIBConnected = 1;
		HAMLIBConnecting = 0;
		qDebug() << "HAMLIB Connected";
	}
}


void mynet::ConnecttoHAMLIB()
{
	delete(HAMLIBsock);

	HAMLIBConnected = 0;
	HAMLIBConnecting = 1;

	HAMLIBsock = new QTcpSocket();

	connect(HAMLIBsock, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(HAMLIBdisplayError(QAbstractSocket::SocketError)));
	connect(HAMLIBsock, SIGNAL(readyRead()), this, SLOT(HAMLIBreadyRead()));
	connect(HAMLIBsock, SIGNAL(stateChanged(QAbstractSocket::SocketState)), this, SLOT(onHAMLIBSocketStateChanged(QAbstractSocket::SocketState)));

	HAMLIBsock->connectToHost(HamLibHost, HamLibPort);

	return;
}

extern "C" void HAMLIBSetPTT(int PTTState)
{
	// Won't work in non=gui mode

	emit m1.HLSetPTT(PTTState);
}

extern "C" void FLRigSetPTT(int PTTState)
{
	// Won't work in non=gui mode

	emit m1.FLRigSetPTT(PTTState);
}


QTcpSocket * FLRigsock;
int FLRigConnected = 0;
int FLRigConnecting = 0;

void mynet::FLRigdisplayError(QAbstractSocket::SocketError socketError)
{
	switch (socketError)
	{
	case QAbstractSocket::RemoteHostClosedError:
		break;

	case QAbstractSocket::HostNotFoundError:
		QMessageBox::information(nullptr, tr("QtSM"),
			"FLRig host was not found. Please check the "
			"host name and portsettings->");

		break;

	case QAbstractSocket::ConnectionRefusedError:

		qDebug() << "FLRig Connection Refused";
		break;

	default:

		qDebug() << "FLRig Connection Failed";
		break;

	}

	FLRigConnecting = 0;
	FLRigConnected = 0;
}

void mynet::FLRigreadyRead()
{
	unsigned char Buffer[4096];
	QTcpSocket* Socket = static_cast<QTcpSocket*>(QObject::sender());

	// read the data from the socket. Don't do anyhing with it at the moment

	Socket->read((char *)Buffer, 4095);
}

void mynet::onFLRigSocketStateChanged(QAbstractSocket::SocketState socketState)
{
	if (socketState == QAbstractSocket::UnconnectedState)
	{
		// Close any connections

		FLRigConnected = 0;
		FLRigConnecting = 0;

		//	delete (FLRigsock);
		//	FLRigsock = 0;

		qDebug() << "FLRig Connection Closed";

	}
	else if (socketState == QAbstractSocket::ConnectedState)
	{
		FLRigConnected = 1;
		FLRigConnecting = 0;
		qDebug() << "FLRig Connected";
	}
}


void mynet::ConnecttoFLRig()
{
	delete(FLRigsock);

	FLRigConnected = 0;
	FLRigConnecting = 1;

	FLRigsock = new QTcpSocket();

	connect(FLRigsock, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(FLRigdisplayError(QAbstractSocket::SocketError)));
	connect(FLRigsock, SIGNAL(readyRead()), this, SLOT(FLRigreadyRead()));
	connect(FLRigsock, SIGNAL(stateChanged(QAbstractSocket::SocketState)), this, SLOT(onFLRigSocketStateChanged(QAbstractSocket::SocketState)));

	FLRigsock->connectToHost(FLRigHost, FLRigPort);

	return;
}

static char MsgHddr[] = "POST /RPC2 HTTP/1.1\r\n"
"User-Agent: XMLRPC++ 0.8\r\n"
"Host: 127.0.0.1:7362\r\n"
"Content-Type: text/xml\r\n"
"Content-length: %d\r\n"
"\r\n%s";

static char Req[] = "<?xml version=\"1.0\"?>\r\n"
"<methodCall><methodName>%s</methodName>\r\n"
"%s"
"</methodCall>\r\n";


void mynet::doFLRigSetPTT(int c)
{
	int Len;
	char ReqBuf[512];
	char SendBuff[512];
	char ValueString[256] = "";

	sprintf(ValueString, "<params><param><value><i4>%d</i4></value></param></params\r\n>", c);

	Len = sprintf(ReqBuf, Req, "rig.set_ptt", ValueString);
	Len = sprintf(SendBuff, MsgHddr, Len, ReqBuf);

	if (FLRigsock == nullptr || FLRigsock->state() != QAbstractSocket::ConnectedState)
		ConnecttoFLRig();

	if (FLRigsock == nullptr || FLRigsock->state() != QAbstractSocket::ConnectedState)
		return;

	FLRigsock->write(SendBuff);

	FLRigsock->waitForBytesWritten(3000);

	QByteArray datas = FLRigsock->readAll();

	qDebug(datas.data());
}



extern "C" void startTimer(int Time)
{
	// Won't work in non=gui mode

//	emit m1.startTimer(Time);
}

void mynet::dostartTimer(int Time)
{
	timercopy->start(Time);
}

extern "C" void stopTimer()
{
	// Won't work in non=gui mode

//	emit m1.stopTimer();
}

void mynet::dostopTimer()
{
	timercopy->stop();
}

void mynet::doHLSetPTT(int c)
{
	char Msg[16];

	if (HAMLIBsock == nullptr || HAMLIBsock->state() != QAbstractSocket::ConnectedState)
		ConnecttoHAMLIB();

	sprintf(Msg, "T %d\r\n", c);
	HAMLIBsock->write(Msg);

	HAMLIBsock->waitForBytesWritten(30000);

	QByteArray datas = HAMLIBsock->readAll();

	qDebug(datas.data());

}

void mynet::domgmtSetPTT(int snd_ch, int PTTState)
{
	char Msg[64];
	uint64_t ret;

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

		if (MGMT->BPQPort[snd_ch])
		{
			sprintf(Msg, "PTT %d %s\r", MGMT->BPQPort[snd_ch], PTTState ? "ON" : "OFF");
			ret = socket->write(Msg);
		}
	}
}




extern "C" void KISSSendtoServer(void * sock, Byte * Msg, int Len)
{
	emit t->sendtoKISS(sock, Msg, Len);
}


void workerThread::run()
{
	if (SoundMode == 2)			// Pulse
	{
		if (initPulse() == nullptr)
		{
			if (nonGUIMode)
			{
				qDebug() << "PulseAudio requested but pulseaudio libraries not found\nMode set to ALSA\n";
			}
			else
			{
				QMessageBox msgBox;
				msgBox.setText("PulseAudio requested but pulseaudio libraries not found\nMode set to ALSA");
				msgBox.exec();
			}
			SoundMode = 0;
			saveSettings();
		}
	}

	soundMain();

	if (SoundMode != 3)
	{
		if (!InitSound(1))
		{
			//		QMessageBox msgBox;
			//		msgBox.setText("Open Sound Card Failed");
			//		msgBox.exec();
		}
	}

	// Initialise Modems

	init_speed(0);
	init_speed(1);
	init_speed(2);
	init_speed(3);

	//	emit t->openSockets();

#if defined(__APPLE__)
	// --decode-wav harness: feed the WAV directly into ProcessNewSamples
	// and exit when done. Bypasses MainLoop's PollQSound entirely.
	if (g_wavInputPath != NULL)
	{
		qDebug() << "Decode-wav harness: feeding" << g_wavInputPath;
		debugDecodeWav(g_wavInputPath);
		qDebug() << "Decode-wav harness: done, exiting";
		Closing = 1;
	}
#endif

	while (Closing == 0)
	{
		// Run scheduling loop

		MainLoop();

		this->msleep(10);
	}

	qDebug() << "Saving Settings";

	saveSettings();

	qDebug() << "Main Loop exited";

	qApp->exit();

};

// Audio over UDP Code.

// Code can either send audio blocks from the sound card as UDP packets or use UDP packets from 
// a suitable source (maybe another copy of QtSM) and process them instead of input frm a sound card/

// ie act as a server or client for UDP audio.

// of course we need bidirectional audio, so even when we are a client we send modem generated samples
// to the server and as a server pass received smaples to modem

// It isn't logical to run as both client and server, so probably can use just one socket


QUdpSocket * udpSocket;

qint64 udpServerSeqno= 0;
qint64 udpClientLastSeq = 0;
qint64 udpServerLastSeq = 0;
int droppedPackets = 0;
extern "C" int UDPSoundIsPlaying;

QQueue <unsigned char *> queue;


void mynet::OpenUDP()
{
	udpSocket = new QUdpSocket();

	if (UDPServ)
	{
		udpSocket->bind(QHostAddress("0.0.0.0"), UDPServerPort);
		QTimer *timer = new QTimer(this);
		timercopy = timer;
		connect(timer, SIGNAL(timeout()), this, SLOT(dropPTT()));
	}
	else
		udpSocket->bind(QHostAddress("0.0.0.0"), UDPClientPort);

	connect(udpSocket, SIGNAL(readyRead()), this, SLOT(readPendingDatagrams()));	
}

extern "C" void Flush();

void mynet::dropPTT()
{
	timercopy->stop();
	
	if (UDPSoundIsPlaying)
	{
		// Drop PTT when all sent

		Flush();
		UDPSoundIsPlaying = 0;
		Debugprintf("PTT Off");
		RadioPTT(0, 0);
	}
}

void mynet::readPendingDatagrams()
{
	while (udpSocket->hasPendingDatagrams())
	{
		QHostAddress Addr;
		quint16 rxPort;
		char copy[1501];

		// We expect datagrams of 1040 bytes containing a 16 byte header and 512 16 bit samples
		// We should get a datagram every 43 mS. We need to use a timeout to drop PTT if running as server

		if (UDPServ)
			timercopy->start(200);

		int Len = udpSocket->readDatagram(copy, 1500, &Addr, &rxPort);

		if (Len == 1040)
		{
			qint64 Seq;

			memcpy(&Seq, copy, sizeof(udpServerSeqno));

			if (Seq < udpClientLastSeq || udpClientLastSeq == 0)

				// Client or Server Restarted

				udpClientLastSeq = Seq;

			else
			{
				int Missed = Seq - udpClientLastSeq;

				if (Missed > 100)			// probably stopped in debug
					Missed = 1;

				while (--Missed)
				{
					droppedPackets++;

					// insert silence to maintain timing

					unsigned char * pkt = (unsigned char *)malloc(1024);

					memset(pkt, 0, 1024);

					mutex.lock();
					queue.append(pkt);
					mutex.unlock();

				}
			}

			udpClientLastSeq = Seq;

			unsigned char * pkt = (unsigned char *)malloc(1024);

			memcpy(pkt, &copy[16], 1024);

			mutex.lock();
			queue.append(pkt);
			mutex.unlock();
		}
	}
}

void  mynet::socketError()
{
	char errMsg[80];
	sprintf(errMsg, "%d %s", udpSocket->state(), udpSocket->errorString().toLocal8Bit().constData());
	//	qDebug() << errMsg;
	//	QMessageBox::question(NULL, "ARDOP GUI", errMsg, QMessageBox::Yes | QMessageBox::No);
}

extern "C" void sendSamplestoStdout(short * Samples, int nSamples)
{

}


extern "C" void sendSamplestoUDP(short * Samples, int nSamples, int Port)
{
	if (udpSocket == nullptr)
		return;
	
	unsigned char txBuff[1048];

	memcpy(txBuff, &udpServerSeqno, sizeof(udpServerSeqno));
	udpServerSeqno++;

	if (nSamples > 512)
		nSamples = 512;

	nSamples <<= 1;				// short to byte

	memcpy(&txBuff[16], Samples, nSamples);

	udpSocket->writeDatagram((char *)txBuff, nSamples + 16, QHostAddress(UDPHost), Port);
}

static int min = 0, max = 0, lastlevelGUI = 0, lastlevelreport = 0;

static UCHAR CurrentLevel = 0;		// Peak from current samples

extern "C" int SoundIsPlaying;
extern "C" short * SendtoCard(short * buf, int n);
extern "C" short * DMABuffer;


extern "C" void ProcessNewSamples(short * Samples, int nSamples);


extern "C" void UDPPollReceivedSamples()
{
	if (queue.isEmpty())
		return;

	short * ptr;
	short * save;

	// If we are using UDP for output (sound server) send samples to sound card.
	// If for input (virtual sound card) then pass to modem

	if (UDPServ)
	{
		// We only get packets if TX active (sound VOX) so raise PTT and start sending
		// ?? do we ignore if local modem is already sending ??

		if (SoundIsPlaying)
		{
			mutex.lock();
			save = ptr = (short *)queue.dequeue();
			mutex.unlock();
			free(save);
		}
		
		if (UDPSoundIsPlaying == 0)
		{
			// Wait for a couple of packets to reduce risk of underrun (but not too many or delay will be excessive

			if (queue.count() < 3)
				return;

			UDPSoundIsPlaying = 1;
			Debugprintf("PTT On");
			RadioPTT(0, 1);				// UDP only use one channel

			/// !! how do we drop ptt ??
		}

		while (queue.count() > 1)
		{
			short * outptr = DMABuffer;
			boolean dropPTT1 = 1;
			boolean dropPTT2 = 1;

			// We get mono samples but soundcard expects stereo
			// Sound card needs 1024 samples so send two packets

			mutex.lock();
			save = ptr = (short *)queue.dequeue();
			mutex.unlock();

			for (int n = 0; n < 512; n++)
			{
				*(outptr++) = *ptr;
				*(outptr++) = *ptr++;	// Duplicate
				if (*ptr)
					dropPTT1 = 0;		// Drop PTT if all samples zero
			}

			free(save);

			mutex.lock();
			save = ptr = (short *)queue.dequeue();
			mutex.unlock();

			for (int n = 0; n < 512; n++)
			{
				*(outptr++) = *ptr;
				*(outptr++) = *ptr++;	// Duplicate
				if (*ptr)
					dropPTT2 = 0;		// Drop PTT if all samples zero
			}

			free(save);

			if (dropPTT1 && dropPTT2)
			{
				startTimer(1);			// All zeros so no need to send
				return;
			}

			DMABuffer = SendtoCard(DMABuffer, 1024);

			if (dropPTT2)				// 2nd all zeros
				startTimer(1);	
		}
		return;
	}

	mutex.lock();
	save = ptr = (short *)queue.dequeue();
	mutex.unlock();

	// We get mono samples but modem expects stereo

	short Buff[2048];
	short * inptr = (short *)ptr;
	short * outptr = Buff;

	int i;

	for (i = 0; i < ReceiveSize; i++)
	{
		if (*(ptr) < min)
			min = *ptr;
		else if (*(ptr) > max)
			max = *ptr;
		ptr++;
	}

	CurrentLevel = ((max - min) * 75) / 32768;	// Scale to 150 max

	if ((Now - lastlevelGUI) > 2000)	// 2 Secs
	{
		lastlevelGUI = Now;

		if ((Now - lastlevelreport) > 10000)	// 10 Secs
		{
			char HostCmd[64];
			lastlevelreport = Now;

			sprintf(HostCmd, "INPUTPEAKS %d %d", min, max);
			Debugprintf("Input peaks = %d, %d", min, max);
		}
		
		min = max = 0;
	}

	for (int n = 0; n < 512; n++)
	{
		*(outptr++) = *inptr;
		*(outptr++) = *inptr++;	// Duplicate
	}

	ProcessNewSamples(Buff, 512);
	free(save);
}



#ifdef WIN32

__declspec(dllimport) unsigned short __stdcall htons(__in unsigned short hostshort);
__declspec(dllimport) unsigned short __stdcall ntohs(__in unsigned short hostshort);

#elif defined(__APPLE__)

// macOS implements these as macros (via __builtin_constant_p) in
// <arpa/inet.h>, so Linux's forward declarations would clash.
#include <arpa/inet.h>

#else

#include <stdint.h>

uint32_t htonl(uint32_t hostlong);
uint16_t htons(uint16_t hostshort);
uint32_t ntohl(uint32_t netlong);
uint16_t ntohs(uint16_t netshort);

#endif


extern "C" void * zalloc(int len)
{
	// malloc and clear

	void * ptr;

	ptr = malloc(len);

	if (ptr)
		memset(ptr, 0, len);

	return ptr;
}

/* jer:
 * This is the original file, my mods were only to change the name/semantics on the b64decode function
 * and remove some dependencies.
 */
 /*
	 LibCGI base64 manipulation functions is extremly based on the work of Bob Tower,
	 from its projec http://base64.sourceforge.net. The functions were a bit modicated.
	 Above is the MIT license from b64.c original code:

 LICENCE:        Copyright (c) 2001 Bob Trower, Trantor Standard Systems Inc.

				 Permission is hereby granted, free of charge, to any person
				 obtaining a copy of this software and associated
				 documentation files (the "Software"), to deal in the
				 Software without restriction, including without limitation
				 the rights to use, copy, modify, merge, publish, distribute,
				 sublicense, and/or sell copies of the Software, and to
				 permit persons to whom the Software is furnished to do so,
				 subject to the following conditions:

				 The above copyright notice and this permission notice shall
				 be included in all copies or substantial portions of the
				 Software.

				 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
				 KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
				 WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
				 PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
				 OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
				 OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
				 OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
				 SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE

 */

static char mycd64[256] = "";
static const char cb64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; // / causes problems with freedata

void xencodeblock(unsigned char in[3], char out[4], int len)
{
	if (mycd64[0] == 0)
	{
		int i, j;

		for (i = 0; i < 64; i++)
		{
			j = cb64[i];
			mycd64[j] = i;
		}
	}

	out[0] = cb64[in[0] >> 2];
	out[1] = cb64[((in[0] & 0x03) << 4) | ((in[1] & 0xf0) >> 4)];
	out[2] = (unsigned char)(len > 1 ? cb64[((in[1] & 0x0f) << 2) | ((in[2] & 0xc0) >> 6)] : '=');
	out[3] = (unsigned char)(len > 2 ? cb64[in[2] & 0x3f] : '=');
}

void xdecodeblock(unsigned char in[4], unsigned char out[3])
{
	char Block[5];
	int i, j;

	for (i = 0; i < 64; i++)
	{
		j = cb64[i];
		mycd64[j] = i;
	}


	Block[0] = mycd64[in[0]];
	Block[1] = mycd64[in[1]];
	Block[2] = mycd64[in[2]];
	Block[3] = mycd64[in[3]];

	out[0] = (unsigned char)(Block[0] << 2 | Block[1] >> 4);
	out[1] = (unsigned char)(Block[1] << 4 | Block[2] >> 2);
	out[2] = (unsigned char)(((Block[2] << 6) & 0xc0) | Block[3]);
}

/**
* @ingroup libcgi_string
* @{
*/

/**
* Encodes a given tring to its base64 form.
*
* @param *str String to convert
* @return Base64 encoded String
* @see str_base64_decode
**/

char * byte_base64_encode(unsigned char *str, int len)
{
	unsigned int i = 0, j = 0;
	char *result = (char *)zalloc((len * 2) + 5);

	if (!result)
		return NULL;

	while (len > 2)
	{
		xencodeblock(&str[i], &result[j], 3);
		i += 3;
		j += 4;
		len -= 3;
	}
	if (len)
	{
		xencodeblock(&str[i], &result[j], len);
	}

	return result;
}



/*

Copyright (C) 1998, 2009
Paul E. Jones <paulej@packetizer.com>

Freeware Public License (FPL)

This software is licensed as "freeware."  Permission to distribute
this software in source and binary forms, including incorporation
into other products, is hereby granted without a fee.  THIS SOFTWARE
IS PROVIDED 'AS IS' AND WITHOUT ANY EXPRESSED OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS FOR A PARTICULAR PURPOSE.  THE AUTHOR SHALL NOT BE HELD
LIABLE FOR ANY DAMAGES RESULTING FROM THE USE OF THIS SOFTWARE, EITHER
DIRECTLY OR INDIRECTLY, INCLUDING, BUT NOT LIMITED TO, LOSS OF DATA
OR DATA BEING RENDERED INACCURATE.
*/

/*  sha1.h
 *
 *  Copyright (C) 1998, 2009
 *  Paul E. Jones <paulej@packetizer.com>
 *  All Rights Reserved
 *
 *****************************************************************************
 *  $Id: sha1.h 12 2009-06-22 19:34:25Z paulej $
 *****************************************************************************
 *
 *  Description:
 *      This class implements the Secure Hashing Standard as defined
 *      in FIPS PUB 180-1 published April 17, 1995.
 *
 *      Many of the variable names in the SHA1Context, especially the
 *      single character names, were used because those were the names
 *      used in the publication.
 *
 *      Please read the file sha1.c for more information.
 *
 */

#ifndef _SHA1_H_
#define _SHA1_H_

 /*
  *  This structure will hold context information for the hashing
  *  operation
  */
typedef struct SHA1Context
{
	unsigned Message_Digest[5]; /* Message Digest (output)          */

	unsigned Length_Low;        /* Message length in bits           */
	unsigned Length_High;       /* Message length in bits           */

	unsigned char Message_Block[64]; /* 512-bit message blocks      */
	int Message_Block_Index;    /* Index into message block array   */

	int Computed;               /* Is the digest computed?          */
	int Corrupted;              /* Is the message digest corruped?  */
} SHA1Context;

/*
 *  Function Prototypes
 */
void SHA1Reset(SHA1Context *);
int SHA1Result(SHA1Context *);
void SHA1Input(SHA1Context *, const unsigned char *, unsigned);

#endif

/*
 *  Define the circular shift macro
 */
#define SHA1CircularShift(bits,word) \
                ((((word) << (bits)) & 0xFFFFFFFF) | \
                ((word) >> (32-(bits))))

 /* Function prototypes */
void SHA1ProcessMessageBlock(SHA1Context *);
void SHA1PadMessage(SHA1Context *);


#if !defined(__APPLE__)
// macOS provides htonl as a macro in <arpa/inet.h>; redefining it as
// a function would clash. Linux/Windows still need this in-repo
// implementation.
uint32_t htonl(uint32_t x)
{
#if BYTE_ORDER == LITTLE_ENDIAN
	unsigned char *s = (unsigned char *)&x;
	return (uint32_t)(s[0] << 24 | s[1] << 16 | s[2] << 8 | s[3]);
#else
	return x;
#endif
}
#endif
void SHA1Reset(SHA1Context *context)
{
	context->Length_Low = 0;
	context->Length_High = 0;
	context->Message_Block_Index = 0;

	context->Message_Digest[0] = 0x67452301;
	context->Message_Digest[1] = 0xEFCDAB89;
	context->Message_Digest[2] = 0x98BADCFE;
	context->Message_Digest[3] = 0x10325476;
	context->Message_Digest[4] = 0xC3D2E1F0;

	context->Computed = 0;
	context->Corrupted = 0;
}

/*
 *  SHA1Result
 *
 *  Description:
 *      This function will return the 160-bit message digest into the
 *      Message_Digest array within the SHA1Context provided
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to use to calculate the SHA-1 hash.
 *
 *  Returns:
 *      1 if successful, 0 if it failed.
 *
 *  Comments:
 *
 */
int SHA1Result(SHA1Context *context)
{

	if (context->Corrupted)
	{
		return 0;
	}

	if (!context->Computed)
	{
		SHA1PadMessage(context);
		context->Computed = 1;
	}

	return 1;
}

/*
 *  SHA1Input
 *
 *  Description:
 *      This function accepts an array of octets as the next portion of
 *      the message.
 *
 *  Parameters:
 *      context: [in/out]
 *          The SHA-1 context to update
 *      message_array: [in]
 *          An array of characters representing the next portion of the
 *          message.
 *      length: [in]
 *          The length of the message in message_array
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *
 */
void SHA1Input(SHA1Context         *context,
	const unsigned char *message_array,
	unsigned            length)
{
	if (!length)
	{
		return;
	}

	if (context->Computed || context->Corrupted)
	{
		context->Corrupted = 1;
		return;
	}

	while (length-- && !context->Corrupted)
	{
		context->Message_Block[context->Message_Block_Index++] =
			(*message_array & 0xFF);

		context->Length_Low += 8;
		/* Force it to 32 bits */
		context->Length_Low &= 0xFFFFFFFF;
		if (context->Length_Low == 0)
		{
			context->Length_High++;
			/* Force it to 32 bits */
			context->Length_High &= 0xFFFFFFFF;
			if (context->Length_High == 0)
			{
				/* Message is too long */
				context->Corrupted = 1;
			}
		}

		if (context->Message_Block_Index == 64)
		{
			SHA1ProcessMessageBlock(context);
		}

		message_array++;
	}
}


BOOL SHA1PasswordHash(unsigned char * lpszPassword, char * Hash)
{
	SHA1Context sha;
	int i;

	SHA1Reset(&sha);
	SHA1Input(&sha, lpszPassword, strlen((char *)lpszPassword));
	SHA1Result(&sha);

	// swap byte order if little endian

	for (i = 0; i < 5; i++)
		sha.Message_Digest[i] = htonl(sha.Message_Digest[i]);

	memcpy(Hash, &sha.Message_Digest[0], 20);

	return TRUE;
}


/*
 *  SHA1ProcessMessageBlock
 *
 *  Description:
 *      This function will process the next 512 bits of the message
 *      stored in the Message_Block array.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      Many of the variable names in the SHAContext, especially the
 *      single character names, were used because those were the names
 *      used in the publication.
 *
 *
 */
void SHA1ProcessMessageBlock(SHA1Context *context)
{
	const unsigned K[] =            /* Constants defined in SHA-1   */
	{
		0x5A827999,
		0x6ED9EBA1,
		0x8F1BBCDC,
		0xCA62C1D6
	};
	int         t;                  /* Loop counter                 */
	unsigned    temp;               /* Temporary word value         */
	unsigned    W[80];              /* Word sequence                */
	unsigned    A, B, C, D, E;      /* Word buffers                 */

	/*
	 *  Initialize the first 16 words in the array W
	 */
	for (t = 0; t < 16; t++)
	{
		W[t] = ((unsigned)context->Message_Block[t * 4]) << 24;
		W[t] |= ((unsigned)context->Message_Block[t * 4 + 1]) << 16;
		W[t] |= ((unsigned)context->Message_Block[t * 4 + 2]) << 8;
		W[t] |= ((unsigned)context->Message_Block[t * 4 + 3]);
	}

	for (t = 16; t < 80; t++)
	{
		W[t] = SHA1CircularShift(1, W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16]);
	}

	A = context->Message_Digest[0];
	B = context->Message_Digest[1];
	C = context->Message_Digest[2];
	D = context->Message_Digest[3];
	E = context->Message_Digest[4];

	for (t = 0; t < 20; t++)
	{
		temp = SHA1CircularShift(5, A) +
			((B & C) | ((~B) & D)) + E + W[t] + K[0];
		temp &= 0xFFFFFFFF;
		E = D;
		D = C;
		C = SHA1CircularShift(30, B);
		B = A;
		A = temp;
	}

	for (t = 20; t < 40; t++)
	{
		temp = SHA1CircularShift(5, A) + (B ^ C ^ D) + E + W[t] + K[1];
		temp &= 0xFFFFFFFF;
		E = D;
		D = C;
		C = SHA1CircularShift(30, B);
		B = A;
		A = temp;
	}

	for (t = 40; t < 60; t++)
	{
		temp = SHA1CircularShift(5, A) +
			((B & C) | (B & D) | (C & D)) + E + W[t] + K[2];
		temp &= 0xFFFFFFFF;
		E = D;
		D = C;
		C = SHA1CircularShift(30, B);
		B = A;
		A = temp;
	}

	for (t = 60; t < 80; t++)
	{
		temp = SHA1CircularShift(5, A) + (B ^ C ^ D) + E + W[t] + K[3];
		temp &= 0xFFFFFFFF;
		E = D;
		D = C;
		C = SHA1CircularShift(30, B);
		B = A;
		A = temp;
	}

	context->Message_Digest[0] =
		(context->Message_Digest[0] + A) & 0xFFFFFFFF;
	context->Message_Digest[1] =
		(context->Message_Digest[1] + B) & 0xFFFFFFFF;
	context->Message_Digest[2] =
		(context->Message_Digest[2] + C) & 0xFFFFFFFF;
	context->Message_Digest[3] =
		(context->Message_Digest[3] + D) & 0xFFFFFFFF;
	context->Message_Digest[4] =
		(context->Message_Digest[4] + E) & 0xFFFFFFFF;

	context->Message_Block_Index = 0;
}

/*
 *  SHA1PadMessage
 *
 *  Description:
 *      According to the standard, the message must be padded to an even
 *      512 bits.  The first padding bit must be a '1'.  The last 64
 *      bits represent the length of the original message.  All bits in
 *      between should be 0.  This function will pad the message
 *      according to those rules by filling the Message_Block array
 *      accordingly.  It will also call SHA1ProcessMessageBlock()
 *      appropriately.  When it returns, it can be assumed that the
 *      message digest has been computed.
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to pad
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *
 */
void SHA1PadMessage(SHA1Context *context)
{
	/*
	 *  Check to see if the current message block is too small to hold
	 *  the initial padding bits and length.  If so, we will pad the
	 *  block, process it, and then continue padding into a second
	 *  block.
	 */
	if (context->Message_Block_Index > 55)
	{
		context->Message_Block[context->Message_Block_Index++] = 0x80;
		while (context->Message_Block_Index < 64)
		{
			context->Message_Block[context->Message_Block_Index++] = 0;
		}

		SHA1ProcessMessageBlock(context);

		while (context->Message_Block_Index < 56)
		{
			context->Message_Block[context->Message_Block_Index++] = 0;
		}
	}
	else
	{
		context->Message_Block[context->Message_Block_Index++] = 0x80;
		while (context->Message_Block_Index < 56)
		{
			context->Message_Block[context->Message_Block_Index++] = 0;
		}
	}

	/*
	 *  Store the message length as the last 8 octets
	 */
	context->Message_Block[56] = (context->Length_High >> 24) & 0xFF;
	context->Message_Block[57] = (context->Length_High >> 16) & 0xFF;
	context->Message_Block[58] = (context->Length_High >> 8) & 0xFF;
	context->Message_Block[59] = (context->Length_High) & 0xFF;
	context->Message_Block[60] = (context->Length_Low >> 24) & 0xFF;
	context->Message_Block[61] = (context->Length_Low >> 16) & 0xFF;
	context->Message_Block[62] = (context->Length_Low >> 8) & 0xFF;
	context->Message_Block[63] = (context->Length_Low) & 0xFF;

	SHA1ProcessMessageBlock(context);
}



// RHP code from LinBPQ

/*
Copyright 2001-2022 John Wiseman G8BPQ

This file is part of LinBPQ/BPQ32.

LinBPQ/BPQ32 is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

LinBPQ/BPQ32 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with LinBPQ/BPQ32.  If not, see http://www.gnu.org/licenses
*/

/*

	Paula (G8PZT)'s Remote Host Protocol interface.
	For now only sufficient support for WhatsPac


*/
#define _CRT_SECURE_NO_DEPRECATE


static void GetJSONValue(char * _REPLYBUFFER, char * Name, char * Value, int Len);
static int GetJSONInt(char * _REPLYBUFFER, char * Name);

// Generally Can have multiple RHP connections and each can have multiple RHF Sessions


struct RHPSessionInfo
{
	QTcpSocket * Socket;	// Websocks Socket
	int Handle;				// RHP session ID
	int Seq;
	char Local[12];
	char Remote[12];
	BOOL Connecting;		// Set while waiting for connection to complete
	BOOL Listening;
	BOOL Connected;
	TAX25Port * RHPax25Sess;
	int Busy;
};


struct RHPSessionInfo ** RHPSessions;
int NumberofRHPSessions;
int WebSocks = 0;



void mynet::onRHPConnection()
{
	RHPSocket = RHPServer->nextPendingConnection();
	connect(RHPSocket, SIGNAL(readyRead()), this, SLOT(onRHPReadyRead()));
	connect(RHPSocket, SIGNAL(stateChanged(QAbstractSocket::SocketState)), this, SLOT(onRHPSocketStateChanged(QAbstractSocket::SocketState)));
}

void mynet::onRHPSocketStateChanged(QAbstractSocket::SocketState socketState)
{
	if (socketState == QAbstractSocket::UnconnectedState)
	{
		QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());

		// Should be disconnect any sessions? yes

		int n;
		TAX25Port * AX25Sess;

		// Close any ax.25 session

		for (n = 0; n < NumberofRHPSessions; n++)
		{
			if (RHPSessions[n]->RHPax25Sess)
			{
				AX25Sess = RHPSessions[n]->RHPax25Sess;

				if (AX25Sess)
				{
					rst_timer(AX25Sess);
					set_unlink(AX25Sess, AX25Sess->Path);
				}

				RHPSessions[n]->RHPax25Sess = 0;
			}
		}
		WebSocks = 0;
	}
}

void mynet::onRHPReadyRead()
{
	QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());

	RHPProcessLine(sender);
}


void mynet::onRHPAPIConnection()
{
	RHPAPISocket = RHPAPIServer->nextPendingConnection();
	connect(RHPAPISocket, SIGNAL(readyRead()), this, SLOT(onRHPAPIReadyRead()));
	connect(RHPAPISocket, SIGNAL(stateChanged(QAbstractSocket::SocketState)), this, SLOT(onRHPAPISocketStateChanged(QAbstractSocket::SocketState)));
}


void mynet::onRHPAPISocketStateChanged(QAbstractSocket::SocketState socketState)
{
	if (socketState == QAbstractSocket::UnconnectedState)
	{
		QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());
	}
}

void mynet::onRHPAPIReadyRead()
{
	QTcpSocket* sender = static_cast<QTcpSocket*>(QObject::sender());

	RHPAPIProcessLine(sender);
}


char ErrCodes[18][24] =
{
	"Ok",
	"Unspecified",
	"Bad or missing type",
	"Invalid handle",
	"No memory",
	"Bad or missing mode",
	"Invalid local address",
	"Invalid remote address" ,
	"Bad or missing family" ,
	"Duplicate socket"   ,
	"No such port"    ,
	"Invalid protocol"      ,
	"Bad parameter" ,
	"No buffers"  ,
	"Unauthorised"  ,
	"No Route"  ,
	"Operation not supported" };




extern char pgm[256];
char szBuff[80];

int WhatsPacConfigured = 1;

int RHPPaclen = 236;



int processRHCPOpen(QTcpSocket * Socket, char * Msg, char * ReplyBuffer);
int processRHCPSend(QTcpSocket * Socket, char * Msg, char * ReplyBuffer);
int processRHCPClose(QTcpSocket * Socket, char * Msg, char * ReplyBuffer);
int processRHCPStatus(QTcpSocket * Socket, char * Msg, char * ReplyBuffer);

int Connect(int Stream)
{
	return 0;
}
int Disconnect(int Stream)
{
	return 0;
}

int SendMsg(int Stream, char * Msg, int MsgLen)
{
	return 0;
}


extern "C" void RHPSendtoServer(void * sock, char * Msg, int Len)
{
	emit t->sendtoRHP(sock, Msg, Len);
}


void mynet::sendtoRHP(void * sock, char * Msg, int Len)
{

	QTcpSocket* socket = (QTcpSocket*)sock;
	socket->write(Msg, Len);

//	free(Msg);
}




int ProcessHTTPMessage(char * MsgPtr, int MsgLen, QTcpSocket * sender);
void ProcessRHPWebSock(QTcpSocket * Socket, char * Msg, int MsgLen);

int SendResponse(QTcpSocket * sender, char * Msg)
{
	char Header[256];
	
	sprintf(Header, "HTTP/1.1 200 OK\r\n"
		"Content-Length: %d\r\n"
		"Content-Type: application/json\r\n"
		"Connection: close\r\n"
		"Access-Control-Allow-Origin: *\r\n\r\n", strlen(Msg));

	sender->write(Header);
	sender->write(Msg);

	return 0;
}


void RHPProcessLine(QTcpSocket * sender)
{
	QByteArray datas = sender->readAll();
	ProcessHTTPMessage(datas.data(), datas.length(), sender);
}

int ProcessAPIHTTPMessage(char * MsgPtr, int MsgLen, QTcpSocket * sender);

void RHPAPIProcessLine(QTcpSocket * sender)
{
	QByteArray datas = sender->readAll();
	ProcessAPIHTTPMessage(datas.data(), datas.length(), sender);
}


int ProcessHTTPMessage(char * MsgPtr, int MsgLen, QTcpSocket * sender)
{
	int allowDeflate = 0;
	char * Compressed = 0;

	unsigned char * MsgBytes = reinterpret_cast<unsigned char *>(MsgPtr);

	int ReplyLen = 0;
	char Header[256];
	int HeaderLen;
	char TimeString[64];
	int Len;
	char * WebSock = 0;
	char * HostPtr;

	char * encPtr;

	char Encoding[] = "Content-Encoding: deflate\r\n";

	HostPtr = strstr(MsgPtr, "Host: ");

	WebSock = strstr(MsgPtr, "Upgrade: websocket");

	if (HostPtr)
	{

	}

	if (memicmp(MsgPtr, "GET /rhp ", 9) == 0)
	{
		// clear any existing websock connection

		WebSocks = 0;
	}

	encPtr = strstr(MsgPtr, "Accept-Encoding:");

	if (encPtr && strstr(encPtr, "deflate"))
		allowDeflate = 1;
	else
		Encoding[0] = 0;

	if (WebSocks)
	{
		// Websocks message

		int i, j;
		int Fin, Opcode, Len, Mask;
		char MaskingKey[4];
		char * ptr;
		char * Payload;

		/*
		 +-+-+-+-+-------+-+-------------+-------------------------------+
	 |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
	 |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
	 |N|V|V|V|       |S|             |   (if payload len==126/127)   |
	 | |1|2|3|       |K|             |                               |
	 +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
	 |     Extended payload length continued, if payload len == 127  |
	 + - - - - - - - - - - - - - - - +-------------------------------+
	 |                               |Masking-key, if MASK set to 1  |
	 +-------------------------------+-------------------------------+
	 | Masking-key (continued)       |          Payload Data         |
	 +-------------------------------- - - - - - - - - - - - - - - - +
	 :                     Payload Data continued ...                :

	  Octet i of the transformed data ("transformed-octet-i") is the XOR of
   octet i of the original data ("original-octet-i") with octet at index
   i modulo 4 of the masking key ("masking-key-octet-j"):

	 j                   = i MOD 4
	 transformed-octet-i = original-octet-i XOR masking-key-octet-j
*/
		Fin = MsgBytes[0] >> 7;
		Opcode = MsgBytes[0] & 15;
		Mask = MsgBytes[1] >> 7;
		Len = MsgBytes[1] & 127;

		if (Len == 126)		// Two Byte Len
		{
			Len = (MsgBytes[2] << 8) + MsgBytes[3];
			memcpy(MaskingKey, &MsgPtr[4], 4);
			ptr = &MsgPtr[8];
		}
		else
		{
			memcpy(MaskingKey, &MsgPtr[2], 4);
			ptr = &MsgPtr[6];
		}

		Payload = ptr;

		for (i = 0; i < Len; i++)
		{
			j = i & 3;

			*ptr = *ptr ^ MaskingKey[j];
			ptr++;
		}

		if (Opcode == 8)
		{
			int n;
			TAX25Port * AX25Sess;
			
			Debugprintf("WebSock Close");

			// Close any ax.25 session
			
			for (n = 0; n < NumberofRHPSessions; n++)
			{
				if (RHPSessions[n]->RHPax25Sess)
				{
					AX25Sess = RHPSessions[n]->RHPax25Sess;

					if (AX25Sess)
					{
						rst_timer(AX25Sess);
						set_unlink(AX25Sess, AX25Sess->Path);
					}

					RHPSessions[n]->RHPax25Sess = 0;
				}
			}
			WebSocks = 0;
			return 0;
		}
		else if (Opcode == 1)
		{
			ProcessRHPWebSock(sender, Payload, Len);
			return 0;
		}
		else if (Opcode == 9)
		{
			// Ping. Return as Pong (10)

			int TxLen;
			char OutBuffer[256];

			// WebSock Encode. Buffer has 10 bytes on front for header but header len depends on Msg len

			// Two Byte Header

			OutBuffer[0] = 0x8A;		// Fin, Pong
			OutBuffer[1] = Len;

			memcpy(&OutBuffer[2], Payload, Len);
			TxLen = Len + 2;
			sender->write(OutBuffer, TxLen);
		}
		else
		{
			Debugprintf("WebSock Opcode %d Msg %s", Opcode, &MsgPtr[6]);
			return 0;
		}
	}


	if (WebSock)
	{
		// Websock connection request - Reply and remember state.

		char KeyMsg[128];
		char Webx[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";	// Fixed UID
		char Hash[64] = "";
		char * Hash64;		// base 64 version
		char * ptr;
		char Reply[256];

		//Sec-WebSocket-Key: l622yZS3n+zI+hR6SVWkPw==

		char ReplyMsg[] =
			"HTTP/1.1 101 Switching Protocols\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Accept: %s\r\n"
			//				"Sec-WebSocket-Protocol: chat\r\n"
			"\r\n";

		ptr = strstr(MsgPtr, "Sec-WebSocket-Key:");

		if (ptr)
		{
			ptr += 18;
			while (*ptr == ' ')
				ptr++;

			memcpy(KeyMsg, ptr, 40);
			strlop(KeyMsg, 13);
			strlop(KeyMsg, ' ');
			strcat(KeyMsg, Webx);

			SHA1PasswordHash((unsigned char *)&KeyMsg[0], Hash);
			Hash64 = byte_base64_encode(reinterpret_cast<unsigned char *>(Hash), 20);

			WebSocks = 1;

			ReplyLen = sprintf(Reply, ReplyMsg, Hash64);

			free(Hash64);

			sender->write(Reply, ReplyLen);

		}
	}
	return 0;
}



int ProcessAPIHTTPMessage(char * MsgPtr, int MsgLen, QTcpSocket * sender)
{
	int allowDeflate = 0;
	char * Compressed = 0;

	char * Context, *Method, *Key;

	int ReplyLen = 0;
	char Header[256];
	int HeaderLen;
	char TimeString[64];

	int Len;
	char * HostPtr;

	char * encPtr;

	char Encoding[] = "Content-Encoding: deflate\r\n";

	HostPtr = strstr(MsgPtr, "Host: ");

	if (HostPtr)
	{

	}

	encPtr = strstr(MsgPtr, "Accept-Encoding:");

	if (encPtr && strstr(encPtr, "deflate"))
		allowDeflate = 1;
	else
		Encoding[0] = 0;

	// HTTP Config Request

	if (memcmp(MsgPtr, "OPTIONS ", 8) == 0)
	{
		// CORS Request

		char Resp[] =
			"HTTP/1.1 200 OK\r\n"
			"Access-Control-Allow-Origin: *\r\n"
			"Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
			"Access-Control-Allow-Headers: authorization";

		sender->write(Resp);
		return 0;
	}


	/*

	OPTIONS /api/v1/config HTTP/1.1
Host: 127.0.0.1:9000
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:143.0) Gecko/20100101 Firefox/143.0
Accept: //*//*
Accept - Language: en - GB, en; q = 0.5
Accept - Encoding: gzip, deflate, br, zstd
Access - Control - Request - Method : POST
Access - Control - Request - Headers : content - type
Referer : http://whatspac.m0ahn.co.uk:88/
Origin: http://whatspac.m0ahn.co.uk:88
Connection: keep - alive
	Sec - Fetch - Dest : empty
	Sec - Fetch - Mode : cors
	Sec - Fetch - Site : cross - site
	Priority : u = 4




		HTTP / 1.1 200 OK
			Date : Mon, 01 Dec 2008 01 : 15 : 39 GMT
			Server : Apache / 2.0.61 (Unix)
			Access - Control - Allow - Origin : https ://foo.example
			Access - Control - Allow - Methods : POST, GET, OPTIONS
			Access - Control - Allow - Headers : X - PINGOTHER, Content - Type
			Access - Control - Max - Age : 86400
			Vary : Accept - Encoding, Origin
			Keep - Alive : timeout = 2, max = 100
			Connection : Keep - Alive
		*/





	if (memcmp(MsgPtr, "GET /api/v1/state ", 18) == 0)
	{
		if (WhatsPacConfigured)
			SendResponse(sender, (char *)"{\"configured\": true}\r\n");
		else
			SendResponse(sender, (char *)"{\"configured\": false}\r\n");

		return 0;
	}


	if (memcmp(MsgPtr, "GET /api/v1/config ", 19) == 0)
	{
		char Template[] =
			"{\"MODE\": 0,"
			"\"RHPPORT\": \"%d\","
			"\"AGWPORT\": \"%d\","
			"\"INTERFACES\": [{\"INTERFACE\": 1,\"PROTOCOL\": \"KISS\",\"TYPE\": \"TCP\",\"IOADDR\": \"127.0.0.1\",\"INTNUM\": 8100}],"
			"\"PORTS\": ["
			"{\"PORT\": 1,\"ID\": \"QTSM Port 1\", \"INTERFACENUM\": 1}"
			"%s%s%s]}";

		char Msg[512];
		char Port2[] = ",{\"PORT\": 2,\"ID\": \"QTSM Port 2\", \"INTERFACENUM\": 1}";
		char Port3[] = ",{\"PORT\": 3,\"ID\": \"QTSM Port 3\", \"INTERFACENUM\": 1}";
		char Port4[] = ",{\"PORT\": 4,\"ID\": \"QTSM Port 4\", \"INTERFACENUM\": 1}";

		sprintf(Msg, Template, RHPPort + 1, RHPPort, (soundChannel[1]) ? Port2 : "", (soundChannel[2]) ? Port3 : "", (soundChannel[3]) ? Port4 : "");

		SendResponse(sender, Msg);
		return 0;
	}
	SendResponse(sender, (char *)"{\"Version\": \"1.0\"}\r\n");
	return 0;

}




void SendWebSockMessage(QTcpSocket * socket, char * Msg, int Len)
{
	int Loops = 0;
	int Sent;
	int TxLen;
	char * OutBuffer = Msg;

	// WebSock Encode. Buffer has 10 bytes on front for header but header len depends on Msg len

	if (Len < 126)
	{
		// Two Byte Header

		OutBuffer[8] = 0x81;		// Fin, Data
		OutBuffer[9] = Len;

		TxLen = Len + 2;
		OutBuffer = &Msg[8];
	}
	else if (Len < 65536)
	{
		OutBuffer[6] = 0x81;		// Fin, Data
		OutBuffer[7] = 126;			// Unmasked, Extended Len 16
		OutBuffer[8] = Len >> 8;
		OutBuffer[9] = Len & 0xff;
		TxLen = Len + 4;
		OutBuffer = &Msg[6];
	}
	else
	{
		OutBuffer[0] = 0x81;		// Fin, Data
		OutBuffer[1] = 127;			// Unmasked, Extended Len 64 bits
		// Len is 32 bits, so pad with zeros
		OutBuffer[2] = 0;
		OutBuffer[3] = 0;
		OutBuffer[4] = 0;
		OutBuffer[5] = 0;
		OutBuffer[6] = (Len >> 24) & 0xff;
		OutBuffer[7] = (Len >> 16) & 0xff;
		OutBuffer[8] = (Len >> 8) & 0xff;
		OutBuffer[9] = Len & 0xff;

		TxLen = Len + 10;
		OutBuffer = &Msg[0];
	}


	emit t->sendtoRHP(socket, OutBuffer, TxLen);


	return;
}


void ProcessRHPWebSock(QTcpSocket * Socket, char * Msg, int MsgLen)
{
	int Loops = 0;
	int InputLen = 0;
	int Len;

	char Value[16];
	char * OutBuffer = (char *)malloc(250000);

	//	struct RHPConnectionInfo * RHPSocket = NULL;
	//	int n;

	Msg[MsgLen] = 0;


//	{"type":"open","id":5,"pfam":"ax25","mode":"stream","port":"1","local":"G8BPQ","remote":"G8BPQ-2","flags":128}
//	{"type": "openReply", "id": 82, "handle": 1, "errCode": 0, "errText": "Ok"}
//	{"seqno": 0, "type": "status", "handle": 1, "flags": 0}
//	("seqno": 1, "type": "close", "handle": 1}
//	{"id":40,"type":"close","handle":1}

//	{"seqno": 0, "type": "status", "handle": 1, "flags": 2}.~.
//	{"seqno": 1, "type": "recv", "handle": 1, "data": "Welcome to G8BPQ's Test Switch in Nottingham \rType ? for list of available commands.\r"}.

//	{"type": "status", "handle": 0}. XRouter will reply with {"type": "statusReply", "handle": 0, "errcode": 12, "errtext": "invalid handle"}. It
//	{type: 'keepalive'} if there has been no other activity for nearly 3 minutes. Replies with {"type": "keepaliveReply"}

	GetJSONValue(Msg, (char *)"\"type\":", Value, 15);

	if (_stricmp(Value, (char *)"open") == 0)
	{
		Len = processRHCPOpen(Socket, Msg, &OutBuffer[10]);		// Space at front for WebSock Header
		if (Len)
			SendWebSockMessage(Socket, OutBuffer, Len);
		return;
	}

	if (_stricmp(Value, (char *)"send") == 0)
	{
		Len = processRHCPSend(Socket, Msg, &OutBuffer[10]);		// Space at front for WebSock Header
		SendWebSockMessage(Socket, OutBuffer, Len);
		return;
	}

	if (_stricmp(Value, (char *)"close") == 0)
	{
		Len = processRHCPClose(Socket, Msg, &OutBuffer[10]);		// Space at front for WebSock Header
		SendWebSockMessage(Socket, OutBuffer, Len);
		return;
	}

	if (_stricmp(Value, (char *)"status") == 0)
	{
		Len = processRHCPStatus(Socket, Msg, &OutBuffer[10]);		// Space at front for WebSock Header
		SendWebSockMessage(Socket, OutBuffer, Len);
		return;
	}

	if (_stricmp(Value, (char *)"keepalive") == 0)
	{
		Len = sprintf(&OutBuffer[10], "{\"type\": \"keepaliveReply\"}"); // Space at front for WebSock Header
		SendWebSockMessage(Socket, OutBuffer, Len);
		return;
	}

	Debugprintf("Unrecognised RHP Message - %s", Msg);
}

void ProcessRHPWebSockClosed(QTcpSocket * socket)
{
	// Close any connections on this scoket and delete socket entry

	struct RHPSessionInfo * RHPSession = 0;
	int n;

	// Find and close any Sessions

	for (n = 0; n < NumberofRHPSessions; n++)
	{
		if (RHPSessions[n]->Socket == socket)
		{
			RHPSession = RHPSessions[n];

			if (RHPSession->RHPax25Sess)
			{
		
				//	Disconnect(RHPSession->BPQStream);

				RHPSession->RHPax25Sess = 0;
			}

			RHPSession->Connecting = 0;

			// We can't send a close to RHP endpont as socket has gone

			RHPSession->Connected = 0;
		}
	}
}

TAX25Port * RHPConnectOut(int Port, char * CallFrom, char * CallTo, char * Digis);

extern "C" TAX25Port * RHPax25Sess;
extern "C" void send_data_buf(TAX25Port * AX25Sess, int  nr);

int processRHCPOpen(QTcpSocket *  Socket, char * Msg, char * ReplyBuffer)
{
	//{"type":"open","id":5,"pfam":"ax25","mode":"stream","port":"1","local":"G8BPQ","remote":"G8BPQ-2","flags":128}
	//{"type":"open","id":7,"pfam":"ax25","mode":"trace","port":"1","flags":7}

	struct RHPSessionInfo * RHPSession = 0;

	char * Value = (char *)malloc(strlen(Msg));	// Will always be long enough
	int ID;

	char pfam[16];
	char Mode[16];
	int Port = 0;
	char Local[16];
	char Remote[16];
	int flags;
	int Handle = 1;
	unsigned char AXCall[10];
	char PortString[64];

	int n;

	// ID seems to be used for control commands like open. SeqNo for data within a session (i Think!

	ID = GetJSONInt(Msg, (char *)"\"id\":");
	GetJSONValue(Msg, (char *)"\"pfam\":", pfam, 15);
	GetJSONValue(Msg, (char *)"\"mode\":", Mode, 15);
	GetJSONValue(Msg, (char *)"\"port\":", PortString, 63);
	GetJSONValue(Msg, (char *)"\"local\":", Local, 15);
	GetJSONValue(Msg, (char *)"\"remote\":", Remote, 15);
	flags = GetJSONInt(Msg, (char *)"\"flags\":");

	if (_stricmp(pfam, (char *)"ax25") != 0)
		return sprintf(ReplyBuffer, "{\"type\": \"openReply\", \"id\": %d, \"handle\": %d, \"errCode\": 12, \"errText\": \"Bad parameter\"}", ID, 0);

	if (_stricmp(Mode, (char *)"stream") == 0)
	{	
		// Allocate a RHP Session

// See if there is an old one we can reuse

		for (n = 0; n < NumberofRHPSessions; n++)
		{
			if (RHPSessions[n]->Socket == 0)
			{
				RHPSession = RHPSessions[n];
				Handle = n + 1;
				break;
			}
		}

		if (RHPSession == 0)
		{
			RHPSessions =  (struct RHPSessionInfo **)realloc(RHPSessions, sizeof(void *) * (NumberofRHPSessions + 1));
			RHPSession = RHPSessions[NumberofRHPSessions] = (struct RHPSessionInfo *)zalloc(sizeof(struct RHPSessionInfo));
			NumberofRHPSessions++;

			Handle = NumberofRHPSessions;
		}

		RHPSession->Handle = Handle;
		RHPSession->Connecting = TRUE;
		RHPSession->Socket = Socket;

		strcpy(RHPSession->Local, Local);
		strcpy(RHPSession->Remote, Remote);

		Port = atoi(PortString) - 1;

		RHPSession->RHPax25Sess = RHPConnectOut(Port, Local, Remote, (char *)"");


		if (RHPSession == 0)
			return sprintf(ReplyBuffer, "{\"type\": \"openReply\", \"id\": %d, \"handle\": %d, \"errCode\": 12, \"errText\": \"Bad parameter\"}", ID, 0);
		else
			return sprintf(ReplyBuffer, "{\"type\": \"openReply\", \"id\": %d, \"handle\": %d, \"errCode\": 0, \"errText\": \"Ok\"}", ID, Handle);
	
	}

	return sprintf(ReplyBuffer, "{\"type\": \"openReply\", \"id\": %d, \"handle\": %d, \"errCode\": 12, \"errText\": \"Bad parameter\"}", ID, 0);
}

int processRHCPSend(QTcpSocket *  Socket, char * Msg, char * ReplyBuffer)
{
	// {"type":"send","handle":1,"data":";;;;;;\r","id":70}

	struct RHPSessionInfo * RHPSession;

	TAX25Port * AX25Sess = 0;

	int ID;
	char * Data;
	char * ptr;
	unsigned char * uptr;
	int c;
	int Len;
	unsigned int HexCode1;
	unsigned int HexCode2;

	int n;

	int Handle = 1;

	Data = (char *)malloc(strlen(Msg));

	ID = GetJSONInt(Msg, (char *)"\"id\":");
	Handle = GetJSONInt(Msg, (char *)"\"handle\":");
	GetJSONValue(Msg, (char *)"\"data\":", Data, strlen(Msg) - 1);

	if (Handle < 1 || Handle > NumberofRHPSessions)
	{
		free(Data);
		return sprintf(ReplyBuffer, "{\"type\": \"sendReply\", \"id\": %d, \"handle\": %d, \"errCode\": 3, \"errtext\": \"Invalid handle\"}", ID, Handle);
	}

	RHPSession = RHPSessions[Handle - 1];

	if (RHPSession)
		AX25Sess = RHPSession->RHPax25Sess;


	// Look for \ escapes, Can now also get \u00c3

	ptr = Data;
	Len = strlen(Data);				// in case no escapes

	while (ptr = strchr(ptr, '\\'))
	{
		c = ptr[1];

		switch (c)
		{
		case 'r':

			*ptr = 13;
			memmove(ptr + 1, ptr + 2, strlen(ptr + 1));
			break;

		case 'u':

			HexCode1 = HexCode2 = 0;

			n = toupper(ptr[2]) - '0';
			if (n > 9) n = n - 7;
			HexCode1 |= n << 4;

			n = toupper(ptr[3]) - '0';
			if (n > 9) n = n - 7;
			HexCode1 |= n;

			n = toupper(ptr[4]) - '0';
			if (n > 9) n = n - 7;
			HexCode2 |= n << 4;

			n = toupper(ptr[5]) - '0';
			if (n > 9) n = n - 7;
			HexCode2 |= n;

			if (HexCode1 == 0 || HexCode1 == 0xC2)
			{
				uptr = (unsigned char *)ptr;
				*uptr = HexCode2;
			}
			else if (HexCode1 == 0xc2)
			{
				uptr = (unsigned char *)ptr;
				*uptr = HexCode2 + 0x40;
			}

			memmove(ptr + 1, ptr + 6, strlen(ptr + 5));
			break;


		case '\\':

			*ptr = '\\';
			memmove(ptr + 1, ptr + 2, strlen(ptr + 1));
			break;

		case '"':

			*ptr = '"';
			memmove(ptr + 1, ptr + 2, strlen(ptr + 1));
			break;
		}
		ptr++;
		Len = ptr - Data;
	}

	ptr = Data;

	if (AX25Sess)
	{
		while (Len > RHPPaclen)
		{
			string * data = newString();

			stringAdd(data, reinterpret_cast<unsigned char *>(Data), RHPPaclen);
			Add(&AX25Sess->in_data_buf, data);
			send_data_buf(AX25Sess, AX25Sess->vs);

			Len -= RHPPaclen;
			Data += RHPPaclen;
		}
		string * data = newString();

		stringAdd(data, reinterpret_cast<unsigned char *>(Data), Len);
		Add(&AX25Sess->in_data_buf, data);
		send_data_buf(AX25Sess, AX25Sess->vs);
	}

//	SendMsg(RHPSession->BPQStream, ptr, Len);

//	free(Data);
	return sprintf(ReplyBuffer, "{\"type\": \"sendReply\", \"id\": %d, \"handle\": %d, \"errCode\": 0, \"errText\": \"Ok\", \"status\": %d}", ID, Handle, 2);
}


int processRHCPClose(QTcpSocket *  Socket, char * Msg, char * ReplyBuffer)
{

	// {"id":70,"type":"close","handle":1}

	TAX25Port * AX25Sess;
	struct RHPSessionInfo * RHPSession;

	int ID;
	int Handle = 1;

	char * OutBuffer = (char *)malloc(256);

	ID = GetJSONInt(Msg, (char *)"\"id\":");
	Handle = GetJSONInt(Msg, (char *)"\"handle\":");

	if (Handle < 1 || Handle > NumberofRHPSessions)
		return sprintf(ReplyBuffer, "{\"id\": %d, \"type\": \"closeReply\", \"handle\": %d, \"errcode\": 3, \"errtext\": \"Invalid handle\"}", ID, Handle);


	RHPSession = RHPSessions[Handle - 1];
//	Disconnect(RHPSession->BPQStream);
	RHPSession->Connected = 0;
	RHPSession->Connecting = 0;

	AX25Sess = RHPSession->RHPax25Sess;

	if (AX25Sess)
	{
		rst_timer(AX25Sess);

		set_unlink(AX25Sess, AX25Sess->Path);
	}

	RHPSession->RHPax25Sess = 0;

	return sprintf(ReplyBuffer, "{\"id\": %d, \"type\": \"closeReply\", \"handle\": %d, \"errcode\": 0, \"errtext\": \"Ok\"}", ID, Handle);
}

int processRHCPStatus(QTcpSocket *  Socket, char * Msg, char * ReplyBuffer)
{
	// {"type": "status", "handle": 0}. XRouter will reply with {"type": "statusReply", "handle": 0, "errcode": 3, "errtext": "invalid handle"}. It

	struct RHPSessionInfo * RHPSession;
	int Handle = 0;

	Handle = GetJSONInt(Msg, (char *)"\"handle\":");

	if (Handle < 1 || Handle > NumberofRHPSessions)
		return sprintf(ReplyBuffer, "{\"type\": \"statusReply\", \"handle\": %d, \"errcode\": 3, \"errtext\": \"Invalid handle\"}", Handle);

	RHPSession = RHPSessions[Handle - 1];

	return sprintf(ReplyBuffer, "{\"type\": \"status\", \"handle\": %d, \"flags\": 2}", RHPSession->Handle);

}

char toHex[] = "0123456789abcdef";

extern "C" int	RHPLinkClosed(TAX25Port * AX25Sess)
{
	// see if one of our connections

	struct RHPSessionInfo * RHPSession = 0;
	int n;

	for (n = 0; n < NumberofRHPSessions; n++)
	{

		if (RHPSessions[n]->RHPax25Sess == AX25Sess)
		{
			int Len;
			char * RHPMsg = (char *)malloc(256);
			RHPSession = RHPSessions[n];

			Len = sprintf(&RHPMsg[10], "{\"type\": \"close\", \"seqno\": %d, \"handle\": %d}", RHPSession->Seq++, RHPSession->Handle);
			SendWebSockMessage(RHPSession->Socket, RHPMsg, Len);

			RHPSessions[n]->RHPax25Sess = 0;
			return 1;
		}
	}
	return 0;
}


extern "C" int	RHPLinkConnected(TAX25Port * AX25Sess)
{
	// see if one of our connections

	struct RHPSessionInfo * RHPSession = 0;
	int n;

	for (n = 0; n < NumberofRHPSessions; n++)
	{

		if (RHPSessions[n]->RHPax25Sess == AX25Sess)
		{
			int Len;
			char * RHPMsg = (char *)malloc(256);
			RHPSession = RHPSessions[n];

			RHPSession->Seq = 0;
			RHPSession->Connecting = FALSE;
			RHPSession->Connected = TRUE;

			RHPMsg = (char *)malloc(256);
			Len = sprintf(&RHPMsg[10], "{\"seqno\": %d, \"type\": \"status\", \"handle\": %d, \"flags\": 2}", RHPSession->Seq++, RHPSession->Handle);
			SendWebSockMessage(RHPSession->Socket, RHPMsg, Len);

			// Send RHP CTEXT

			RHPMsg = (char *)malloc(256);
			QThread::msleep(30);			// otherwise WhatsPac doesn't display connected
			Len = sprintf(&RHPMsg[10], "{\"seqno\": %d, \"type\": \"recv\", \"handle\": %d, \"data\": \"Connected to RHP Server\\r\"}", RHPSession->Seq++, RHPSession->Handle);
			SendWebSockMessage(RHPSession->Socket, RHPMsg, Len);
			return 1;
		}
	}
	return 0;
}

extern "C" int RHPLinkRXED(TAX25Port * AX25Sess, int PID, Byte * path, string * data)
{
	struct RHPSessionInfo * RHPSession = 0;
	int n;
	unsigned char Buffer[2048];			// Space to escape control chars

	for (n = 0; n < NumberofRHPSessions; n++)
	{
		if (RHPSessions[n]->RHPax25Sess == AX25Sess)
		{
			int pktlen = data->Length;
			RHPSession = RHPSessions[n];
			
			if (pktlen > 0)
			{
				memcpy(Buffer, data->Data, data->Length);

				char * ptr = (char *)&Buffer[0];
				unsigned char c;
				int Len;
				char * RHPMsg = (char *)malloc(256);

				Buffer[pktlen] = 0;

				//				RHPSession->sockptr->LastSendTime = time(NULL);


								// Message is JSON so Convert CR to \r, \ to \\ " to \"

								// Looks like I need to escape everything not between 0x20 and 0x7f eg \u00c3


				while (c = *(ptr))
				{
					switch (c)
					{
					case 13:

						memmove(ptr + 2, ptr + 1, strlen(ptr) + 1);
						*(ptr++) = '\\';
						*(ptr++) = 'r';
						break;

					case '"':

						memmove(ptr + 2, ptr + 1, strlen(ptr) + 1);
						*(ptr++) = '\\';
						*(ptr++) = '"';
						break;

					case '\\':

						memmove(ptr + 2, ptr + 1, strlen(ptr) + 1);
						*(ptr++) = '\\';
						*(ptr++) = '\\';
						break;

					default:

						if (c > 127)
						{
							memmove(ptr + 6, ptr + 1, strlen(ptr) + 1);
							*(ptr++) = '\\';
							*(ptr++) = 'u';
							*(ptr++) = '0';
							*(ptr++) = '0';
							*(ptr++) = toHex[c >> 4];
							*(ptr++) = toHex[c & 15];
							break;
						}
						else
							ptr++;
					}
				}

				RHPMsg = (char *)malloc(2048);

				Len = sprintf(&RHPMsg[10], "{\"seqno\": %d, \"type\": \"recv\", \"handle\": %d, \"data\": \"%s\"}", RHPSession->Seq++, RHPSession->Handle, Buffer);
				SendWebSockMessage(RHPSession->Socket, RHPMsg, Len);

			}
			return  1;
		}
	}
	return 0;
}


/*

void RHPPoll()
{
	int Stream;
	int n;
	int state, change;
	int Len;
	char * RHPMsg;
	unsigned char Buffer[2048];			// Space to escape control chars
	int pktlen, count;

	struct RHPSessionInfo * RHPSession;

	for (n = 0; n < NumberofRHPSessions; n++)
	{
		RHPSession = RHPSessions[n];
		Stream = RHPSession->BPQStream;

		// See if connected state has changed

		SessionState(Stream, &state, &change);

		if (change == 1)
		{
			if (state == 1)
			{
				// Connected

				RHPSession->Seq = 0;
				RHPSession->Connecting = FALSE;
				RHPSession->Connected = TRUE;

				RHPMsg = (char *)malloc(256);
				Len = sprintf(&RHPMsg[10], "{\"seqno\": %d, \"type\": \"status\", \"handle\": %d, \"flags\": 2}", RHPSession->Seq++, RHPSession->Handle);
				SendWebSockMessage(RHPSession->Socket, RHPMsg, Len);

				// Send RHP CTEXT

				RHPMsg = (char *)malloc(256);
				QThread::msleep(30);			// otherwise WhatsPac doesn't display connected
				Len = sprintf(&RHPMsg[10], "{\"seqno\": %d, \"type\": \"recv\", \"handle\": %d, \"data\": \"Connected to RHP Server\\r\"}", RHPSession->Seq++, RHPSession->Handle);
				SendWebSockMessage(RHPSession->Socket, RHPMsg, Len);
			}
			else
			{
				// Disconnected. Send Close to client

				RHPMsg = (char *)malloc(256);
				Len = sprintf(&RHPMsg[10], "{\"type\": \"close\", \"seqno\": %d, \"handle\": %d}", RHPSession->Seq++, RHPSession->Handle);
				SendWebSockMessage(RHPSession->Socket, RHPMsg, Len);

				RHPSession->Connected = 0;
				RHPSession->Connecting = 0;

				RHPSession->BPQStream = 0;
			}
		}
		do
		{
			GetMsg(Stream, Buffer, &pktlen, &count);

			if (pktlen > 0)
			{
				char * ptr = (char *)&Buffer[0];
				unsigned char c;

				Buffer[pktlen] = 0;

//				RHPSession->sockptr->LastSendTime = time(NULL);


				// Message is JSON so Convert CR to \r, \ to \\ " to \"

				// Looks like I need to escape everything not between 0x20 and 0x7f eg \u00c3


				while (c = *(ptr))
				{
					switch (c)
					{
					case 13:

						memmove(ptr + 2, ptr + 1, strlen(ptr) + 1);
						*(ptr++) = '\\';
						*(ptr++) = 'r';
						break;

					case '"':

						memmove(ptr + 2, ptr + 1, strlen(ptr) + 1);
						*(ptr++) = '\\';
						*(ptr++) = '"';
						break;

					case '\\':

						memmove(ptr + 2, ptr + 1, strlen(ptr) + 1);
						*(ptr++) = '\\';
						*(ptr++) = '\\';
						break;

					default:

						if (c > 127)
						{
							memmove(ptr + 6, ptr + 1, strlen(ptr) + 1);
							*(ptr++) = '\\';
							*(ptr++) = 'u';
							*(ptr++) = '0';
							*(ptr++) = '0';
							*(ptr++) = toHex[c >> 4];
							*(ptr++) = toHex[c & 15];
							break;
						}
						else
							ptr++;
					}
				}

				RHPMsg = (char *)malloc(2048);

				Len = sprintf(&RHPMsg[10], "{\"seqno\": %d, \"type\": \"recv\", \"handle\": %d, \"data\": \"%s\"}", RHPSession->Seq++, RHPSession->Handle, Buffer);
				SendWebSockMessage(RHPSession->Socket, RHPMsg, Len);

			}

		} while (count > 0);
	}
}

*/

static void GetJSONValue(char * _REPLYBUFFER, char * Name, char * Value, int Len)
{
	char * ptr1, *ptr2;

	Value[0] = 0;

	ptr1 = strstr(_REPLYBUFFER, Name);

	if (ptr1 == 0)
		return;

	ptr1 += (strlen(Name) + 1);

	//	"data":"{\"t\":\"c\",\"n\":\"John\",\"c\":\"G8BPQ\",\"lm\":1737912636,\"le\":1737883907,\"led\":1737758451,\"v\":0.33,\"cc\":[{\"cid\":1,\"lp\":1737917257201,\"le\":1737913735726,\"led\":1737905249785},{\"cid\":0,\"lp\":1737324074107,\"le\":1737323831510,\"led\":1737322973662},{\"cid\":5,\"lp\":1737992107419,\"le\":1737931466510,\"led\":1737770056244}]}\r","id":28}

		// There may be escaped " in data stream

	ptr2 = strchr(ptr1, '"');

	while (*(ptr2 - 1) == '\\')
	{
		ptr2 = strchr(ptr2 + 2, '"');
	}


	if (ptr2)
	{
		size_t ValLen = ptr2 - ptr1;
		if (ValLen > Len)
			ValLen = Len;

		memcpy(Value, ptr1, ValLen);
		Value[ValLen] = 0;
	}

	return;
}


static int GetJSONInt(char * _REPLYBUFFER, char * Name)
{
	char * ptr1;

	ptr1 = strstr(_REPLYBUFFER, Name);

	if (ptr1 == 0)
		return 0;

	ptr1 += (strlen(Name));

	return atoi(ptr1);
}

TAX25Port * RHPConnectOut(int Port, char * CallFrom, char * CallTo, char * Digis)
{
	char path[128];
	Byte axpath[80];

	TAX25Port * AX25Sess;

	// Also used for 'v' - connect via digis

	AX25Sess = get_free_port(Port);

	if (AX25Sess)
	{
		AX25Sess->snd_ch = Port;

		strcpy(AX25Sess->mycall, CallFrom);
		strcpy(AX25Sess->corrcall, CallTo);

		sprintf(path, "%s,%s", CallTo, CallFrom);


		if (Digis)
		{
			// Have digis

			int nDigis = Digis[0];

			Digis++;

			while (nDigis--)
			{
				sprintf(path, "%s,%s", path, Digis);
				Digis += 10;
			}
		}

		AX25Sess->digi[0] = 0;

		//		rst_timer(snd_ch, free_port);

		strcpy(AX25Sess->kind, "Outgoing");
	
		AX25Sess->pathLen = get_addr(path, axpath);

		if (AX25Sess->pathLen == 0)
			return 0;						// Invalid Path

		strcpy((char *)AX25Sess->Path, (char *)axpath);
		reverse_addr(axpath, AX25Sess->ReversePath, AX25Sess->pathLen);


		set_link(AX25Sess, AX25Sess->Path);
	};

	return AX25Sess;
};










