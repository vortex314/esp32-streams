#include <../Common/Config.h>
#include <Mqtt.h>
#include <Wifi.h>
#include <sys/types.h>
#include <unistd.h>

// volatile MQTTAsync_token deliveredtoken;

//#define BZERO(x) ::memset(&x, 0, sizeof(x))

#ifndef MQTT_HOST
#error "check MQTT_HOST definition "
#endif
#ifndef MQTT_PORT
#error "check MQTT_PORT definition"
#endif
#define STRINGIFY(X) #X
#define S(X) STRINGIFY(X)

#define TIMER_KEEP_ALIVE 1
#define TIMER_2 2
//________________________________________________________________________
//
Mqtt::Mqtt()
	:incoming(20)
	, outgoing(20)
	, _reportTimer(1000, true, true)
	, keepAliveTimer(TIMER_KEEP_ALIVE,1000,true)

{
	_lwt_message = "false";
}
//________________________________________________________________________
//
Mqtt::~Mqtt() {}
//________________________________________________________________________
//
void Mqtt::init() {
	string_format(_address, "mqtt://%s:%d", S(MQTT_HOST), MQTT_PORT);
	string_format(_lwt_topic, "system/alive", Sys::hostname());
	string_format(_hostPrefix, "src/%s/", Sys::hostname());
	_clientId = Sys::hostname();
	//	esp_log_level_set("*", ESP_LOG_VERBOSE);
	esp_mqtt_client_config_t mqtt_cfg;
	BZERO(mqtt_cfg);
	INFO(" uri : %s ", _address.c_str());

	mqtt_cfg.uri = _address.c_str();
	mqtt_cfg.event_handle = Mqtt::mqtt_event_handler;
	mqtt_cfg.client_id = Sys::hostname();
	mqtt_cfg.user_context = this;
	mqtt_cfg.buffer_size = 10240;
	mqtt_cfg.lwt_topic = _lwt_topic.c_str();
	mqtt_cfg.lwt_msg = _lwt_message.c_str();
	mqtt_cfg.lwt_qos = 1;
	mqtt_cfg.lwt_msg_len = 5;
	mqtt_cfg.keepalive = 5;
	_mqttClient = esp_mqtt_client_init(&mqtt_cfg);

	_reportTimer.start();

	wifiConnected.handler([=](bool conn) {
		if(conn) {
			esp_mqtt_client_start(_mqttClient);
		} else {
			if(connected()) esp_mqtt_client_stop(_mqttClient);
		}
	});
	outgoing >> *this;
	*this >> incoming;
	keepAliveTimer >> (Sink<TimerMsg>&)(*this);
}
//________________________________________________________________________
//
void Mqtt::request() {
	/*	if ( connected()) {
			incoming.request();
			outgoing.request();
			keepAliveTimer.request();
		}*/
}

void Mqtt::subscribeOn(Thread& t) {
	t.addTimer(&keepAliveTimer);
	outgoing.subscribeOn(t);
}
//________________________________________________________________________
//

void Mqtt::onNext(MqttMessage m) {
	if(connected()) {
		std::string topic = _hostPrefix;
		topic += m.topic;
		mqttPublish(topic.c_str(), m.message.c_str());
	};
}
//________________________________________________________________________
//
void Mqtt::onNext(TimerMsg tm) {
	if(tm.id == TIMER_KEEP_ALIVE && connected() ) {
		onNext({_lwt_topic.c_str(), "true"});
	}
}
//________________________________________________________________________
//
int Mqtt::mqtt_event_handler(esp_mqtt_event_t* event) {
	Mqtt& me = *(Mqtt*)event->user_context;
	std::string topics;
	esp_mqtt_client_handle_t client = event->client;
	int msg_id;

	switch(event->event_id) {
		case MQTT_EVENT_BEFORE_CONNECT: {
				INFO("MQTT_EVENT_BEFORE_CONNECT");
				break;
			}
		case MQTT_EVENT_CONNECTED: {
				INFO("MQTT_EVENT_CONNECTED to %s", me._address.c_str());
				INFO(" session : %d %d ", event->session_present, event->msg_id);
				msg_id = esp_mqtt_client_publish(me._mqttClient, "src/limero/systems", Sys::hostname(), 0, 1, 0);
				topics = "dst/";
				topics += Sys::hostname();
				me.mqttSubscribe(topics.c_str());
				topics += "/#";
				me.mqttSubscribe(topics.c_str());
				me.connected=true;
				break;
			}
		case MQTT_EVENT_DISCONNECTED: {
				INFO("MQTT_EVENT_DISCONNECTED");
				me.connected=false;
				break;
			}
		case MQTT_EVENT_SUBSCRIBED:
			INFO("MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_UNSUBSCRIBED:
			INFO("MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_PUBLISHED:
			//			INFO("MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_DATA: {
				bool busy = false;
				if(!busy) {
					busy = true;
					static std::string topic;
					static std::string data;
					bool ready = true;
					if(event->data_len != event->total_data_len) {
						if(event->current_data_offset == 0) {
							topic = std::string(event->topic, event->topic_len);
							data = std::string(event->data, event->data_len);
							ready = false;
						} else {
							data.append(event->data, event->data_len);
							if(data.length() != event->total_data_len) {
								ready = false;
							}
						}
					} else {
						topic = std::string(event->topic, event->topic_len);
						data = std::string(event->data, event->data_len);
						topic = topic.substr(me._hostPrefix.length());
					}
					if(ready) {
						INFO("MQTT RXD %s=%s", topic.c_str(), data.c_str());
						me.incoming.emit({topic, data});
					}
					busy = false;
				} else {
					WARN(" sorry ! MQTT reception busy ");
				}
				break;
			}
		case MQTT_EVENT_ERROR:
			WARN("MQTT_EVENT_ERROR");
			break;
	}
	return ESP_OK;
}
//________________________________________________________________________
//
typedef enum { PING = 0, PUBLISH, PUBACK, SUBSCRIBE, SUBACK } CMD;
//________________________________________________________________________
//
void Mqtt::mqttPublish(const char* topic, const char* message) {
	if(connected() == false) return;
//	INFO("PUB : %s = %s", topic, message);
	int id = esp_mqtt_client_publish(_mqttClient, topic, message, 0, 0, 0);
	if(id < 0) WARN("esp_mqtt_client_publish() failed.");
}
//________________________________________________________________________
//
void Mqtt::mqttSubscribe(const char* topic) {
	if(connected() == false) return;
	INFO("Subscribing to topic %s ", topic);
	int id = esp_mqtt_client_subscribe(_mqttClient, topic, 0);
	if(id < 0) WARN("esp_mqtt_client_subscribe() failed.");
}
