#ifndef NEO6M_H
#define NEO6M_H
#include <Hardware.h>
#include <Log.h>
#include <Streams.h>
#include <coroutine.h>
#include <Mqtt.h>

class Neo6m : public Coroutine,public Source<MqttMessage> {
		Connector* _connector;
		UART& _uart;
		static void onRxd(void*);
		std::string _line;
	public:
		Neo6m(Connector* connector);
		virtual ~Neo6m();
		void setup();
		void loop();
		void handleRxd();
		void request();
};

#endif // NEO6M_H
