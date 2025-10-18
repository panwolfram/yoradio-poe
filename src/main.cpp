#include "Arduino.h"
#include "core/options.h"
#include "core/config.h"
#include "core/telnet.h"
#include "core/player.h"
#include "core/display.h"
#include "core/network.h"
#include "core/netserver.h"
#include "core/controls.h"
#include "core/mqtt.h"
#include "core/optionschecker.h"
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
#include "freertos/FreeRTOS.h"

#define WDT_TIMEOUT_SECONDS 60

#if DSP_HSPI || TS_HSPI || VS_HSPI
SPIClass  SPI2(HOOPSENb);
#endif

extern __attribute__((weak)) void yoradio_on_setup();

static void wdt_setup() {
  esp_task_wdt_config_t cfg = {
    .timeout_ms    = WDT_TIMEOUT_SECONDS * 1000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,  // watch idle tasks on all cores
    .trigger_panic = true
  };
  if (esp_task_wdt_init(&cfg) == ESP_ERR_INVALID_STATE) {
    esp_task_wdt_reconfigure(&cfg);
  }
  esp_task_wdt_add(NULL); // subscribe current (Arduino loop) task
}

void setup() {
  wdt_setup();

  Serial.begin(115200);
  if(REAL_LEDBUILTIN!=255) pinMode(REAL_LEDBUILTIN, OUTPUT);
  if (yoradio_on_setup) yoradio_on_setup();
  pm.on_setup();
  config.init();
  display.init();
  player.init();
  network.begin();
  if (network.status != CONNECTED && network.status!=SDREADY) {
    BOOTLOG("##[BOOT]#\tNo network\tExiting");
    netserver.begin();
    initControls();
    display.putRequest(DSP_START);
    while(!display.ready()) delay(10);
    return;
  }
  if(SDC_CS!=255) {
    display.putRequest(WAITFORSD, 0);
    Serial.print("##[BOOT]#\tSD search\t");
  }
  BOOTLOG("##[BOOT]#\tInitializing playlist mode\t");
  config.initPlaylistMode();
  BOOTLOG("##[BOOT]#\tStarting netserver\t");
  netserver.begin();
  BOOTLOG("##[BOOT]#\tStarting telnet\t");
  telnet.begin();
  initControls();
  display.putRequest(DSP_START);
  while(!display.ready()) delay(10);
  #ifdef MQTT_ROOT_TOPIC
    mqttInit();
  #endif
  if (config.getMode()==PM_SDCARD) player.initHeaders(config.station.url);
  player.lockOutput=false;
  if (config.store.smartstart == 1) player.sendCommand({PR_PLAY, config.lastStation()});
  pm.on_end_setup();
}

void loop() {
  telnet.loop();
  if (network.status == CONNECTED || network.status==SDREADY) {
    player.loop();
    //loopControls();
  }
  if (config.emptyFS) {
    esp_task_wdt_reset();
  }
  loopControls();
  netserver.loop();
  // esp_task_wdt_reset();
}

#include "core/audiohandlers.h"
