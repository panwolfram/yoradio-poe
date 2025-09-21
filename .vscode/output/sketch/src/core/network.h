#line 1 "C:\\Users\\mwolfram\\Documents\\Arduino Projects\\yoradio\\yoRadio\\src\\core\\network.h"
#ifndef network_h
#define network_h

#include <Ticker.h>
#include <time.h>
#include <WiFi.h>       // still needed: ETH events use WiFi.onEvent
#include "rtcsupport.h"

#define apSsid      "yoRadioAP"
#define apPassword  "12345987"

#define WEATHER_STRING_L 254

enum n_Status_e { CONNECTED, SOFT_AP, FAILED, SDREADY };

class MyNetwork {
public:
  n_Status_e status;
  struct tm timeinfo;

  // state
  bool firstRun;
  bool forceTimeSync, forceWeather;
  bool lostPlaying = false, beginReconnect = false;

  // timers & weather buffer
  Ticker ctimer;
  char *weatherBuf = nullptr;
  bool trueWeather = false;

public:
  MyNetwork() {}
  void begin();

  // ETH path
  bool ethBegin(uint32_t timeoutMs = 15000);
  void setEthParams();

  // kept as a shim so old callsites compile; it just waits for ETH link
  bool wifiBegin(bool silent = false);

  void requestTimeSync(bool withTelnetOutput = false, uint32_t clientId = 0);
  void requestWeatherSync();

private:
  Ticker rtimer;
  void raiseSoftAP();

  // Old Wi-Fi callbacks (harmless to keep, not used for ETH)
  static void WiFiLostConnection(WiFiEvent_t event, WiFiEventInfo_t info);
  static void WiFiReconnected(WiFiEvent_t event, WiFiEventInfo_t info);
};

extern MyNetwork network;
extern __attribute__((weak)) void network_on_connect();

#endif
