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

//#define TXSILENCE

// UZ7HO Soundmodem Port by John Wiseman G8BPQ
//
//	Audio interface Routine

//	Passes audio samples to/from the sound interface

//	As this is platform specific it also has the main() routine, which does
//	platform specific initialisation before calling ardopmain()

//	Windows Version is Waveout.c

// Now have separate Linux and Alsa Modules, so we can use Linux code on OSX using QSound instead of ALSA

// This is Linux bits extracted from AlsaSound

#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>

#include "UZ7HOStuff.h"

#define VOID void

char * strlop(char * buf, char delim);

int gethints();

struct timespec pttclk;

extern int Closing;

int SoundMode = 0;
int stdinMode = 0;
int onlyMixSnoop = 0;

int txLatency;

//#define SHARECAPTURE		// if defined capture device is opened and closed for each transission

#define HANDLE int

void gpioSetMode(unsigned gpio, unsigned mode);
void gpioWrite(unsigned gpio, unsigned level);
int WriteLog(char * msg, int Log);
int _memicmp(unsigned char *a, unsigned char *b, int n);
int stricmp(const unsigned char * pStr1, const unsigned char *pStr2);
int gpioInitialise(void);
HANDLE OpenCOMPort(char * pPort, int speed, BOOL SetDTR, BOOL SetRTS, BOOL Quiet, int Stopbits);
int CloseSoundCard();
int PackSamplesAndSend(short * input, int nSamples);
void displayLevel(int max);
BOOL WriteCOMBlock(HANDLE fd, char * Block, int BytesToWrite);
VOID processargs(int argc, char * argv[]);
void PollReceivedSamples();
void closeTraceLog();


HANDLE OpenCOMPort(char * Port, int speed, BOOL SetDTR, BOOL SetRTS, BOOL Quiet, int Stopbits);
VOID COMSetDTR(HANDLE fd);
VOID COMClearDTR(HANDLE fd);
VOID COMSetRTS(HANDLE fd);
VOID COMClearRTS(HANDLE fd);

int oss_read(short * samples, int nSamples);
int oss_write(short * ptr, int len);
int oss_flush();
int oss_audio_open(char * adevice_in, char * adevice_out);
void oss_audio_close();

int listpulse();
int pulse_read(short * ptr, int len);
int pulse_write(short * ptr, int len);
int pulse_flush();
int pulse_audio_open(char * adevice_in, char * adevice_out);
void pulse_audio_close();


int initdisplay();

extern BOOL blnDISCRepeating;
extern BOOL UseKISS;			// Enable Packet (KISS) interface

extern short * DMABuffer;

#define MaxReceiveSize 2048		// Enough for 9600
#define MaxSendSize 4096

short buffer[2][MaxSendSize * 2];		// Two Transfer/DMA buffers of 0.1 Sec  (x2 for Stereo)
short inbuffer[MaxReceiveSize * 2];	// Input Transfer/ buffers of 0.1 Sec (x2 for Stereo)


extern short * DMABuffer;
extern int Number;

int ReceiveSize = 512;
int SendSize = 1024;
int using48000 = 0;

int SampleRate = 12000;


BOOL UseLeft = TRUE;
BOOL UseRight = TRUE;
char LogDir[256] = "";

void WriteDebugLog(char * Msg);

VOID Debugprintf(const char * format, ...)
{
	char Mess[10000];
	va_list(arglist);

	va_start(arglist, format);
	vsprintf(Mess, format, arglist);
	WriteDebugLog(Mess);

	return;
}


void Sleep(int mS)
{
	usleep(mS * 1000);
	return;
}


// Windows and ALSA work with signed samples +- 32767
// STM32 and Teensy DAC uses unsigned 0 - 4095


BOOL Loopback = FALSE;
//BOOL Loopback = TRUE;

int Ticks;

int LastNow;

extern int Number;				// Number waiting to be sent

#include <stdarg.h>

FILE *logfile[3] = {NULL, NULL, NULL};
char LogName[3][256] = {"ARDOPDebug", "ARDOPException", "ARDOPSession"};

#define DEBUGLOG 0
#define EXCEPTLOG 1
#define SESSIONLOG 2

FILE *statslogfile = NULL;

void printtick(char * msg)
{
	Debugprintf("%s %i", msg, Now - LastNow);
	LastNow = Now;
}

struct timespec time_start;

unsigned int getTicks()
{	
	struct timespec tp;
	
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return (tp.tv_sec - time_start.tv_sec) * 1000 + (tp.tv_nsec - time_start.tv_nsec) / 1000000;
}

void PlatformSleep(int mS)
{
	Sleep(mS);
}

// PTT via GPIO code

#ifdef __ARM_ARCH

#define PI_INPUT  0
#define PI_OUTPUT 1
#define PI_ALT0   4
#define PI_ALT1   5
#define PI_ALT2   6
#define PI_ALT3   7
#define PI_ALT4   3
#define PI_ALT5   2

// Set GPIO pin as output and set low

void SetupGPIOPTT()
{
	if (pttGPIOPin == -1)
	{
		Debugprintf("GPIO PTT disabled"); 
		useGPIO = FALSE;
	}
	else
	{
		if (pttGPIOPin < 0) {
			pttGPIOInvert = TRUE;
			pttGPIOPin = -pttGPIOPin;
		}

		gpioSetMode(pttGPIOPin, PI_OUTPUT);
		gpioWrite(pttGPIOPin, pttGPIOInvert ? 1 : 0);
		Debugprintf("Using GPIO pin %d for Left/Mono PTT", pttGPIOPin); 

		if (pttGPIOPinR != -1)
		{
			gpioSetMode(pttGPIOPinR, PI_OUTPUT);
			gpioWrite(pttGPIOPinR, pttGPIOInvert ? 1 : 0);
			Debugprintf("Using GPIO pin %d for Right PTT", pttGPIOPin);
		}

		useGPIO = TRUE;
	}
}
#endif


static void sigterm_handler(int n)
{
	UNUSED(n);

	printf("terminating on SIGTERM\n");
	Closing = TRUE;
}

static void sigint_handler(int n)
{
	UNUSED(n);

	printf("terminating on SIGINT\n");
	Closing = TRUE;
}

char * PortString = NULL;


void platformInit()
{
	struct sigaction act;

//	Sleep(1000);	// Give LinBPQ time to complete init if exec'ed by linbpq

	// Get Time Reference
		
	clock_gettime(CLOCK_MONOTONIC, &time_start);
	LastNow = getTicks();

	// Trap signals

	memset (&act, '\0', sizeof(act));
 
	act.sa_handler = &sigint_handler; 
	if (sigaction(SIGINT, &act, NULL) < 0) 
		perror ("SIGINT");

	act.sa_handler = &sigterm_handler; 
	if (sigaction(SIGTERM, &act, NULL) < 0) 
		perror ("SIGTERM");

	act.sa_handler = SIG_IGN; 

	if (sigaction(SIGHUP, &act, NULL) < 0) 
		perror ("SIGHUP");

	if (sigaction(SIGPIPE, &act, NULL) < 0) 
		perror ("SIGPIPE");
}

void txSleep(int mS)
{
	// called while waiting for next TX buffer. Run background processes

	if (mS < 0)
		return;

	while (mS > 50)
	{
		PollReceivedSamples();			// discard any received samples

		Sleep(50);
		mS -= 50;
	}

	Sleep(mS);

	PollReceivedSamples();			// discard any received samples
}
 

// Routine to check that library is available

int CheckifLoaded()
{
	// Prevent CTRL/C from closing the TNC
	// (This causes problems if the TNC is started by LinBPQ)

	signal(SIGHUP, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

	return TRUE;
}

/*
   tiny_gpio.c
   2016-04-30
   Public Domain
*/
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#define GPSET0 7
#define GPSET1 8

#define GPCLR0 10
#define GPCLR1 11

#define GPLEV0 13
#define GPLEV1 14

#define GPPUD     37
#define GPPUDCLK0 38
#define GPPUDCLK1 39

unsigned piModel;
unsigned piRev;

static volatile uint32_t  *gpioReg = MAP_FAILED;

#define PI_BANK (gpio>>5)
#define PI_BIT  (1<<(gpio&0x1F))

/* gpio modes. */

void gpioSetMode(unsigned gpio, unsigned mode)
{
   int reg, shift;

   reg   =  gpio/10;
   shift = (gpio%10) * 3;

   gpioReg[reg] = (gpioReg[reg] & ~(7<<shift)) | (mode<<shift);
}

int gpioGetMode(unsigned gpio)
{
   int reg, shift;

   reg   =  gpio/10;
   shift = (gpio%10) * 3;

   return (*(gpioReg + reg) >> shift) & 7;
}

/* Values for pull-ups/downs off, pull-down and pull-up. */

#define PI_PUD_OFF  0
#define PI_PUD_DOWN 1
#define PI_PUD_UP   2

void gpioSetPullUpDown(unsigned gpio, unsigned pud)
{
   *(gpioReg + GPPUD) = pud;

   usleep(20);

   *(gpioReg + GPPUDCLK0 + PI_BANK) = PI_BIT;

   usleep(20);
  
   *(gpioReg + GPPUD) = 0;

   *(gpioReg + GPPUDCLK0 + PI_BANK) = 0;
}

int gpioRead(unsigned gpio)
{
   if ((*(gpioReg + GPLEV0 + PI_BANK) & PI_BIT) != 0) return 1;
   else                                         return 0;
}
void gpioWrite(unsigned gpio, unsigned level)
{
   if (level == 0)
	   *(gpioReg + GPCLR0 + PI_BANK) = PI_BIT;
   else
	   *(gpioReg + GPSET0 + PI_BANK) = PI_BIT;
}

void gpioTrigger(unsigned gpio, unsigned pulseLen, unsigned level)
{
   if (level == 0) *(gpioReg + GPCLR0 + PI_BANK) = PI_BIT;
   else            *(gpioReg + GPSET0 + PI_BANK) = PI_BIT;

   usleep(pulseLen);

   if (level != 0) *(gpioReg + GPCLR0 + PI_BANK) = PI_BIT;
   else            *(gpioReg + GPSET0 + PI_BANK) = PI_BIT;
}

/* Bit (1<<x) will be set if gpio x is high. */

uint32_t gpioReadBank1(void) { return (*(gpioReg + GPLEV0)); }
uint32_t gpioReadBank2(void) { return (*(gpioReg + GPLEV1)); }

/* To clear gpio x bit or in (1<<x). */

void gpioClearBank1(uint32_t bits) { *(gpioReg + GPCLR0) = bits; }
void gpioClearBank2(uint32_t bits) { *(gpioReg + GPCLR1) = bits; }

/* To set gpio x bit or in (1<<x). */

void gpioSetBank1(uint32_t bits) { *(gpioReg + GPSET0) = bits; }
void gpioSetBank2(uint32_t bits) { *(gpioReg + GPSET1) = bits; }

unsigned gpioHardwareRevision(void)
{
   static unsigned rev = 0;

   FILE * filp;
   char buf[512];
   char term;
   int chars=4; /* number of chars in revision string */

   if (rev) return rev;

   piModel = 0;

   filp = fopen ("/proc/cpuinfo", "r");

   if (filp != NULL)
   {
      while (fgets(buf, sizeof(buf), filp) != NULL)
      {
         if (piModel == 0)
         {
            if (!strncasecmp("model name", buf, 10))
            {
               if (strstr (buf, "ARMv6") != NULL)
               {
                  piModel = 1;
                  chars = 4;
               }
               else if (strstr (buf, "ARMv7") != NULL)
               {
                  piModel = 2;
                  chars = 6;
               }
               else if (strstr (buf, "ARMv8") != NULL)
               {
                  piModel = 2;
                  chars = 6;
               }
            }
         }

         if (!strncasecmp("revision", buf, 8))
         {
            if (sscanf(buf+strlen(buf)-(chars+1),
               "%x%c", &rev, &term) == 2)
            {
               if (term != '\n') rev = 0;
            }
         }
      }

      fclose(filp);
   }
   return rev;
}

int gpioInitialise(void)
{
   int fd;

   piRev = gpioHardwareRevision(); /* sets piModel and piRev */

   fd = open("/dev/gpiomem", O_RDWR | O_SYNC) ;

   if (fd < 0)
   {
      fprintf(stderr, "failed to open /dev/gpiomem\n");
      return -1;
   }

   gpioReg = (uint32_t *)mmap(NULL, 0xB4, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

   close(fd);

   if (gpioReg == MAP_FAILED)
   {
      fprintf(stderr, "Bad, mmap failed\n");
      return -1;
   }
   return 0;
}



char Leds[8]= {0};
unsigned int PKTLEDTimer = 0;



static struct speed_struct
{
	int	user_speed;
	speed_t termios_speed;
} speed_table[] = {
	{300,         B300},
	{600,         B600},
	{1200,        B1200},
	{2400,        B2400},
	{4800,        B4800},
	{9600,        B9600},
	{19200,       B19200},
	{38400,       B38400},
	{57600,       B57600},
	{115200,      B115200},
	{-1,          B0}
};


VOID COMSetDTR(HANDLE fd)
{
	int status;

	ioctl(fd, TIOCMGET, &status);
	status |= TIOCM_DTR;
	ioctl(fd, TIOCMSET, &status);
}

VOID COMClearDTR(HANDLE fd)
{
	int status;

	ioctl(fd, TIOCMGET, &status);
	status &= ~TIOCM_DTR;
	ioctl(fd, TIOCMSET, &status);
}

VOID COMSetRTS(HANDLE fd)
{
	int status;

	if (ioctl(fd, TIOCMGET, &status) == -1)
		perror("COMSetRTS PTT TIOCMGET");
	status |= TIOCM_RTS;
	if (ioctl(fd, TIOCMSET, &status) == -1)
		perror("COMSetRTS PTT TIOCMSET");
}

VOID COMClearRTS(HANDLE fd)
{
	int status;

	if (ioctl(fd, TIOCMGET, &status) == -1)
		perror("COMClearRTS PTT TIOCMGET");
	status &= ~TIOCM_RTS;
	if (ioctl(fd, TIOCMSET, &status) == -1)
		perror("COMClearRTS PTT TIOCMSET");

}



HANDLE OpenCOMPort(char * Port, int speed, BOOL SetDTR, BOOL SetRTS, BOOL Quiet, int Stopbits)
{
	char buf[256];

	//	Linux Version.

	int fd;
	u_long param = 1;
	struct termios term;
	struct speed_struct *s;

	char fulldev[80];

	UNUSED(Stopbits);

	sprintf(fulldev, "/dev/%s", Port);

	printf("%s\n", fulldev);

	if ((fd = open(fulldev, O_RDWR | O_NDELAY)) == -1)
	{
		if (Quiet == 0)
		{
			perror("Com Open Failed");
			sprintf(buf, " %s could not be opened", (char *)fulldev);
			Debugprintf(buf);
		}
		return 0;
	}

	// Validate Speed Param

	for (s = speed_table; s->user_speed != -1; s++)
		if (s->user_speed == speed)
			break;

	if (s->user_speed == -1)
	{
		fprintf(stderr, "tty_speed: invalid speed %d", speed);
		return FALSE;
	}

	if (tcgetattr(fd, &term) == -1)
	{
		perror("tty_speed: tcgetattr");
		return FALSE;
	}

	cfmakeraw(&term);
	cfsetispeed(&term, s->termios_speed);
	cfsetospeed(&term, s->termios_speed);

	if (tcsetattr(fd, TCSANOW, &term) == -1)
	{
		perror("tty_speed: tcsetattr");
		return FALSE;
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

BOOL WriteCOMBlock(HANDLE fd, char * Block, int BytesToWrite)
{
	//	Some systems seem to have a very small max write size

	int ToSend = BytesToWrite;
	int Sent = 0, ret;

	while (ToSend)
	{
		ret = write(fd, &Block[Sent], ToSend);

		if (ret >= ToSend)
			return TRUE;

//		perror("WriteCOM");

		if (ret == -1)
		{
			if (errno != 11 && errno != 35)					// Would Block
				return FALSE;

			usleep(10000);
			ret = 0;
		}

		Sent += ret;
		ToSend -= ret;
	}
	return TRUE;
}

VOID CloseCOMPort(HANDLE fd)
{
	close(fd);
}

unsigned short * sendSamplestoQSound(unsigned short * buf, int n);
void ProcessNewSamples(short * Samples, int nSamples);
int SoundCardWrite(short * input, int nSamples);


short * SendtoCard(short * buf, int n)
{
	if (Loopback)
	{
		// Loop back   to decode for testing

		ProcessNewSamples(buf, 1200);		// signed
	}

	if (SoundMode == 5)			// QSound
	{
		sendSamplestoQSound(buf, n);
		return buf;
	}


	if (SoundMode == 1)			// OSS
		oss_write(buf, n);
	else if (SoundMode == 2)	// Pulse
		pulse_write(buf, n);
	else
		SoundCardWrite(buf, n);

	return buf;
}



// Microsoft routines not available in gcc

int memicmp(unsigned char *a, unsigned char *b, int n)
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
int stricmp(const unsigned char * pStr1, const unsigned char *pStr2)
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
char * strupr(char* s)
{
	char* p = s;

	if (s == 0)
		return 0;

	while (*p = toupper(*p)) p++;
	return s;
}

char * strlwr(char* s)
{
	char* p = s;
	while (*p = tolower(*p)) p++;
	return s;
}




