#include "LedBlinker.h"

#define PIN_LED 2

LedBlinker::LedBlinker(uint32_t pin, uint32_t delay) : ProtoThread("LedBlinker")
{
    _pin = pin;
    _delay = delay;
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
    handler([=](bool flag) {
        _delay = flag ? 500 : 100;
    });
}
void LedBlinker::loop()
{
    PT_BEGIN();
    while (true) {
        timeout(_delay);
        gpio_set_level((gpio_num_t)_pin, 1);
        PT_YIELD_UNTIL(timeout());
        timeout(_delay);
        gpio_set_level((gpio_num_t)_pin, 0);
        PT_YIELD_UNTIL(timeout());
    }
    PT_END();
}
void LedBlinker::delay(uint32_t d)
{
    _delay = d;
}
