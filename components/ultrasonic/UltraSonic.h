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
		int32_t _distance;
		int32_t _delay;

	public:
		ReferenceSource<int32_t> distance;
		UltraSonic(Connector*);
		virtual ~UltraSonic();
		void setup();
		void loop();
};

#endif // ULTRASONIC_H
