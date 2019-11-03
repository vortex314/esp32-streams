#ifndef MOTORSERVO_H
#define MOTORSERVO_H

#include <Hardware.h>
#include <Streams.h>
#include <coroutine.h>
#include <MedianFilter.h>
#include <BTS7960.h>

#include "driver/mcpwm.h"
#include "soc/mcpwm_reg.h"
#include "soc/mcpwm_struct.h"

#define SERVO_MAX_SAMPLES 16

class MotorServo : public Coroutine
{

// D34 : L_IS
// D35 : R_IS
// D25 : ENABLE
// D26 : L_PWM
// D27 : R_PWM
// D32 : ADC POT
    BTS7960 _bts7960;
    ADC& _adcPot;
    MedianFilter<int,11> _potFilter;

    mcpwm_unit_t _mcpwm_num;
    mcpwm_timer_t _timer_num;

    float _bias=0;
    float _error=0;
    float _errorPrior=0;
    float _iteration_time=1;
//   float _angleSamples[SERVO_MAX_SAMPLES];
    uint32_t _indexSample=0;
    int _watchdogCounter;
    int _directionTargetLast;
    Timer _pulseTimer;
    Timer _reportTimer;
public:
    ValueFlow<int> angleTarget=0;
    ValueFlow<int> angleMeasured=0;
    ValueSource<float> KP=3,KI=0.005,KD=0.1,output=0.0,error=0.0,rpmFiltered=0.0;
    ValueSource<float> proportional=0.0,integral=0.0,derivative=0.0;
    ValueSource<float> current=0.0;
    ValueSink<bool> keepGoing=true;
    MotorServo(Connector* connector);
    MotorServo(uint32_t pinPot, uint32_t pinIS,
               uint32_t pinLeftEnable, uint32_t pinRightEnable,
               uint32_t pinLeftPwm, uint32_t pinRightPwm);
    ~MotorServo();
    void setup();
    void loop();

    void calcTarget(float);
    float PID(float error, float interval);
    float filterAngle(float inp);
    void round(float& f,float resol);
    bool measureAngle();
    float scale(float x,float x1,float x2,float y1,float y2);
};


#endif // MOTORSERVO_H
