#ifndef WIFI_H
#define WIFI_H

#include <Log.h>
#include <Streams.h>
#include <coroutine.h>

#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_event.h"
#include "esp_system.h"
//#include "esp_event_loop.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"


class Wifi {
		std::string _pswd;
		std::string _prefix;
		uint8_t _mac[6];

	public:
		ValueFlow<bool> connected=false;
		ValueFlow<int> rssi;
		ValueFlow<std::string> ipAddress;
		ValueFlow<std::string> ssid;
		ValueFlow<uint64_t> mac;
		ValueFlow<std::string> macAddress;

		Wifi( );
		~Wifi();
		void init();
		static esp_err_t wifi_event_handler(void* ctx, system_event_t* event);
		bool scanDoneHandler();
		void connectToAP(const char* AP);
		void startScan();
		void wifiInit();
};

#endif // WIFI_H
