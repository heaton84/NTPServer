#pragma once

/*
  NTPServer.h

  Implements a single-threaded NTP Server. The server will support control packets
  (specifically, only "read variable") if the onReadVariableCallback function is
  hooked into by the client.

  Revision History
  Version    Date        Author           Description
  ---------  ----------  ---------------  -----------------------------------------
  1.0.0      9/15/2022   J. Heaton        Initial library release

  TODO:
    - implement setting of variables
    - implement auto de-synch
    - implement traffic throttling
*/

#include <Udp.h>
#include <stdio.h>
#include <time.h>

/* Tracing Levels */
#define TL_NTP_ERROR            0
#define TL_NTP_WARN             1
#define TL_NTP_INFO             2
#define TL_NTP_DEBUG            3

/* Common Return Values */
#define L_NTP_R_ERROR           0
#define L_NTP_R_SUCCESS         1

/* Specific Return Values */
#define L_NTP_R_NOT_SYNCHED     2

#define L_NTP_UNSUPPORTED_VERSION  100
#define L_NTP_MISSING_DATA         101
#define L_NTP_TOO_MUCH_DATA        102
#define L_NTP_BAD_REQUEST          103
#define L_NTP_NOT_IMPLEMENTED      104
#define L_NTP_BAD_VARIABLENAME     105

/* Reject Reasons */

/* NTP Protocol Definitions */
#define L_NTP_LI_NONE                0
#define L_NTP_LI_61SEC               1
#define L_NTP_LI_59SEC               2
#define L_NTP_LI_UNSYNCH             3

#define L_NTP_VERSION                3   /* Server version to identify as in replies */
#define L_NTP_MIN_VER                3   /* Minimum packet version # to accept */
#define L_NTP_MAX_VER                4   /* Maximum packet version # to accept */

/* NTP Modes */
#define L_NTP_MODE_CLIENT            3
#define L_NTP_MODE_SERVER            4
#define L_NTP_MODE_BROADCAST         5
#define L_NTP_MODE_CONTROL           6

/* NTP Control Opcodes */
#define L_NTP_CTL_READVAR            2   /* Read System or Peer Variables */

/* NTP Stratums */
#define L_NTP_STRAT_UNSPECIFIED      0
#define L_NTP_STRAT_PRIMARY          1
#define L_NTP_STRAT_SECONDARY        2
#define L_NTP_STRAT_UNSYNCHRONIZED  16

#define L_NTP_EPOCH       2208988800UL

#define L_NTP_MAX_RX_BUFF          500  /* Max receive buffuer size, bytes */

/* Type Aliases */
typedef uint64_t      t_ntpTimestamp;   /* Type for 64-bit NTP timestamps */
typedef uint64_t      t_ntpSysClock;    /* Type for native system clock (micros64 calls) */


/* NTP Structure Definitions */

#pragma pack(push, 1)

typedef struct s_ntp_header
{
  char mode : 3;
	char vn : 3;
  char li : 2;
} S_NTP_HEADER;

typedef struct s_ntp_packet
{
	/* Header */
	S_NTP_HEADER header;

	char stratum;
	char poll;
	char precision;

	/* Root Clock Info */
	int root_delay;
	int root_dispersion;
	char reference_id[4];

	t_ntpTimestamp ts_reference;
	t_ntpTimestamp ts_origin;
	t_ntpTimestamp ts_received;
	t_ntpTimestamp ts_transmit;

} S_NTP_PACKET;

typedef struct s_ntp_control_packet
{
	/* Header */
	S_NTP_HEADER header;

  char opcode : 5;
  char more : 1;
  char error : 1;
	char response : 1;
  
	short sequence;

	short status;
	short association_id;
	short offset;
	short count;

	/* Note: Payload follows */

} S_NTP_CONTROL_PACKET;

#pragma pack(pop)



/* Begin Server Class Definition */


class NTPServer
{
private:

	union
	{
		S_NTP_HEADER          header;
		S_NTP_PACKET          packet;
		S_NTP_CONTROL_PACKET  controlPacket;
		char                  byteBuffer[L_NTP_MAX_RX_BUFF];
	} _u_packetBuffer;

	short _packetBufferPtr;                     // Points to next byte location in byteBuffer for next receive call

  /* Server State */
	char _clockIsSynchronized : 1;              // Current synch status
  char _clockSynchronizedSinceBoot : 1;       // Keeps track of synch status since boot

  t_ntpSysClock _maxTimeBetweenUpdates;       // Maximum time between updates before declaring unsynched

  /* NTP Configuration Items */
	char _stratum;
	char _maxPollInterval;
	char _precision;
	int  _rootDelay;
	int  _rootDispersion;
	char _referenceId[4];

  /* Clock Synch Items */
	t_ntpSysClock _lastTimeSyncMillis;
	t_ntpSysClock _referenceTimeMicros;
	struct tm      _referenceTime;
  time_t         _referenceTimeAsSeconds;

  /* Network Items */
  UDP *_udp;

  /* Stat Counters */
  unsigned short _requestsSucceeded,
                 _requestsFailed;

	/* Wrappers for arduino calls */
	int _recv(int cbExpectedBytes);         // Read next N bytes from input stream into tcp buffer
	int _send(int cbPacketSize);            // Send out first N bytes from the tcp buffer
  int _close(int reason);                 // Closes out current receive
  
	void _timestamp(struct timeval *tv);       // Snapshot current timestamp
  void _htonTimestamp(const struct timeval tv, t_ntpTimestamp *dest); // Copy timestamp into network packet format
  void _ntohs(short *v);

	void _handleRequest(const struct timeval tvReceived);
	void _handleControlRequest();

  int (*onReadVariableCallback)(const char *var, char *lpBuffer, int cbBuffer);

public:
	NTPServer();
  NTPServer(const char *referenceId, const char stratum);

  int begin(UDP &udp);
  void end();

	void setStratum(char stratum);
	void setMaxPollInterval(int pollIntervalSeconds);
	void setServerPrecision(double precisionInSeconds);
	void setRootDelay(double delayInSeconds);
	void setRootDispersion(double dispersionInSeconds);
	int  setReferenceId(const char * referenceId);

	void setReferenceTime(struct tm refTime);
	void setReferenceTime(struct tm refTime, t_ntpSysClock refTimeMillis);
  unsigned long getElapsedTimeSinceSync();

	int  getCurrentTime(struct tm *outTime, t_ntpSysClock *outMilliseconds);

  bool isClockSynchronized();
	void invalidateTimeSynch();

	void update(); // Checks for requests and services them, if need be

  unsigned short getSuccessfulRequests(bool resetCounter);
  unsigned short getFailedRequests(bool resetCounter);

  /* Event Hooks */
  void onReadVariable(int (*fn)(const char *var, char *lpBuffer, int cbBuffer)) { onReadVariableCallback = fn; } 
};
