#ifndef ULTRASONIC_H
#define ULTRASONIC_H

#include "HCSR04.h"
#include <Hardware.h>
#include <Log.h>
#include <Streams.h>
#include <Mqtt.h>

class UltraSonic : public TimerSource ,public Sink<TimerMsg> {
		Connector* _connector;
		HCSR04* _hcsr;

	public:
		ValueFlow<int32_t> distance=0;
		ValueFlow<int32_t> delay=0;
		UltraSonic(Connector*);
		virtual ~UltraSonic();
		void init();
		void onNext(const TimerMsg& );
};

#endif // ULTRASONIC_H
