#ifndef ULTRASONIC_H
#define ULTRASONIC_H

#include "HCSR04.h"
#include <Hardware.h>
#include <Log.h>
#include <Streams.h>
#include <coroutine.h>
#include <Mqtt.h>

class UltraSonic : public Coroutine {
		Connector* _connector;
		HCSR04* _hcsr;

public:
	ValueFlow<int32_t> distance=0;
	ValueFlow<int32_t> delay=0;
		UltraSonic(Connector*);
		virtual ~UltraSonic();
		void setup();
		void loop();
};

#endif // ULTRASONIC_H
