#ifndef CONTROLLER_H
#define CONTROLLER_H
#include <Hardware.h>
#include <Log.h>
#include <Streams.h>
#include <coroutine.h>
#include <Mqtt.h>
#include <MedianFilter.h>
#define MEDIAN_SAMPLES 7

//______________________________________________________________________________________
//

class LedLight : public Sink<bool>
{
    DigitalOut& _dOut;

public:
    LedLight(int pin)
        : _dOut(DigitalOut::create(pin))
    {
    }
    void init()
    {
        _dOut.init();
        _dOut.write(1);
    };
    void onNext(bool b)
    {
        _dOut.write(b ? 0 : 1);
    }
};
//______________________________________________________________________________________
//
class Pot : public Source<int>
{
    ADC& _adc;
    float _value;

public:
    Pot(PhysicalPin pin)
        : _adc(ADC::create(pin))
    {
    }
    ~Pot() {};
    void init()
    {
        _adc.init();
    };
    void request()
    {
        emit(_adc.getValue());
    }
};
//______________________________________________________________________________________
//
class Button : public Source<bool>
{
    DigitalIn& _dIn;

public:
    Button(int pin)
        : _dIn(DigitalIn::create(pin))
    {
    }
    void init()
    {
        _dIn.setMode(DigitalIn::DIN_PULL_UP);
        _dIn.init();
    };
    void request()
    {
        emit(_dIn.read() == 0);
    }
};
//______________________________________________________________________________________
//
#endif
