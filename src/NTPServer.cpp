#include <Arduino.h>
#include <udp.h>

#include <math.h>
#include <time.h>

#include "NTPServer.h"

NTPServer::NTPServer()
{
  onReadVariableCallback      = NULL;
  
  _packetBufferPtr            = 0;
  _clockIsSynchronized        = 0;
  _clockSynchronizedSinceBoot = 0;
  _lastTimeSyncMillis         = 0;
  _requestsSucceeded          = 0;
  _requestsFailed             = 0;
  _stratum                    = L_NTP_STRAT_UNSPECIFIED;

  _maxTimeBetweenUpdates      = 5 * 60 * 1000 * 1000; // 5 minutes of drift
  
  setMaxPollInterval(64);
  setServerPrecision(1);
  setRootDelay(0);
  setRootDispersion(0);
  setReferenceId("LOCL");

  _udp = NULL;
}

NTPServer::NTPServer(const char *referenceId, const char stratum) : NTPServer()
{
  setReferenceId(referenceId);
  _stratum = stratum;
}

int NTPServer::begin(UDP &udp)
{
  // I am welcome to suggestions as to how to prevent needing an external UDP
  // object. To that end: what kind of UDP class are we employing? EthernetUdp or WifiUdp? OtherUdp?
  // This way, no matter what is employed, as long as it is derived from the UDP object we can use it.

  _udp = &udp;
}

void NTPServer::end()
{
  if (_udp)
  {
    _udp->stop();
    _udp = NULL;
  }
}

void NTPServer::update()
{
  static struct timeval   tvReceived;

  // de-sync as needed
  if (micros64() - _referenceTimeMicros > _maxTimeBetweenUpdates)
    _clockIsSynchronized = 0;
   
  if (_recv(sizeof(S_NTP_HEADER)))
  {
    /* We have something incoming, figure out what to receive after a quick sanity check on version number */
    _timestamp(&tvReceived);

    if (_u_packetBuffer.header.vn >= L_NTP_MIN_VER || _u_packetBuffer.header.vn <= L_NTP_MAX_VER)
    {
      if (_u_packetBuffer.header.mode == L_NTP_MODE_CLIENT)            /* Basic NTP Request */
      {
        if (_recv(sizeof(S_NTP_PACKET) - sizeof(S_NTP_HEADER)))
        {
          _handleRequest(tvReceived);
        }  
        else
        {
          // We did not get enough data within a reasonable timeframe
          _close(L_NTP_MISSING_DATA);
        }
      }
      else if (_u_packetBuffer.header.mode == L_NTP_MODE_CONTROL)      /* Control Mode Request */
      {        
        // Zero out the control packet addendum to prevent overwrite
        memset(&_u_packetBuffer.byteBuffer[sizeof(S_NTP_HEADER)], 0, L_NTP_MAX_RX_BUFF - sizeof(S_NTP_CONTROL_PACKET));
        
        if (_recv(sizeof(S_NTP_CONTROL_PACKET) - sizeof(S_NTP_HEADER)))
        {
          // Translate words for handler routine
          _ntohs(&_u_packetBuffer.controlPacket.sequence);
          _ntohs(&_u_packetBuffer.controlPacket.status);
          _ntohs(&_u_packetBuffer.controlPacket.association_id);
          _ntohs(&_u_packetBuffer.controlPacket.offset);
          _ntohs(&_u_packetBuffer.controlPacket.count);
          
          if (_u_packetBuffer.controlPacket.count >= 0 &&
              _u_packetBuffer.controlPacket.count <= L_NTP_MAX_RX_BUFF - sizeof(S_NTP_CONTROL_PACKET) &&
              _u_packetBuffer.controlPacket.response == 0 &&
              _u_packetBuffer.controlPacket.error == 0 &&
              _u_packetBuffer.controlPacket.more == 0)
          {
            if (_recv(_u_packetBuffer.controlPacket.count))
            {
              _handleControlRequest();
            }
            else
            {
              // Client did not send as many bytes as indicated
              _close(L_NTP_MISSING_DATA);
            }
          }
          else
          {
            if (_u_packetBuffer.controlPacket.count > L_NTP_MAX_RX_BUFF - sizeof(S_NTP_CONTROL_PACKET))
              _close(L_NTP_TOO_MUCH_DATA);
            else
              _close(L_NTP_BAD_REQUEST); // illegal request (count out of range, or R/E/M set)
          }
        }
        else
        {
          _close(L_NTP_MISSING_DATA); // malformed request
        }
      } // Mode check
      else
      {
        _close(L_NTP_NOT_IMPLEMENTED); // unsupported mode
      }
    } // Version check
    else
    {
      _close(L_NTP_UNSUPPORTED_VERSION);  // unsupported version
    }
  }

  _packetBufferPtr = 0;
}

void NTPServer::_timestamp(struct timeval *tv)
{
  // Gets the current time
  // This is last reference time PLUS elpased microseconds since that sync

  static t_ntpTimestamp delta;

  if (_clockSynchronizedSinceBoot)
  {
    delta = micros64() - _referenceTimeMicros;
  
    tv->tv_sec = _referenceTimeAsSeconds;

    while (delta >= 100000000) {
      delta -= 100000000;
      tv->tv_sec+=100;
    }  
    while (delta >= 10000000) {
      delta -= 10000000;
      tv->tv_sec+=10;
    }  
    while (delta >= 1000000) {
      delta -= 1000000;
      tv->tv_sec++;
    }
    
    tv->tv_usec = delta;
  }
  else
  {
    tv->tv_sec = 0;
    tv->tv_usec = 0;
  }
}

unsigned long NTPServer::getElapsedTimeSinceSync()
{
  return millis() - _lastTimeSyncMillis;
}

void NTPServer::_htonTimestamp(const struct timeval tv, t_ntpTimestamp *dest)
{
  // Packs a timeval struct into NTP format (mainly used to assemble packets)
  
  static uint32_t w;
  static char *ptr;
  
  ptr = (char *)dest;

  w = L_NTP_EPOCH + tv.tv_sec;
  ptr[0] = (w >> 24) & 0xFF;
  ptr[1] = (w >> 16) & 0xFF;
  ptr[2] = (w >> 8) & 0xFF;
  ptr[3] = w & 0xFF;

  w = (tv.tv_usec * 1825) >> 5;
  w = ((tv.tv_usec << 12) + (tv.tv_usec << 8) - w);

  ptr[4] = (w >> 24) & 0xFF;
  ptr[5] = (w >> 16) & 0xFF;
  ptr[6] = (w >> 8) & 0xFF;
  ptr[7] = w & 0xFF;  
}

void NTPServer::_ntohs(short *v)
{
  // Swap bytes to network order
  // TODO: Determine based on processor type if this needs to be done
  static char *cv;
  static char t;

  cv = (char *)v;

  t = cv[0];
  cv[0] = cv[1];
  cv[1] = t;
}

void NTPServer::_handleRequest(const struct timeval tvReceived)
{
  // We've already validated the request, pack in the required data and send it back.
  static struct timeval tv;

  if (!_clockIsSynchronized)
  {
    _u_packetBuffer.header.li = L_NTP_LI_UNSYNCH;
  }
  else
  {
    // Note that at this time, we don't have any notion of leap second so we can't
    // report anything. If someone knows how to get this out of a GPS, please
    // implement it here
    
    _u_packetBuffer.header.li = L_NTP_LI_NONE;
  }

  _u_packetBuffer.header.vn              = L_NTP_VERSION;
  _u_packetBuffer.header.mode            = L_NTP_MODE_SERVER;

  _u_packetBuffer.packet.stratum         = (_clockSynchronizedSinceBoot ? _stratum : L_NTP_STRAT_UNSYNCHRONIZED);
  _u_packetBuffer.packet.poll            = _maxPollInterval;
  _u_packetBuffer.packet.precision       = _precision;

  _u_packetBuffer.packet.root_delay      = _rootDelay;
  _u_packetBuffer.packet.root_dispersion = _rootDispersion;

  memcpy(_u_packetBuffer.packet.reference_id, _referenceId, sizeof(_referenceId));

  // Mirror transmit time back to sender
  _u_packetBuffer.packet.ts_origin = _u_packetBuffer.packet.ts_transmit;

  _timestamp(&tv);
  tv.tv_usec = 0;

  _htonTimestamp(tvReceived, &_u_packetBuffer.packet.ts_received);
  _htonTimestamp(tv,         &_u_packetBuffer.packet.ts_reference);

  _timestamp(&tv);
  _htonTimestamp(tv,         &_u_packetBuffer.packet.ts_transmit);

  _send(sizeof(S_NTP_PACKET));
  
  _requestsSucceeded++;
}

void NTPServer::_handleControlRequest()
{
  static short reply_sz;  
  
  if (_u_packetBuffer.controlPacket.opcode == L_NTP_CTL_READVAR)
  {
    
    // L_NTP_CTL_READVAR // "TZ"
    _u_packetBuffer.controlPacket.response = 1;

    // Calc max space for handler routine to dump data
    reply_sz = L_NTP_MAX_RX_BUFF - sizeof(S_NTP_CONTROL_PACKET);
  
    if (onReadVariableCallback != NULL)
    {
      if (L_NTP_R_SUCCESS != onReadVariableCallback(&_u_packetBuffer.byteBuffer[sizeof(S_NTP_CONTROL_PACKET)], &_u_packetBuffer.byteBuffer[sizeof(S_NTP_CONTROL_PACKET)], reply_sz))
      {
        // Callback reported a non-success, ignore request
        _close(L_NTP_BAD_VARIABLENAME);
      }
      else
      {
        // Callback returned success.
        // Protect against misbehaving callback, ensure a null terminator at the end of the buffer
        _u_packetBuffer.byteBuffer[ L_NTP_MAX_RX_BUFF - 1 ] = 0;

        // Callback should have stuffed a string in the buffer. Send that back to NTP client
        _u_packetBuffer.controlPacket.count = strlen( &_u_packetBuffer.byteBuffer[sizeof(S_NTP_CONTROL_PACKET)] );   
        reply_sz = sizeof(S_NTP_CONTROL_PACKET) + _u_packetBuffer.controlPacket.count;
    
        _ntohs(&_u_packetBuffer.controlPacket.sequence);
        _ntohs(&_u_packetBuffer.controlPacket.status);
        _ntohs(&_u_packetBuffer.controlPacket.association_id);
        _ntohs(&_u_packetBuffer.controlPacket.offset);
        _ntohs(&_u_packetBuffer.controlPacket.count);
           
        _send(reply_sz);
        _requestsSucceeded++;
      }
    }
    else
    {
      // Callback not implemented
      _close(L_NTP_NOT_IMPLEMENTED);
    }
  }
}

/***** System Calls ******/

int NTPServer::_recv(int cbExpectedBytes)
{
  // Read in next cbExpectedBytes at mReceiveBufferPtr, return 1 if byte count matches

  static int rx;
  static char firstCall = 1;

  if (_udp == NULL)
    return L_NTP_R_ERROR;

  if (L_NTP_MAX_RX_BUFF - _packetBufferPtr < cbExpectedBytes)
    return L_NTP_R_ERROR; // Not enough room in buffer

  if (firstCall)
  {
    _udp->parsePacket();
    firstCall = 0;
  }

  if (_udp->available() >= cbExpectedBytes)
  {
    rx = _udp->read(&_u_packetBuffer.byteBuffer[_packetBufferPtr], cbExpectedBytes);

    _packetBufferPtr += rx;

    if (rx >= cbExpectedBytes)
      return L_NTP_R_SUCCESS;
  }
  else
  {    
    // Nothing left. Look for another packet.
    if (_udp->parsePacket() > 0)
    {
      // We had another packet. Reset pointers and try again.
      _packetBufferPtr = 0;

      if (_udp->available() >= cbExpectedBytes)
      {
        rx = _udp->read(&_u_packetBuffer.byteBuffer[_packetBufferPtr], cbExpectedBytes);
    
        _packetBufferPtr += rx;
    
        if (rx >= cbExpectedBytes)
          return L_NTP_R_SUCCESS;
      }
    }
  }
  
  return L_NTP_R_ERROR;
}

int NTPServer::_send(int cbPacketSize)
{
  if (_udp == NULL)
    return L_NTP_R_ERROR;
     
  // Sends out the current packet buffer as a single packet
  _udp->beginPacket(_udp->remoteIP(), _udp->remotePort());
  _udp->write((const unsigned char *)_u_packetBuffer.byteBuffer, cbPacketSize);
  
  return (_udp->endPacket() == 1 ? L_NTP_R_SUCCESS : L_NTP_R_ERROR);
}

int NTPServer::_close(int reason)
{
  if (_udp->available() > 0)
  {
    _recv(_udp->available());
  }

  _requestsFailed++;
}

/***** setter methods ******/

void NTPServer::setStratum(char stratum)
{
  _stratum = stratum;
}

void NTPServer::setMaxPollInterval(int pollIntervalSeconds)
{
  // NTP Poll Interval is defined as:
  // Interval = 2 ^ x
  //
  // i.e. x=6, Interval=2^6=64 seconds

  _maxPollInterval = (char)(log((float)pollIntervalSeconds) / log(2.0f));
}

void NTPServer::setServerPrecision(double precisionInSeconds)
{
  // Server precision is specified in log2 seconds, signed byte
  //
  // i.e. x=-6, Interval=2^-6=0.015625 seconds

  _precision = (signed char)(log(precisionInSeconds) / log(2.0));
}

void NTPServer::setRootDelay(double delayInSeconds)
{
  // Root delay is stored in a 64-bit fixed decimal point format
  int delay = (int)delayInSeconds;
  _rootDelay = htonl(delay);
}

void NTPServer::setRootDispersion(double dispersionInSeconds)
{
  int disp = (int)dispersionInSeconds;
  _rootDispersion = htonl(disp);
}

/**
  * setReferenceId
  *
  * Sets the NTP Reference ID. Parameter must be a 4-byte string.
  */
int NTPServer::setReferenceId(const char * referenceId)
{
  if (strlen(referenceId) <= sizeof _referenceId)
  {
    memset(_referenceId, 0, sizeof(_referenceId));
    strcpy(_referenceId, referenceId);
    
    return L_NTP_R_SUCCESS;
  }

  return L_NTP_R_ERROR;
}

void NTPServer::invalidateTimeSynch()
{
  _clockIsSynchronized = 0;
}

void NTPServer::setReferenceTime(struct tm refTime)
{ 
  setReferenceTime(refTime, micros64());
}

void NTPServer::setReferenceTime(struct tm refTime, t_ntpSysClock refTimeMicros)
{
  _referenceTime = refTime;              // Time aquired from external source
  _referenceTimeMicros = refTimeMicros;  // Timestamp at which this time was acquried (used to compute fractional seconds)

  _lastTimeSyncMillis = millis();        // Keeps track of how long it has been since the sync time was set
  _clockIsSynchronized = 1;              // Clock is now synchronized
  _clockSynchronizedSinceBoot = 1;

  _referenceTimeAsSeconds = mktime(&refTime);
}

int NTPServer::getCurrentTime(struct tm *outTime, t_ntpSysClock *outMilliseconds)
{
  int result = L_NTP_R_ERROR;

  t_ntpSysClock deltaMicros;
  *outTime = _referenceTime;

  if (_clockIsSynchronized)
  {
    deltaMicros = micros64() - _referenceTimeMicros;

    while (deltaMicros > 1000000)
    {
      deltaMicros -= 1000000;
      outTime->tm_sec++;

      if (outTime->tm_sec > 250)
      {
        // Make sure that things don't overflow on long loss-of-synch timeframes
        mktime(outTime);
      }
    }

    // One last call to adjust
    mktime(outTime);
    *outMilliseconds = deltaMicros/1000;
    result = L_NTP_R_SUCCESS;
  }
  else
  {
    deltaMicros = 0;
    result = L_NTP_R_NOT_SYNCHED;
  }

  return result;
}

bool NTPServer::isClockSynchronized()
{
  return (_clockIsSynchronized != 0);
}

unsigned short NTPServer::getSuccessfulRequests(bool resetCounter)
{
  static unsigned short req;

  req = _requestsSucceeded;

  if (resetCounter)
    _requestsSucceeded = 0;
  
  return req;
}

unsigned short NTPServer::getFailedRequests(bool resetCounter)
{
  static unsigned short req;

  req = _requestsFailed;

  if (resetCounter)
    _requestsFailed = 0;
  
  return req;
}
