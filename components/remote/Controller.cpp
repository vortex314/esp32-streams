#include "Controller.h"


Controller::Controller()  : Coroutine("remote"),
    _led_right(23),
    _led_left(32),
    _pot_left(36),
    _pot_right(39),
    _leftSwitch(DigitalIn::create(13)),
    _rightSwitch(DigitalIn::create(16)),
    _potLeftFilter(),_potRightFilter(),
    _reportTimer(1000,true,true),
    potLeft(&_potLeft),
    potRight(&_potRight)
{
}

Controller::~Controller()
{
}

void Controller::setup()
{
    _leftSwitch.setMode(DigitalIn::DIN_PULL_UP);
    _rightSwitch.setMode(DigitalIn::DIN_PULL_UP);

    _led_right.init();
    _led_right.off();

    _led_left.init();
    _led_left.off();

    _pot_left.init();
    _pot_right.init();

    _leftSwitch.init();
    _rightSwitch.init();

}
#include <cmath>
bool delta(float a,float b,float max)
{
    if ( abs(a-b)>max) return true;
    return false;
}

void Controller::savePrev()
{
    _potLeftPrev = _potLeft;
    _potRightPrev= _potRight;
    _buttonLeftPrev=_buttonLeft;
    _buttonRightPrev=_buttonRight;
}

void Controller::measure()
{

    _potLeftFilter.addSample(_pot_left.getValue());
    _potRightFilter.addSample(_pot_right.getValue());

    _potLeft = _potLeftFilter.getMedian();
    _potRight= _potRightFilter.getMedian();

    _buttonLeft = _leftSwitch.read()==1?false:true;
    _buttonRight = _rightSwitch.read()==1?false:true;
}

void Controller::loop()
{
    PT_BEGIN();
    while(true) {
        timeout(10);
        PT_YIELD_UNTIL(timeout() || _reportTimer.timeout());
        measure();
        if ( timeout()) {
            if ( delta(_potLeft, _potLeftPrev,2 ))	potLeft.pub();
            if ( delta(_potRight, _potRightPrev,2) ) potRight.pub();
            if ( _buttonLeftPrev != _buttonLeft) buttonLeft.emit(_buttonLeft);
            if ( _buttonRightPrev != _buttonRight ) buttonRight.emit(_buttonRight);
        } else if ( _reportTimer.timeout()) {
            potLeft.pub();
            potRight.pub();
            buttonLeft.emit(_buttonLeft);
            buttonRight.emit(_buttonRight);
            _reportTimer.start();
        }
        _led_left.on(!ledLeft.get());
        _led_right.on(!ledRight.get());
        savePrev();
    }
    PT_END();
}
