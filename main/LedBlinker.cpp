#include "LedBlinker.h"

#define PIN_LED 2

LedBlinker::LedBlinker(uint32_t pin, uint32_t delay) : blinkTimer(1,100,true)
{
    _pin = pin;
    blinkTimer.interval(delay);
}
void LedBlinker::setup()
{
    gpio_config_t io_conf;
    io_conf.intr_type = (gpio_int_type_t)GPIO_PIN_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = 1<<PIN_LED;
    io_conf.pull_down_en = (gpio_pulldown_t)1;
    io_conf.pull_up_en = (gpio_pullup_t)1;
    gpio_config(&io_conf);
	blinkTimer >> *this;
    blinkSlow.handler([=](bool flag) {
		if ( flag ) blinkTimer.interval(500);
		else blinkTimer.interval(100);
    });
}
void LedBlinker::onNext(TimerMsg m)
{
	gpio_set_level((gpio_num_t)_pin, _on ? 1 : 0 );
	_on = ! _on;
}
void LedBlinker::delay(uint32_t d)
{
    blinkTimer.interval(d);
}
