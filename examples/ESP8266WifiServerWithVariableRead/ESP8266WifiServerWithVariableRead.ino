//#include <WiFi.h>
#include "ESP8266WiFi.h"

#include <sys/time.h>      /* Note: This may need to be changed to just "#include <time.h>" depending on your board */
#include <WiFiNTPServer.h>

WiFiNTPServer ntpServer("GPS", L_NTP_STRAT_PRIMARY);

void setup() {

  struct tm tmRef;

	Serial.begin(115200);
	while (!Serial)
  {
    yield();
  }

  Serial.print("\n\nConnecting to wifi");

  WiFi.mode(WIFI_STA);
	WiFi.begin("ssid-goes-here", "password-goes-here");

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);  
    Serial.print(".");
  }

  Serial.printf("\nConnected to wifi at address %s\n", WiFi.localIP().toString().c_str());

  // Jan 1, 2022 02:00:00 UTC
  tmRef.tm_year = 2022 - 1900;  // Note: 0 = 1900
  tmRef.tm_mon = 0;             // Note: 0 = January
  tmRef.tm_mday = 1;
  tmRef.tm_hour = 2;
  tmRef.tm_min = 0;
  tmRef.tm_sec = 0;

  ntpServer.onReadVariable(ntpServerReadVariable);  // Set up read callback
  ntpServer.setReferenceTime(tmRef);

  ntpServer.begin();
}

void loop() {

  ntpServer.update();

}


int ntpServerReadVariable(const char *var, char *lpBuffer, int cbBuffer)
{
  // Only respond to the variable name "arduino"

  if (!strncasecmp(var, "arduino", strlen("arduino")))
  {
    strcpy(lpBuffer, "Bonjour!");
    return L_NTP_R_SUCCESS;
  }

  return L_NTP_R_ERROR;
}
