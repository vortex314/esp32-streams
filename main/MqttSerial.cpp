#include <MqttSerial.h>

#define TIMER_KEEP_ALIVE 1
#define TIMER_CONNECT 2
#define TIMER_SERIAL 3

MqttSerial::MqttSerial()
    : connected(false)
    , incoming(20)
    , outgoing(20)
    , keepAliveTimer(TIMER_KEEP_ALIVE, 1000, true)
    , connectTimer(TIMER_CONNECT, 3000, true)
    , serialTimer(TIMER_SERIAL, 10, true)
{
}
MqttSerial::~MqttSerial() {}

void MqttSerial::init()
{
    INFO("MqttSerial started. ");
    txd.clear();
    rxd.clear();
    _hostPrefix = "dst/";
    _hostPrefix += Sys::hostname();
    _hostPrefix += "/";
    _loopbackTopic = _hostPrefix + "system/loopback";
    _loopbackReceived = 0;
    outgoing >> *this;
    *this >> incoming;
    Sink<TimerMsg>& me = *this;
    keepAliveTimer >> me;
    connectTimer >> me;
    serialTimer >> me;
    connected.emitOnChange(true);
}

void MqttSerial::onNext(const TimerMsg& tm)
{
    // LOG(" timer : %lu ",tm.id);
    if(tm.id == TIMER_KEEP_ALIVE) {
	publish(_loopbackTopic, std::string("true"));
	outgoing.onNext({"system/alive", "true"});
    } else if(tm.id == TIMER_CONNECT) {
	if(Sys::millis() > (_loopbackReceived + 2000)) {
	    connected = false;
	    std::string topic;
	    string_format(topic, "dst/%s/#", Sys::hostname());
	    subscribe(topic);
	    publish(_loopbackTopic, "true");
	} else {
	    connected = true;
	}
    } else if(tm.id == TIMER_SERIAL) {
	/*	//   LOG("TIMER_SERIAL");
	        if(_stream.available()) {
	            String s = _stream.readString();
	            rxdSerial(s);
	        };*/
    } else {
	WARN("Invalid Timer Id");
    }
}

void MqttSerial::onNext(const MqttMessage& m)
{
    if(connected()) {
	std::string topic = _hostPrefix+m.topic;
	publish(topic, m.message);
    };
}

void MqttSerial::request()
{
    if(connected()) {
	incoming.request();
	outgoing.request();
    }
    keepAliveTimer.request();
    connectTimer.request();
    serialTimer.request();
}

void MqttSerial::rxdSerial(std::string& s)
{
    for(uint32_t i = 0; i < s.length(); i++) {
	char c = s[i];
	if((c == '\n' || c == '\r')) {
	    if(rxdString.length() > 0) {
		deserializeJson(rxd, rxdString);
		JsonArray array = rxd.as<JsonArray>();
		if(!array.isNull()) {
		    if(array[1].as<std::string>() == _loopbackTopic) {
			_loopbackReceived = Sys::millis();
		    } else {
			std::string topic = array[1];
			emit({topic.substr(_hostPrefix.length()), array[2]});
		    }
		}
		rxdString = "";
	    }
	} else {
	    if(rxdString.length() < 256) rxdString += c;
	}
    }
}

void MqttSerial::publish(std::string& topic, std::string message)
{
    txd.clear();
    txd.add((int)CMD_PUBLISH);
    txd.add(topic);
    txd.add(message);
    txdSerial(txd);
}

void MqttSerial::subscribe(std::string& topic)
{
    txd.clear();
    txd.add((int)CMD_SUBSCRIBE);
    txd.add(topic);
    txdSerial(txd);
}

void MqttSerial::txdSerial(JsonDocument& txd)
{
    std::string output = "";
    serializeJson(txd, output);
    printf("%s\n",output.c_str());
}
