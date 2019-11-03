#ifndef LEDBLINKER_H
#define LEDBLINKER_H

#include <coroutine.h>
#include <Streams.h>
#include "driver/gpio.h"


class LedBlinker : public Coroutine,public HandlerSink<bool>
{
    uint32_t _pin, _delay;

public:
    LedBlinker(uint32_t pin, uint32_t delay);
    void setup();
    void loop();
    void delay(uint32_t d);
};

#endif // LEDBLINKER_H
