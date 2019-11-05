#include "MotorSpeed.h"

#define CONTROL_INTERVAL_MS 10


MotorSpeed::MotorSpeed(uint32_t pinLeftIS, uint32_t pinRightIS,
                       uint32_t pinLeftEnable, uint32_t pinRightEnable,
                       uint32_t pinLeftPwm, uint32_t pinRightPwm)
    : Coroutine("motor"),_bts7960(pinLeftIS, pinRightIS, pinLeftEnable, pinRightEnable, pinLeftPwm,
                                  pinRightPwm),_pulseTimer(5000,true,true),_reportTimer(100,true,true)

{
    //_rpmMeasuredFilter = new AverageFilter<float>();
    rpmTarget = 0;
    _watchdogCounter = 0;
    _directionTargetLast = 0;
    _bts7960.setPwmUnit(0);
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

void MotorSpeed::loop()
{
    PT_BEGIN();
    timeout(CONTROL_INTERVAL_MS);

    while(true) {
        PT_YIELD_UNTIL(timeout() || _pulseTimer.timeout() || _reportTimer.timeout());
        if ( timeout() ) {
            static float newOutput;
            error = rpmTarget() - rpmMeasured();
            newOutput = PID(error(), CONTROL_INTERVAL_MS);
            if (rpmTarget() == 0) {
                newOutput = 0;
                integral=0;
            }
            output = newOutput;
            _bts7960.setOutput(output());
            timeout(CONTROL_INTERVAL_MS);
        }
        if ( _pulseTimer.timeout()) {
            pulse();
            _pulseTimer.start();
        }
        if ( _reportTimer.timeout()) {
            KI.request();
            KD.request();
            KP.request();
            integral.request();
            derivative.request();
            proportional.request();
            output.request();
            rpmTarget.request();
            current = _bts7960.measureCurrentLeft()+ _bts7960.measureCurrentRight();
            INFO("rpm %d/%d = %f => pwm=%f = %f + %f + %f ",  rpmMeasured(),rpmTarget(),error(),
                 output(),
                 KP() * error(),
                 KI() * integral(),
                 KD() * derivative());
            _reportTimer.start();
        }

    }
    PT_END();
}

void MotorSpeed::pulse()
{

    static uint32_t pulse = 0;
    static int rpmTargets[] = {0,  30, 50,  100, 150, 100, 80,
                               40, 0,  -40, -80,-120,-80 -30
                              };
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
