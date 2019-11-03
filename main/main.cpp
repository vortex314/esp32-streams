
#include <Config.h>
#include <Wifi.h>
#include <Mqtt.h>
#include <LedBlinker.h>
#include "freertos/task.h"

typedef struct { uint32_t id;} TimerMsg;

class TimerSource : public Source<uint32_t>,public Coroutine {
		uint32_t _interval;
		uint32_t _id;
	public:
		TimerSource(uint32_t interval,uint32_t id) : Coroutine("TimerSource") {_interval=interval; _id=id;}
		void setup() { };
		void loop() {
			PT_BEGIN();
			while(true) {
				timeout(_interval);
				PT_YIELD_UNTIL(timeout());
				emit(_id);
			}
			PT_END();
		}

};

template <class T>
class MqttSource : public Source<MqttMessage> {
		std::string _name;
		T& _v;
	public:
		MqttSource(const char* name,T& v):_v(v),_name(name) {};
		void txd() {
			std::string s;
			DynamicJsonDocument doc(100);
			JsonVariant variant = doc.to<JsonVariant>();
			variant.set(_v);
			serializeJson(doc, s);
			this->emit({_name, s});
		}
};

template <class T>
class MqttLambdaSource : public Source<MqttMessage> {
		std::string _name;
		std::function<T()> _handler;
	public:
		MqttLambdaSource(const char* name,std::function<T()> handler):_handler(handler),_name(name) {};
		void txd() {
			std::string s;
			DynamicJsonDocument doc(100);
			JsonVariant variant = doc.to<JsonVariant>();
			T v = _handler();
			variant.set(v);
			serializeJson(doc, s);
			this->emit({_name, s});
		}
};

//______________________________________________________________________
//
#define PIN_LED 2

LedBlinker led(PIN_LED,100);

Wifi wifi;
Mqtt mqtt;
Log logger(1024);
MqttLambdaSource<uint64_t> systemUptime("system/upTime",[]() {
	return Sys::millis();
});
MqttLambdaSource<uint32_t> systemHeap("system/heap",[]() {
	return xPortGetFreeHeapSize();
});
MqttLambdaSource<std::string> systemHostname("system/hostname",[]() {
	return Sys::hostname();
});
MqttLambdaSource< int> rssiSource("wifi/rssi",[]() {
	return wifi.rssi();
});

MqttLambdaSource< std::string> wifiIpAddress("wifi/ipAddress",[]() {
	return wifi.ipAddress();
});

MqttLambdaSource< std::string> wifiSsid("wifi/ssid",[]() {
	return wifi.ssid();
});

MqttLambdaSource< std::string> systemBuild("system/build",[]() {
	return __DATE__ " " __TIME__ ;
});
//______________________________________________________________________
//
#include <Neo6m.h>
Connector uext2(2);
Neo6m gps(&uext2);

#include <UltraSonic.h>
Connector uext1(1);
UltraSonic ultrasonic(&uext1);

TimerSource timer1(100,1);
//______________________________________________________________________
//

class Publisher : public Coroutine, public Source<MqttMessage> {
		std::string _systemPrefix;

	public:
		LastValueSink<bool> run;
		Publisher() : Coroutine("Publisher") {};
		void setup() {
			string_format(_systemPrefix, "src/%s/system/",Sys::hostname());
		}
		void loop() {
			std::string s;

			PT_BEGIN();
			while (true) {
				if ( run.value() ) {
					systemUptime.txd();
					systemHeap.txd();
					systemHostname.txd();
					rssiSource.txd();
					wifiIpAddress.txd();
					wifiSsid.txd();
					systemBuild.txd();
				}
				timeout(1000);
				PT_YIELD_UNTIL(timeout());
			}
			PT_END();
		}
};
//______________________________________________________________________
//
Publisher publisher;

extern "C" void app_main(void) {
	Coroutine::setupAll();
	wifi.connected >> mqtt.wifiConnected;
	mqtt.connected >> led;
	mqtt.connected >> publisher.run;
	publisher >> mqtt;
	systemUptime >> mqtt;
	systemHeap >> mqtt;
	systemHostname >> mqtt;
	rssiSource >> mqtt;
	wifiSsid >> mqtt;
	wifiIpAddress >> mqtt;
	systemBuild >> mqtt;
	gps >> mqtt;
	ultrasonic.distance >> new ToMqtt<int32_t>("us/distance") >> mqtt;
//	tacho >> new ToMqtt<double>("tacho/rpm") >> mqtt;
//	mqtt >> new FromMqtt<double>("motor/speed") >> motor.speed;

	while(true) {
		Coroutine::loopAll();
		vTaskDelay(1);
	}
}
