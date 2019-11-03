#include "MotorServo.h"

#define MAX_PWM 50
#define CONTROL_INTERVAL_MS 10
#define ANGLE_MIN -45.0
#define ANGLE_MAX 45.0
#define ADC_MIN 330
#define ADC_MAX 620
#define ADC_ZERO 460



MotorServo::MotorServo(uint32_t pinPot, uint32_t pinIS,
                       uint32_t pinLeftEnable, uint32_t pinRightEnable,
                       uint32_t pinLeftPwm, uint32_t pinRightPwm) :

    Coroutine("servo"),
    _bts7960(pinIS, pinIS, pinLeftEnable, pinRightEnable, pinLeftPwm,pinRightPwm),
    _adcPot(ADC::create(pinPot)),
    _pulseTimer(10000,true,true),
    _reportTimer(100,true,true)
{
    _bts7960.setPwmUnit(1);
}

MotorServo::MotorServo(Connector* uext) : MotorServo(
        uext->toPin(LP_RXD), //only ADC capable pins
        uext->toPin(LP_MISO), // "

        uext->toPin(LP_MOSI),
        uext->toPin(LP_CS),

        uext->toPin(LP_TXD),
        uext->toPin(LP_SCK))
{

}

MotorServo::~MotorServo()
{
}


void MotorServo::setup()
{

    Erc rc = _adcPot.init();
    if ( rc != E_OK ) WARN("Potentiometer initialization failed");
    if ( _bts7960.initialize() ) WARN("BTS7960 initialization failed");
    /*
        timers().startPeriodicTimer("controlTimer", Msg("controlTimer"), CONTROL_INTERVAL_MS);
        timers().startPeriodicTimer("reportTimer", Msg("reportTimer"), 100);
        timers().startPeriodicTimer("pulseTimer", Msg("pulseTimer"), 10000);
        timers().startPeriodicTimer("watchdogTimer", Msg("watchdogTimer"), 2000);*/
}

void MotorServo::loop()
{
    PT_BEGIN();
    timeout(CONTROL_INTERVAL_MS);

    while(true) {
        PT_YIELD_UNTIL(timeout() || _pulseTimer.timeout() || _reportTimer.timeout());
        if ( timeout() ) {
            if ( angleTarget.get()< ANGLE_MIN) angleTarget=ANGLE_MIN;
            if ( angleTarget.get()> ANGLE_MAX) angleTarget=ANGLE_MAX;
            if ( measureAngle()) {
                _error = angleTarget.get() - angleMeasured.get();
                output = PID(_error, CONTROL_INTERVAL_MS);
                _bts7960.setOutput(output.get());
            }
            timeout(CONTROL_INTERVAL_MS);
        }
        if ( _pulseTimer.timeout()) {
            static uint32_t pulse=0;
            static int outputTargets[]= {-30,0,30,0};
            angleTarget=outputTargets[pulse];
            pulse++;
            pulse %= (sizeof(outputTargets)/sizeof(int));
            _pulseTimer.start();
        }
        if ( _reportTimer.timeout()) {
            KI.pub();
            KD.pub();
            KP.pub();
            integral.pub();
            derivative.pub();
            proportional.pub();
            output.pub();
            angleTarget.pub();
            angleMeasured.pub();
            current = _bts7960.measureCurrentLeft()+ _bts7960.measureCurrentRight();
            current.pub();
            INFO("angle %d/%d = %f => pwm=%f = %f + %f + %f ",  angleMeasured.get(),angleTarget.get(),error.get(),
                 output.get(),
                 KP.get() * error.get(),
                 KI.get() * integral.get(),
                 KD.get() * derivative.get());
            _reportTimer.start();
        }

    }
    PT_END();
}

float MotorServo::scale(float x,float x1,float x2,float y1,float y2)
{
    if ( x < x1 ) x=x1;
    if ( x > x2 ) x=x2;
    float y= y1+(( x-x1)/(x2-x1))*(y2-y1);
    return y;
}

bool MotorServo::measureAngle()
{
    int adc = _adcPot.getValue();
    _potFilter.addSample(adc);
    if ( _potFilter.isReady()) {
        angleMeasured = scale(_potFilter.getMedian(),ADC_MIN,ADC_MAX,ANGLE_MIN,ANGLE_MAX);
        return true;
    }
    return false;
}


float MotorServo::PID(float err, float interval)
{
    integral = integral.get() + (err * interval);
    derivative = (err - _errorPrior) / interval;
    float integralPart = KI.get() * integral.get();
    if ( integralPart > 20 ) integral =20.0 / KI.get();
    if ( integralPart < -20.0 ) integral =-20.0 / KI.get();
    float output = KP.get() * err + integralPart + KD.get() * derivative.get() + _bias;
    _errorPrior = err;
    return output;
}

void MotorServo::round(float& f, float resolution)
{
    int i = f / resolution;
    f = i;
    f *= resolution;
}
