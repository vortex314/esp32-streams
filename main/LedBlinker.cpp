#include "LedBlinker.h"


LedBlinker::LedBlinker(uint32_t pin, uint32_t delay) : TimerSource(1,delay,true)
{
    _pin = pin;
    blinkSlow.handler([=](bool flag) {
        if ( flag ) interval(500);
        else interval(100);
    });
    *this >> *this; // consume my own TimerMsg
}
void LedBlinker::init()
{
    gpio_config_t io_conf;
    io_conf.intr_type = (gpio_int_type_t)GPIO_PIN_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = 1<<_pin;
    io_conf.pull_down_en = (gpio_pulldown_t)1;
    io_conf.pull_up_en = (gpio_pullup_t)1;
    gpio_config(&io_conf);
}
void LedBlinker::onNext(const TimerMsg& m)
{
    gpio_set_level((gpio_num_t)_pin, _on ? 1 : 0 );
//	INFO("let's blink");
    _on = ! _on;
}
void LedBlinker::delay(uint32_t d)
{
    interval(d);
}


