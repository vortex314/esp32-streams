#include "MotorSpeed.h"

#define CONTROL_INTERVAL_MS 10


MotorSpeed::MotorSpeed(uint32_t pinLeftIS, uint32_t pinRightIS,
                       uint32_t pinLeftEnable, uint32_t pinRightEnable,
                       uint32_t pinLeftPwm, uint32_t pinRightPwm)
    : _bts7960(pinLeftIS, pinRightIS, pinLeftEnable, pinRightEnable, pinLeftPwm,
               pinRightPwm),_pulseTimer(1,5000,true),_reportTimer(2,1000,true)

{
    //_rpmMeasuredFilter = new AverageFilter<float>();
    rpmTarget = 0;
    _watchdogCounter = 0;
    _directionTargetLast = 0;
    _bts7960.setPwmUnit(0);
    _reportTimer >> *new LambdaSink<TimerMsg>([&](TimerMsg tm) {
        KI.request();
        KD.request();
        KP.request();
        integral.request();
        derivative.request();
        proportional.request();
        output.request();
        rpmTarget.request();
        current = _bts7960.measureCurrentLeft()+ _bts7960.measureCurrentRight();
        INFO("rpm %d/%d = %.2f => pwm=%.2f = %.2f + %.2f + %.2f ",  _rpmMeasured,rpmTarget(),error(),
             output(),
             KP() * error(),
             KI() * integral(),
             KD() * derivative());
    });

    _pulseTimer >> *new LambdaSink<TimerMsg>([&](TimerMsg tm) {
        INFO("TIMER pulse");
        pulse();
    });

    rpmMeasured >> *new LambdaSink<int>([&](int rpm) {
        _rpmMeasured = rpm;
        static float newOutput;
        error = rpmTarget() - rpm;
        newOutput = PID(error(), CONTROL_INTERVAL_MS);
        if (rpmTarget() == 0) {
            newOutput = 0;
            integral=0;
        }
        output = newOutput;
        _bts7960.setOutput(output());
    });
}

MotorSpeed::MotorSpeed(Connector* uext)
    : MotorSpeed(uext->toPin(LP_RXD), uext->toPin(LP_MISO),
                 uext->toPin(LP_MOSI), uext->toPin(LP_CS), uext->toPin(LP_TXD),
                 uext->toPin(LP_SCK)) {}

MotorSpeed::~MotorSpeed() {}

void MotorSpeed::setup()
{
    if (_bts7960.initialize()) WARN(" initialize motor failed ");
}

void MotorSpeed::observeOn(Thread& t)
{
    t.addTimer(&_reportTimer);
    t.addTimer(&_pulseTimer);
    rpmMeasured.observeOn(t);
}



void MotorSpeed::pulse()
{

    static uint32_t pulse = 0;
    static int rpmTargets[] = {0,  10, 20,  30, 20, 10, 0,
                               -10, -20,  -30, -20,-10,0
                              };
    /*    static int rpmTargets[] = {0,  30, 50,  100, 150, 100, 80,
                                   40, 0,  -40, -80,-120,-80 -30
                                  };*/
    rpmTarget = rpmTargets[pulse];
    pulse++;
    pulse %= (sizeof(rpmTargets) / sizeof(int));

}


float MotorSpeed::PID(float err, float interval)
{
    integral = integral() + (err * interval);
    derivative = (err - _errorPrior) / interval;
    float integralPart = KI() * integral();
    if ( integralPart > 30 ) integral =30.0 / KI();
    if ( integralPart < -30.0 ) integral =-30.0 / KI();
    float output = KP() * err + integralPart + KD() * derivative() + bias;
    _errorPrior = err;
    return output;
}
