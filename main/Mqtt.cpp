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


Mqtt::Mqtt():Coroutine("mqtt"),BufferedSink<MqttMessage>(20),_reportTimer(1000,true,true)

{
    _lwt_message="false";
}

Mqtt::~Mqtt()
{
}

void Mqtt::setup()
{
    string_format(_address,"mqtt://%s:%d",S(MQTT_HOST),MQTT_PORT);
    string_format(_lwt_topic,"src/%s/system/alive",Sys::hostname());
    string_format(_hostPrefix,"src/%s/",Sys::hostname());
    _clientId=Sys::hostname();
//	esp_log_level_set("*", ESP_LOG_VERBOSE);
    esp_mqtt_client_config_t mqtt_cfg;
    BZERO(mqtt_cfg);
    INFO(" uri : %s ",_address.c_str());

    mqtt_cfg.uri = _address.c_str();
    mqtt_cfg.event_handle = Mqtt::mqtt_event_handler;
    mqtt_cfg.client_id = Sys::hostname();
    mqtt_cfg.user_context = this;
    mqtt_cfg.buffer_size = 10240;
    mqtt_cfg.lwt_topic = _lwt_topic.c_str();
    mqtt_cfg.lwt_msg = _lwt_message.c_str();
    mqtt_cfg.lwt_qos=1;
    mqtt_cfg.lwt_msg_len=5;
    mqtt_cfg.keepalive=5;
    _mqttClient = esp_mqtt_client_init(&mqtt_cfg);

    _reportTimer.start();

    wifiConnected.handler([=](bool connected) {
        if ( connected ) {
            esp_mqtt_client_start(_mqttClient);
        } else {
            if ( _connected )
                esp_mqtt_client_stop(_mqttClient);
        }
    });

}

void Mqtt::loop()
{
    PT_BEGIN();
    timeout(1000);
    while(true) {
        PT_YIELD_UNTIL(hasNext()||timeout()||_reportTimer.timeout());
        if ( hasNext() ) {
            MqttMessage m;
            getNext(m);
            if ( _connected ) {
                std::string topic=_hostPrefix;
                topic+= m.topic;
                mqttPublish(topic.c_str(),m.message.c_str());
            };
        };
        if ( timeout() ) {
            timeout(1000);
        }
        if ( _reportTimer.timeout() ) {
            mqttPublish(_lwt_topic.c_str(),"true");
            _reportTimer.start();
        }

    }
    PT_END();
}

int Mqtt::mqtt_event_handler(esp_mqtt_event_t* event)
{
    Mqtt& me = *(Mqtt*) event->user_context;
    std::string topics;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch (event->event_id) {
    case MQTT_EVENT_BEFORE_CONNECT: {

    }
    case MQTT_EVENT_CONNECTED: {
        me._connected = true;
        INFO("MQTT_EVENT_CONNECTED to %s",me._address.c_str());
        INFO(" session : %d %d ", event->session_present,event->msg_id);
        msg_id =
            esp_mqtt_client_publish(me._mqttClient, "src/limero/systems", Sys::hostname(), 0, 1, 0);
        topics = "dst/";
        topics += Sys::hostname();
        me.mqttSubscribe(topics.c_str());
        topics += "/#";
        me.mqttSubscribe(topics.c_str());
        me.connected.emit(true);
        break;
    }
    case MQTT_EVENT_DISCONNECTED: {
        me._connected = false;
        me.connected.emit(false);
        INFO("MQTT_EVENT_DISCONNECTED");
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
        if (!busy) {
            busy = true;
            static std::string topic;
            static std::string data;
            bool ready = true;
            if (event->data_len != event->total_data_len) {
                if (event->current_data_offset == 0) {
                    topic = std::string(event->topic, event->topic_len);
                    data = std::string(event->data, event->data_len);
                    ready = false;
                } else {
                    data.append(event->data, event->data_len);
                    if (data.length() != event->total_data_len) {
                        ready = false;
                    }
                }
            } else {
                topic = std::string(event->topic, event->topic_len);
                data = std::string(event->data, event->data_len);
                topic = topic.substr(me._hostPrefix.length());
            }
            if (ready) {
                INFO("MQTT RXD %s=%s",topic.c_str(),data.c_str());
                me.emit({topic,data});
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

typedef enum {
    PING=0,PUBLISH,PUBACK,SUBSCRIBE,SUBACK
} CMD;

void Mqtt::mqttPublish(const char* topic, const char* message)
{
    if (_connected == false) return;
    //INFO("PUB : %s = %s", topic, message);
    int id = esp_mqtt_client_publish(_mqttClient, topic, message, 0, 0, 0);
    if (id < 0)
        WARN("esp_mqtt_client_publish() failed.");
}

void Mqtt::mqttSubscribe(const char* topic)
{
    if (_connected == false) return;
    INFO("Subscribing to topic %s ", topic);
    int id = esp_mqtt_client_subscribe(_mqttClient, topic, 0);
    if (id < 0)
        WARN("esp_mqtt_client_subscribe() failed.");
}
