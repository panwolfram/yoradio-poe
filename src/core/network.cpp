#include "network.h"
#include "display.h"
#include "options.h"
#include "config.h"
#include "telnet.h"
#include "netserver.h"
#include "player.h"
#include "mqtt.h"

#include <ETH.h>
#include <ESPmDNS.h>
#include <Ticker.h>   // for ctimer/rtimer used by ticks()/raiseSoftAP()

#ifndef WIFI_ATTEMPTS
  #define WIFI_ATTEMPTS  16
#endif

MyNetwork network;

/* --- timers from the original release --- */
Ticker ctimer, rtimer;

/* --- sync/task stuff from the original release --- */
TaskHandle_t syncTaskHandle;
bool getWeather(char *wstr);
void doSync(void * pvParameters);

/* ================= Ethernet event bridge ================= */
static void onNetEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      network.setEthParams();
      // if(strlen(config.store.mdnsname)>0){
      //   ETH.setHostname(config.store.mdnsname);
      //   MDNS.begin(config.store.mdnsname);
      // }else{
      //   char buf[MDNS_LENGTH];
      //   snprintf(buf, MDNS_LENGTH, "lan-streamer-%x", config.getChipId());
      //   ETH.setHostname(buf);
      //   MDNS.begin(buf);
      // }
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      network.status = CONNECTED;
      network.beginReconnect = false;
      player.lockOutput = false;
      delay(100);
      display.putRequest(NEWMODE, PLAYER);

      if (config.getMode() == PM_SDCARD) {
        display.putRequest(NEWIP, 0);
      } else {
        if (config.store.smartstart == 1) {
          player.sendCommand({PR_PLAY, config.lastStation()});
        }
      }
      #ifdef MQTT_ROOT_TOPIC
        connectToMqtt();
      #endif
      break;

    case ARDUINO_EVENT_ETH_DISCONNECTED:
      if (!network.beginReconnect) {
        Serial.println("Lost ETH link, waiting for reconnection...");
        if (config.getMode() == PM_SDCARD) {
          network.status = SDREADY;
          display.putRequest(NEWIP, 0);
        } else {
          network.lostPlaying = player.isRunning();
          if (network.lostPlaying) { player.lockOutput = true; player.sendCommand({PR_STOP, 0}); }
          display.putRequest(NEWMODE, LOST);
        }
      }
      network.beginReconnect = true;  // ETH auto-retries
      break;

    default:
      break;
  }
}

/* ================= Ethernet bring-up ================= */
bool MyNetwork::ethBegin(uint32_t timeoutMs /*=15000*/) {
  // Pin/type/clock come from myoptions.h
  if (!ETH.begin(ETH_TYPE,        // eth_phy_type_t
                 ETH_PHY_ADDR,    // int32_t
                 ETH_MDC_PIN,     // int
                 ETH_MDIO_PIN,    // int
                 ETH_POWER_PIN,   // int
                 ETH_CLK_MODE)) { // eth_clock_mode_t
    return false;
  }

  uint32_t start = millis();
  while (!ETH.linkUp()) {
    if (millis() - start > timeoutMs) return false;
    delay(50);
  }

  // Wait for DHCP (or call ETH.config(...) first for static IP)
  while (ETH.localIP() == IPAddress(0,0,0,0)) {
    if (millis() - start > timeoutMs + 5000) return false;
    delay(50);
  }
  return true;
}

/* ================= original ticks() with minimal tweaks ================= */
void ticks() {
  if(!display.ready()) return; // waiting for SD ready
  pm.on_ticker();

  static const uint16_t weatherSyncInterval = 1800;
#if RTCSUPPORTED
  static const uint32_t timeSyncInterval = 86400;
  static uint32_t timeSyncTicks = 0;
#else
  static const uint16_t timeSyncInterval = 3600;
  static uint16_t timeSyncTicks = 0;
#endif
  static uint16_t weatherSyncTicks = 0;
  static bool divrssi;

  timeSyncTicks++;
  weatherSyncTicks++;
  divrssi = !divrssi;

  if (network.status == CONNECTED) {
    if (network.forceTimeSync || network.forceWeather) {
      xTaskCreatePinnedToCore(doSync, "doSync", 1024 * 4, NULL, 0, &syncTaskHandle, 0);
    }
    if (timeSyncTicks >= timeSyncInterval) {
      timeSyncTicks = 0;
      network.forceTimeSync = true;
    }
    if (weatherSyncTicks >= weatherSyncInterval) {
      weatherSyncTicks = 0;
      network.forceWeather = true;
    }
  }

#ifndef DSP_LCD
  if(config.store.screensaverEnabled && display.mode()==PLAYER && !player.isRunning()){
    config.screensaverTicks++;
    if(config.screensaverTicks > config.store.screensaverTimeout+SCREENSAVERSTARTUPDELAY){
      if(config.store.screensaverBlank) display.putRequest(NEWMODE, SCREENBLANK);
      else                              display.putRequest(NEWMODE, SCREENSAVER);
    }
  }
  if(config.store.screensaverPlayingEnabled && display.mode()==PLAYER && player.isRunning()){
    config.screensaverPlayingTicks++;
    if(config.screensaverPlayingTicks > config.store.screensaverPlayingTimeout*60+SCREENSAVERSTARTUPDELAY){
      if(config.store.screensaverPlayingBlank) display.putRequest(NEWMODE, SCREENBLANK);
      else                                     display.putRequest(NEWMODE, SCREENSAVER);
    }
  }
#endif

#if RTCSUPPORTED
  if(config.isRTCFound()){
    rtc.getTime(&network.timeinfo);
    mktime(&network.timeinfo);
    display.putRequest(CLOCK);
  }
#else
  if(network.timeinfo.tm_year>100 || network.status == SDREADY) {
    network.timeinfo.tm_sec++;
    mktime(&network.timeinfo);
    display.putRequest(CLOCK);
  }
#endif

  if(player.isRunning() && config.getMode()==PM_SDCARD)
    netserver.requestOnChange(SDPOS, 0);

  if(divrssi) {
    if(network.status == CONNECTED){
      // No RSSI on Ethernet; keep 0 (or synthesize a “link quality” if you want)
      netserver.setRSSI(0);
      netserver.requestOnChange(NRSSI, 0);
      display.putRequest(DSPRSSI, netserver.getRSSI());
    }
#ifdef USE_SD
    if(display.mode()!=SDCHANGE) player.sendCommand({PR_CHECKSD, 0});
#endif
    player.sendCommand({PR_VUTONUS, 0});
  }
}

/* ================= legacy Wi-Fi callbacks (unused for ETH; kept for ABI) ================= */
void MyNetwork::WiFiReconnected(WiFiEvent_t, WiFiEventInfo_t){
  network.beginReconnect = false;
  player.lockOutput = false;
  delay(100);
  display.putRequest(NEWMODE, PLAYER);
  if(config.getMode()==PM_SDCARD) {
    network.status=CONNECTED;
    display.putRequest(NEWIP, 0);
  }else{
    if (network.lostPlaying) player.sendCommand({PR_PLAY, config.lastStation()});
  }
  #ifdef MQTT_ROOT_TOPIC
    connectToMqtt();
  #endif
}

void MyNetwork::WiFiLostConnection(WiFiEvent_t, WiFiEventInfo_t){
  if(!network.beginReconnect){
    Serial.println("Lost connection, reconnecting...");
    if(config.getMode()==PM_SDCARD) {
      network.status=SDREADY;
      display.putRequest(NEWIP, 0);
    }else{
      network.lostPlaying = player.isRunning();
      if (network.lostPlaying) { player.lockOutput = true; player.sendCommand({PR_STOP, 0}); }
      display.putRequest(NEWMODE, LOST);
    }
  }
  network.beginReconnect = true;
}

/* “wifiBegin” callers exist in old code paths; just wait for ETH link */
bool MyNetwork::wifiBegin(bool silent){
  uint32_t attempts = WIFI_ATTEMPTS;
  while (!ETH.linkUp() && attempts--) {
    if(!silent) Serial.print(".");
    if(REAL_LEDBUILTIN!=255 && !silent) digitalWrite(REAL_LEDBUILTIN, !digitalRead(REAL_LEDBUILTIN));
    delay(500);
  }
  return ETH.linkUp();
}

/* ================= begin(): switch to ETH ================= */
#define DBGAP false

void MyNetwork::begin() {
  BOOTLOG("network.begin");
  config.initNetwork();

  // start clean + request syncs
  ctimer.detach();
  forceTimeSync = true;
  forceWeather  = true;

  // route ETH events
  WiFi.onEvent(onNetEvent);

  if (config.ssidsCount == 0 || DBGAP) {
    // raiseSoftAP();
    ethBegin();
    setEthParams();
    return;
  }

  if (config.getMode() != PM_SDCARD) {
    if (!ethBegin()) {
      // raiseSoftAP();                  // keep AP fallback for first-time config
      setEthParams();
      Serial.println("##[BOOT]#\tEthernet init failed!");
      return;
    }
    Serial.println(".");
    status = CONNECTED;

    // start services & network params
    netserver.begin(true);
    telnet.begin(true);
    display.putRequest(NEWIP, 0);
  } else {
    status = SDREADY;
  }

  Serial.println("##[BOOT]#\tdone");
  if(REAL_LEDBUILTIN!=255) digitalWrite(REAL_LEDBUILTIN, LOW);

#if RTCSUPPORTED
  if(config.isRTCFound()){
    rtc.getTime(&network.timeinfo);
    mktime(&network.timeinfo);
    display.putRequest(CLOCK);
  }
#endif

  // 1 Hz “ticks” like the old build
  ctimer.attach(1, ticks);

  if (network_on_connect) network_on_connect();
  pm.on_connect();
}

/* ================= ETH params / SNTP / mDNS ================= */
void MyNetwork::setEthParams(){
  // Hostname + mDNS
  if(strlen(config.store.mdnsname)>0){
    ETH.setHostname(config.store.mdnsname);
    MDNS.begin(config.store.mdnsname);
  }else{
    char buf[MDNS_LENGTH];
    snprintf(buf, MDNS_LENGTH, "lan-streamer-%x", config.getChipId());
    ETH.setHostname(buf);
    MDNS.begin(buf);
  }

  // allocate weather buffer like the old Wi-Fi version
  weatherBuf = NULL;
  trueWeather = false;
#if (DSP_MODEL!=DSP_DUMMY || defined(USE_NEXTION)) && !defined(HIDE_WEATHER)
  weatherBuf = (char *) malloc(sizeof(char) * WEATHER_STRING_L);
  if (weatherBuf) memset(weatherBuf, 0, WEATHER_STRING_L);
#endif

  // SNTP
  if(strlen(config.store.sntp1)>0 && strlen(config.store.sntp2)>0){
    configTime(config.store.tzHour * 3600 + config.store.tzMin * 60,
               config.getTimezoneOffset(),
               config.store.sntp1, config.store.sntp2);
  }else if(strlen(config.store.sntp1)>0){
    configTime(config.store.tzHour * 3600 + config.store.tzMin * 60,
               config.getTimezoneOffset(),
               config.store.sntp1);
  }
}

/* ================= misc kept from original ================= */
void MyNetwork::requestTimeSync(bool withTelnetOutput, uint32_t clientId) {
  if (withTelnetOutput) {
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    if (config.store.tzHour < 0) {
      telnet.printf(clientId, "##SYS.DATE#: %s%03d:%02d\n> ", timeStringBuff, config.store.tzHour, config.store.tzMin);
    } else {
      telnet.printf(clientId, "##SYS.DATE#: %s+%02d:%02d\n> ", timeStringBuff, config.store.tzHour, config.store.tzMin);
    }
  }
}

static void rebootTime() { ESP.restart(); }

void MyNetwork::raiseSoftAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, apPassword);
  Serial.println("##[BOOT]#");
  BOOTLOG("************************************************");
  BOOTLOG("Running in AP mode");
  BOOTLOG("Connect to AP %s with password %s", apSsid, apPassword);
  BOOTLOG("and go to http:/192.168.4.1/ to configure");
  BOOTLOG("************************************************");
  status = SOFT_AP;
  if(config.store.softapdelay>0)
    rtimer.once(config.store.softapdelay*60, rebootTime);
}

void MyNetwork::requestWeatherSync(){
  display.putRequest(NEWWEATHER);
}

/* ================= doSync + getWeather from original ================= */
void doSync(void * pvParameters) {
  static uint8_t tsFailCnt = 0;

  if(network.forceTimeSync){
    network.forceTimeSync = false;
    if(getLocalTime(&network.timeinfo)){
      tsFailCnt = 0;
      network.forceTimeSync = false;
      mktime(&network.timeinfo);
      display.putRequest(CLOCK);
      network.requestTimeSync(true);
      #if RTCSUPPORTED
        if (config.isRTCFound()) rtc.setTime(&network.timeinfo);
      #endif
    }else{
      if(tsFailCnt<4){
        network.forceTimeSync = true;
        tsFailCnt++;
      }else{
        network.forceTimeSync = false;
        tsFailCnt=0;
      }
    }
  }

  if(network.weatherBuf && (strlen(config.store.weatherkey)!=0 && config.store.showweather) && network.forceWeather){
    network.forceWeather = false;
    network.trueWeather = getWeather(network.weatherBuf);
  }

  vTaskDelete(NULL);
}

bool getWeather(char *wstr) {
#if (DSP_MODEL!=DSP_DUMMY || defined(USE_NEXTION)) && !defined(HIDE_WEATHER)
  WiFiClient client;                 // works fine with ETH stack underneath
  const char* host  = "api.openweathermap.org";

  if (!client.connect(host, 80)) {
    Serial.println("##WEATHER###: connection  failed");
    return false;
  }
  char httpget[250] = {0};
  sprintf(httpget, "GET /data/2.5/weather?lat=%s&lon=%s&units=%s&lang=%s&appid=%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
          config.store.weatherlat, config.store.weatherlon, weatherUnits, weatherLang, config.store.weatherkey, host);
  client.print(httpget);

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 2000UL) {
      Serial.println("##WEATHER###: client available timeout !");
      client.stop();
      return false;
    }
  }
  timeout = millis();
  String line = "";
  if (client.connected()) {
    while (client.available()) {
      line = client.readStringUntil('\n');
      if (strstr(line.c_str(), "\"temp\"") != NULL) {
        client.stop();
        break;
      }
      if ((millis() - timeout) > 500) {
        client.stop();
        Serial.println("##WEATHER###: client read timeout !");
        return false;
      }
    }
  }
  if (strstr(line.c_str(), "\"temp\"") == NULL) {
    Serial.println("##WEATHER###: weather not found !");
    return false;
  }

  char *tmpe;
  char *tmps;
  char *tmpc;
  const char* cursor = line.c_str();
  char desc[120], temp[20], hum[20], press[20], icon[5];

  tmps = strstr(cursor, "\"description\":\"");
  if (tmps == NULL) { Serial.println("##WEATHER###: description not found !"); return false;}
  tmps += 15;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: description not found !"); return false;}
  strlcpy(desc, tmps, tmpe - tmps + 1);
  cursor = tmpe + 2;

  tmps = strstr(cursor, "\"icon\":\"");
  if (tmps == NULL) { Serial.println("##WEATHER###: icon not found !"); return false;}
  tmps += 8;
  tmpe = strstr(tmps, "\"}");
  if (tmpe == NULL) { Serial.println("##WEATHER###: icon not found !"); return false;}
  strlcpy(icon, tmps, tmpe - tmps + 1);
  cursor = tmpe + 2;

  tmps = strstr(cursor, "\"temp\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: temp not found !"); return false;}
  tmps += 7;
  tmpe = strstr(tmps, ",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: temp not found !"); return false;}
  strlcpy(temp, tmps, tmpe - tmps + 1);
  cursor = tmpe + 1;
  float tempf = atof(temp);

  tmps = strstr(cursor, "\"feels_like\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: feels_like not found !"); return false;}
  tmps += 13;
  tmpe = strstr(tmps, ",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: feels_like not found !"); return false;}
  strlcpy(temp, tmps, tmpe - tmps + 1);
  cursor = tmpe + 2;
  float tempfl = atof(temp); (void)tempfl;

  tmps = strstr(cursor, "\"pressure\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: pressure not found !"); return false;}
  tmps += 11;
  tmpe = strstr(tmps, ",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: pressure not found !"); return false;}
  strlcpy(press, tmps, tmpe - tmps + 1);
  cursor = tmpe + 2;
  int pressi = (float)atoi(press) / 1.333;

  tmps = strstr(cursor, "humidity\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: humidity not found !"); return false;}
  tmps += 10;
  tmpe = strstr(tmps, ",\"");
  tmpc = strstr(tmps, "}");
  if (tmpe == NULL) { Serial.println("##WEATHER###: humidity not found !"); return false;}
  strlcpy(hum, tmps, tmpe - tmps + (tmpc>tmpe?1:0));

  tmps = strstr(cursor, "\"grnd_level\":");
  bool grnd_level_pr = (tmps != NULL);
  if(grnd_level_pr){
    tmps += 13;
    tmpe = strstr(tmps, ",\"");
    if (tmpe == NULL) { Serial.println("##WEATHER###: grnd_level not found !"); return false;}
    strlcpy(press, tmps, tmpe - tmps + 1);
    cursor = tmpe + 2;
    pressi = (float)atoi(press) / 1.333;
  }

  tmps = strstr(cursor, "\"speed\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: wind speed not found !"); return false;}
  tmps += 8;
  tmpe = strstr(tmps, ",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: wind speed not found !"); return false;}
  strlcpy(temp, tmps, tmpe - tmps + 1);
  cursor = tmpe + 1;
  float wind_speed = atof(temp); (void)wind_speed;

  tmps = strstr(cursor, "\"deg\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: wind deg not found !"); return false;}
  tmps += 6;
  tmpe = strstr(tmps, ",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: wind deg not found !"); return false;}
  strlcpy(temp, tmps, tmpe - tmps + 1);
  cursor = tmpe + 1;
  int wind_deg = atof(temp)/22.5;
  if(wind_deg<0) wind_deg = 16+wind_deg;

  #ifdef USE_NEXTION
    nextion.putcmdf("press_txt.txt=\"%dmm\"", pressi);
    nextion.putcmdf("hum_txt.txt=\"%d%%\"", atoi(hum));
    char cmd[30];
    snprintf(cmd, sizeof(cmd)-1,"temp_txt.txt=\"%.1f\"", tempf);
    nextion.putcmd(cmd);
    int iconofset;
    if(strstr(icon,"01")!=NULL)      iconofset = 0;
    else if(strstr(icon,"02")!=NULL) iconofset = 1;
    else if(strstr(icon,"03")!=NULL) iconofset = 2;
    else if(strstr(icon,"04")!=NULL) iconofset = 3;
    else if(strstr(icon,"09")!=NULL) iconofset = 4;
    else if(strstr(icon,"10")!=NULL) iconofset = 5;
    else if(strstr(icon,"11")!=NULL) iconofset = 6;
    else if(strstr(icon,"13")!=NULL) iconofset = 7;
    else if(strstr(icon,"50")!=NULL) iconofset = 8;
    else                             iconofset = 9;
    nextion.putcmd("cond_img.pic", 50+iconofset);
    nextion.weatherVisible(1);
  #endif

  Serial.printf("##WEATHER###: description: %s, temp:%.1f C, pressure:%dmmHg, humidity:%s%%\n",
                desc, tempf, pressi, hum);
  #ifdef WEATHER_FMT_SHORT
    sprintf(wstr, weatherFmt, tempf, pressi, hum);
  #else
    #if EXT_WEATHER
      sprintf(wstr, weatherFmt, desc, tempf, tempfl, pressi, hum, wind_speed, wind[wind_deg]);
    #else
      sprintf(wstr, weatherFmt, desc, tempf, pressi, hum);
    #endif
  #endif
  network.requestWeatherSync();
  return true;
#else
  return false;
#endif
}
