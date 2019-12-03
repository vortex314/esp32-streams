#ifndef MOTORSPEED_H
#define MOTORSPEED_H

#include <Hardware.h>
#include <Streams.h>
#include <coroutine.h>
#include <BTS7960.h>

#include <MedianFilter.h>

#include "driver/mcpwm.h"
#include "driver/pcnt.h"
#include "soc/mcpwm_reg.h"
#include "soc/mcpwm_struct.h"
#include "soc/rtc.h"



typedef enum { SIG_CAPTURED = 2 } Signal;

class MotorSpeed
{

    BTS7960 _bts7960;

    int _directionTargetLast;
    float _rpmFiltered;

    float bias = 0;
    float _errorPrior = 0;
    float _sample_time = 1;
//   float _angleFiltered;
    // AverageFilter<float>* _rpmMeasuredFilter;
    int _watchdogCounter;
    float _currentLeft ;
    float _currentRight;
//	    int _pwmSign = 1;
    TimerSource _pulseTimer;
    TimerSource _reportTimer;
    TimerSource _controlTimer;
    int _rpmMeasured;


public:
    ValueFlow<int> rpmTarget=40;
    ValueFlow<float> KP=0.07,KI=0.0005,KD=0.0,output=0.0,error=0.0,rpmFiltered=0.0;
    ValueFlow<float> proportional=0.0,integral=0.0,derivative=0.0;
    ValueFlow<float> current=0.0;
    ValueFlow<int> rpmMeasured;
    ValueFlow<bool> keepGoing=true;

    MotorSpeed(Connector* connector);
    MotorSpeed(uint32_t pinLeftIS, uint32_t pinrightIS, uint32_t pinLeftEnable,
               uint32_t pinRightEnable, uint32_t pinLeftPwm,
               uint32_t pinRightPwm);
    ~MotorSpeed();
    void observeOn(Thread& t);

    void calcTarget(float);
    float PID(float error, float interval);


    void round(float& f, float resol);
    void init();
    void loop();
    void pulse();

    int32_t deltaToRpm(uint32_t delta, int direction);

    Erc selfTest(uint32_t level,std::string& message);
    Erc initialize();
    Erc hold();
    Erc run();
};

#endif // MOTORSPEED_H
