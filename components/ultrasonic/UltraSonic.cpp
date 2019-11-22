#include "UltraSonic.h"

UltraSonic::UltraSonic(Connector* connector)
    : TimerSource(1, 100, true)
{
    _connector = connector;
    _hcsr = new HCSR04(*_connector);
    distance = 0;
    delay = 0;
	*this >> *this;
}

UltraSonic::~UltraSonic() { delete _hcsr; }

void UltraSonic::init() { _hcsr->init(); }

void UltraSonic::onNext(const TimerMsg& tm)
{
    int cm = _hcsr->getCentimeters();
    if(cm < 400 && cm > 0) {
	distance = distance() + (cm - distance()) / 2;
	delay = delay() + (_hcsr->getTime() - delay()) / 2;
    }
    _hcsr->trigger();
}
