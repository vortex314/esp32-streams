#include "UltraSonic.h"

UltraSonic::UltraSonic(Connector* connector):Coroutine("ultrasonic"),distance(&_distance) {
	_connector =connector;
	_hcsr = new HCSR04(*_connector);
	_distance=0;
	_delay=0;
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
			_distance = _distance + (cm - _distance) / 2;
			_delay = _delay + (_hcsr->getTime() - _delay) / 2;
		}
		_hcsr->trigger();
		distance.pub();
	}
	PT_END();
}
