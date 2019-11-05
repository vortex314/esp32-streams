#include "UltraSonic.h"

UltraSonic::UltraSonic(Connector* connector):Coroutine("ultrasonic") {
	_connector =connector;
	_hcsr = new HCSR04(*_connector);
	distance=0;
	delay=0;
}

UltraSonic::~UltraSonic() {
	delete _hcsr;
}

void UltraSonic::setup() {
	_hcsr->init();
}

void UltraSonic::loop() {
	PT_BEGIN();
	while(true) {
		timeout(100);
		PT_YIELD_UNTIL(timeout());
		int cm = _hcsr->getCentimeters();
		if(cm < 400 && cm > 0) {
			distance = distance() + (cm - distance()) / 2;
			delay = delay() + (_hcsr->getTime() - delay()) / 2;
		}
		_hcsr->trigger();
	}
	PT_END();
}
