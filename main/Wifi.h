#ifndef WIFI_H
#define WIFI_H

#include <Log.h>
#include <Streams.h>
#include <coroutine.h>

#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_event_loop.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"


class Wifi : public Coroutine
{
    std::string _ssid;
    std::string _pswd;
    std::string _prefix;
    std::string _ipAddress;
    int _rssi;
    uint8_t _mac[6];

public:
    Source<bool> connected;
    int rssi();
    std::string ipAddress();
    std::string ssid();

    Wifi( );
    ~Wifi();
    void setup();
    void loop();

    static esp_err_t wifi_event_handler(void* ctx, system_event_t* event);
    void init();
    void scanDoneHandler();
    void connectToAP(const char* AP);
    void startScan();
    void wifiInit();
    const char* getSSID();
};

#endif // WIFI_H
