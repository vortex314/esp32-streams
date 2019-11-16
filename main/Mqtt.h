#ifndef _MQTT_H_
#define _MQTT_H_
extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "esp_event.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mqtt_client.h"
}
#include <coroutine.h>
#include <Streams.h>
#include <ArduinoJson.h>

// #define ADDRESS "tcp://test.mosquitto.org:1883"
//#define CLIENTID "microAkka"
//#define TOPIC "dst/steer/system"
//#define PAYLOAD "[\"pclat/aliveChecker\",1234,23,\"hello\"]"
#define QOS 0
#define TIMEOUT 10000L
//____________________________________________________________________________________________________________
//
typedef struct MqttMessage {
	std::string topic;
	std::string message;
} MqttMessage;
//____________________________________________________________________________________________________________
//
template <class T>
class ToMqtt : public Flow<T, MqttMessage> {
		std::string _name;

	public:
		ToMqtt(std::string name) : _name(name) {};
		void onNext(T event) {
			std::string s;
			DynamicJsonDocument doc(100);
			JsonVariant variant = doc.to<JsonVariant>();
			variant.set(event);
			serializeJson(doc, s);
			this->emit({_name, s});
			// emit doesn't work as such
			// https://stackoverflow.com/questions/9941987/there-are-no-arguments-that-depend-on-a-template-parameter
		}
		void request() {};
};


//_______________________________________________________________________________________________________________
//
template <class T>
class FromMqtt : public Flow<MqttMessage, T> {
		std::string _name;

	public:
		FromMqtt(std::string name) : _name(name) {};
		void onNext(MqttMessage mqttMessage) {
			if (mqttMessage.topic != _name) {
				return;
			}
			DynamicJsonDocument doc(100);
			auto error = deserializeJson(doc, mqttMessage.message);
			if (error) {
				WARN(" failed JSON parsing '%s' : '%s' ", mqttMessage.message.c_str(), error.c_str());
				return;
			}
			JsonVariant variant = doc.as<JsonVariant>();
			if (variant.isNull()) {
				WARN(" is not a JSON variant '%s' ", mqttMessage.message.c_str());
				return;
			}
			if ( variant.is<T>() ==false ) {
				WARN(" message '%s' JSON type doesn't match.", mqttMessage.message.c_str());
				return;
			}
			T value = variant.as<T>();
			this->emit(value);
			// emit doesn't work as such
			// https://stackoverflow.com/questions/9941987/there-are-no-arguments-that-depend-on-a-template-parameter
		}
		void request() {};
};

class Mqtt : public Sink<TimerMsg>,public Flow<MqttMessage,MqttMessage> {

		StaticJsonDocument<3000> _jsonBuffer;
		std::string _clientId;
		std::string _address;
		esp_mqtt_client_handle_t _mqttClient;
		std::string _lwt_topic;
		std::string _lwt_message;
		Timer _reportTimer;
		std::string _hostPrefix;


	public:
		AsyncFlow<MqttMessage> outgoing;
		AsyncFlow<MqttMessage> incoming;
		LambdaSink<bool> wifiConnected;
		ValueFlow<bool> connected;
		TimerSource keepAliveTimer;
		Mqtt();
		~Mqtt();
		void init();

		void mqttPublish(const char *topic, const char *message);
		void mqttSubscribe(const char *topic);
		void mqttConnect();
		void mqttDisconnect();

		bool handleMqttMessage(const char *message);
		static int mqtt_event_handler( esp_mqtt_event_t* event);

		void onNext(TimerMsg);
		void onNext(MqttMessage);
		void request();
		template <class T>
		Sink<T>& toTopic(const char* name) {
			return *(new ToMqtt<T>(name))  >> outgoing;
		}
		template <class T>
		Source<T>& fromTopic(const char* name) {
			auto newSource = new FromMqtt<T>(name);
			incoming >> *newSource;
			return *newSource;
		}
		void observeOn(Thread& thread );

};

//_______________________________________________________________________________________________________________
//


#endif
