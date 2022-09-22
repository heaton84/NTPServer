# NTPServer
An Arduino-targetted NTP Server class that supports both basic NTP requests, and a limited subset of control requests. I built this library to also serve up timezone information via NTP management requests.

Note that the documentation is currently a work in progress.

## Quick Start

To initialize the NTP Server, you need to know what stratum and time reference you are using. In my case, I was obtaining my time source from a GPS receiver. This puts me in stratum 1 as a GPS reference:

```
WiFiNTPServer myServer("GPS", L_NTP_STRAT_PRIMARY);   // Note: L_NTP_STRAT_PRIMARY=1
```

Now we can initialize the library with the `begin` method:

```
void setup() {
	myServer.begin();
}
```

The next step is to add a call to `update` in your main loop:

```
void loop() {
	myServer.update();
}

```

Lastly, the server will need to know the current time, which brings us to:

---

## Setting Server Time

#### void setReferenceTime(struct tm refTime, t_ntpSysClock refTimeMillis)

This method sets the reference time from an external source. The two parameters involved are as follows:

1. `refTime`: The reference time (i.e. the time as parsed from the GPS receiver)
2. `refTimeMillis`: The processor time (i.e. a snapshot of `millis()`) that the reference time was taken at.

The more often the reference time is set from an external source, the more accurate the server will be. In the case of a GPS time server, you should capture `refTimeMillis` at the rising edge of the PPS signal, then call `setReferenceTime` once the serial time data has been decoded.

#### void setReferenceTime(struct tm refTime)

Sets the reference time, taking the current value of millis() for convenience (note: this method is not as accurate).

# NTP Configuration

#### setStratum(char stratum)

Sets the current stratum of the server. Valid values are 1 through 16. The stratum is reported back to clients with every request.

#### setMaxPollInterval(int pollIntervalSeconds)

Sets the maximum poll interval that will be reported to clients. Note that the actual interval that will be locked in will be to the closest power of 2, as the NTP protocol definition of poll interval is 2 to the power of the interval.

#### setServerPrecision(double precisionInSeconds)

Sets the reported server precision, in seconds.

#### setRootDelay(double delayInSeconds)

Sets the root delay of the time source.

#### setRootDispersion(double dispersionInSeconds)

Sets the root dispersion of the time source.

#### setReferenceId(const char * referenceId)

Sets the NTP reference ID, in character format. Note that this function will fail if the reference ID is more than 4 bytes (exclusing null terminator).

# Other Functions

#### getElapsedTimeSinceSync()

Returns the number of milliseconds since the last time sync was performed via `setReferenceTime`

#### getCurrentTime(struct tm *outTime, t_ntpSysClock *outMilliseconds)

Returns the current local time, which is the last reference time plus the number of milliseconds since the last clock synchronization. This will only be as accurate as your processor's clock.

#### isClockSynchronized()

Returns `true` if the server's clock is in a synchonized state. The clock may desynchronize if we have not had a recent `setReferenceTime` call. This "maximum synchronization interval" is currently hard-coded, but soon will be configurable.

A note on clock synchronization: if the server determines that the clock is not in a synchonized state, all NTP requests will have the warning set that the clock is no longer synchonized (LI will be set to 3 and stratum will be set to 16).

#### invalidateTimeSynch()

Calling this function will force the clock to a desynchronized state. It is intended for the user to use this in extreme cases when the clock can no longer be trusted.

#### getSuccessfulRequests(bool resetCounter)

Returns the number of successful NTP requests serviced since the last inquiry. If `resetCounter` is set, the internal server counter will be set back to zero.

#### getFailedRequests(bool resetCounter)

Returns the number of malformed/rejected NTP requests serviced since the last inquiry. If `resetCounter` is set, the internal server counter will be set back to zero.

---

# Reading Variables

To enable variable reading through NTP control packets, you need to hook into the `onReadVariable` callback. Until the callback is hooked into, any control requests against the server will fail. To set the callback, do the following:

```
int myCallbackFunction(const char *var, char *lpBuffer, int cbBuffer)
{
	int result = L_NTP_R_ERROR;
	
	if (!strncasecmp(var, "TZ", strlen(var)))
	{
		// Note: Take care not to exceed the buffer space as defined by cbBuffer
		// Report back the current timezone setting (POSIX format)
		strcpy(lpBuffer, "EST+5EDT,M3.2.0/2,M11.1.0/2");
		
		result = L_NTP_R_SUCCESS; // Inform server class that we have a valid response
	}
	
	return result;
}

myServer.onReadVariable(myCallbackFunction);

```
