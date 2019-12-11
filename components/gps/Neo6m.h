#ifndef NEO6M_H
#define NEO6M_H
#include <Hardware.h>
#include <Log.h>
#include <Streams.h>
#ifdef MQTT_SERIAL
#include <MqttSerial.h>
#else
#include <Mqtt.h>
#endif

class Neo6m : public Source<MqttMessage> {
		Connector* _connector;
		UART& _uart;
		static void onRxd(void*);
		std::string _line;
	public:
		Neo6m(Connector* connector);
		virtual ~Neo6m();
		void init();
		void handleRxd();
		void request();
};

#endif // NEO6M_H
