#ifndef LEDBLINKER_H
#define LEDBLINKER_H

#include <coroutine.h>
#include <Streams.h>
#include "driver/gpio.h"

class LedBlinker : public TimerSource,public Sink<TimerMsg>
{
    uint32_t _pin;
    bool _on;

public:
    LambdaSink<bool> blinkSlow;
    LedBlinker(uint32_t pin, uint32_t delay);
    void init();
    void delay(uint32_t d);
    void onNext(const TimerMsg&);
};

#endif // LEDBLINKER_H
