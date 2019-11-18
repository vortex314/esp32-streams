#ifndef ROTARYENCODER_H
#define ROTARYENCODER_H

#include <Hardware.h>
#include <Log.h>
#include <Streams.h>
#include <coroutine.h>
#include <Mqtt.h>
#include "driver/mcpwm.h"
#include "driver/pcnt.h"
#include "soc/mcpwm_reg.h"
#include "soc/mcpwm_struct.h"
#include "soc/rtc.h"

#define MAX_SAMPLES 5


class RotaryEncoder : public ValueFlow<int32_t>
{
    uint32_t _pinTachoA;
    DigitalIn& _dInTachoB;
    uint32_t _pinTachoB;

    uint32_t _capture;
    uint32_t _prevCapture;
    uint64_t _captureTime;
    uint64_t _prevCaptureTime;
    uint32_t _delta;
    uint32_t _deltaPrev;
    uint32_t _captureInterval;
    uint32_t _isrCounter;
    int _direction = 1;
    int _directionPrev = 1;
    int _directionSign = -1;
    mcpwm_unit_t _mcpwm_num;
    mcpwm_timer_t _timer_num;
    int32_t _samples[MAX_SAMPLES];
    uint32_t _indexSample = 0;
    MovingAverage<uint32_t> _movingAverage;
    ValueFlow<uint32_t> _rawCapture;
    TimeoutFlow<int32_t> _timeoutFlow;

public:
    AsyncFlow<uint32_t> captures;
    ValueFlow<int32_t> rpm=0;
    ValueFlow<int32_t> direction=0;

    RotaryEncoder(uint32_t pinTachoA, uint32_t pinTachoB);
    ~RotaryEncoder();
    void init();
    Erc initialize();
    static void onRaise(void*);
    static void isrHandler(void*);
    int32_t deltaToRpm(uint32_t delta, int32_t direction);

    int32_t calcRpm();
    void setPwmUnit(uint32_t);

    void request();
    void onNext(uint32_t);
    void observeOn(Thread& t);

};

#endif // ROTARYENCODER_H
