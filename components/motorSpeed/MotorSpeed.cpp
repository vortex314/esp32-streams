#include "MotorSpeed.h"

#define CONTROL_INTERVAL_MS 100


MotorSpeed::MotorSpeed(uint32_t pinLeftIS, uint32_t pinRightIS,
                       uint32_t pinLeftEnable, uint32_t pinRightEnable,
                       uint32_t pinLeftPwm, uint32_t pinRightPwm)
	: _bts7960(pinLeftIS, pinRightIS, pinLeftEnable, pinRightEnable, pinLeftPwm,
	           pinRightPwm),
	  _pulseTimer(1,5000,true), // change steps each 5 sec
	  _reportTimer(2,1000,true), // to MQTT and display 1/sec
	  _controlTimer(3,CONTROL_INTERVAL_MS,true) // PID per 100 msec

{
	//_rpmMeasuredFilter = new AverageFilter<float>();
	deviceState=INIT;
	rpmTarget = 0;
	_bts7960.setPwmUnit(0);
	integral.emitOnChange(false);
	derivative.emitOnChange(false);
	proportional.emitOnChange(false);
	pwm.emitOnChange(false);

	_reportTimer >> *new LambdaSink<TimerMsg>([&](TimerMsg tm)
	{
		integral.request();
		derivative.request();
		proportional.request();
		current = _bts7960.measureCurrentLeft()+ _bts7960.measureCurrentRight();
		INFO("rpm %d/%d = %.2f => pwm : %.2f = %.2f + %.2f + %.2f ",  rpmMeasured(),rpmTarget(),error(),
		     pwm(),
		     KP() * error(),
		     KI() * integral(),
		     KD() * derivative());

		if ( (pwm() > 20 || pwm() < -20)  && rpmMeasured()==0 )
			{
				deviceState=STANDBY;
				deviceMessage="STANDBY : abs(pwm) > 20 and no rotation detected ? Wiring ? Stalled ? "
			}
	});

	_pulseTimer >> *new LambdaSink<TimerMsg>([&](TimerMsg tm)
	{
		INFO("next pulse");
		pulse();
	});

	auto pidCalc = new LambdaSink<int>([&](int rpm)
	{
		if ( state()==ON )
			{
				static float newOutput;
				error = rpmTarget() - rpm;
				newOutput = PID(error(), CONTROL_INTERVAL_MS/1000.0);
				if (rpmTarget() == 0)
					{
						newOutput = 0;
						integral=0;
					}
				pwm = newOutput;
				_bts7960.setOutput(pwm());
			}
		else
			{
				_bts7960.setOutput(0);
			}
	});

	rpmMeasured >> *pidCalc ;
	rpmMeasured.emitOnChange(false);
	_controlTimer >> *new LambdaSink<TimerMsg>([&](TimerMsg tick)
	{
		rpmMeasured.request();
	});

	rpmTarget >> *new LambdaSink<int>([](int v)
	{
		INFO(" rpmTarget : %d ",v);
	});
	deviceState=ON;

}

MotorSpeed::MotorSpeed(Connector* uext)
	: MotorSpeed(uext->toPin(LP_RXD), uext->toPin(LP_MISO),
	             uext->toPin(LP_MOSI), uext->toPin(LP_CS), uext->toPin(LP_TXD),
	             uext->toPin(LP_SCK)) {}

MotorSpeed::~MotorSpeed() {}

void MotorSpeed::init()
{
	if (_bts7960.initialize())
		{
			WARN(" initialize motor failed ");
			deviceState=STANDBY;
			deviceMessage="STANDBY : BTS7960 init failed ";
		}
}

void MotorSpeed::observeOn(Thread& t)
{
	_reportTimer.observeOn(t);
//    _pulseTimer.observeOn(t);
	_controlTimer.observeOn(t);
}



void MotorSpeed::pulse()
{

	static uint32_t pulse = 0;
	static int rpmTargets[] = {0,  60, 120,  180, 120, 60, 0,  -60, -120,  -180, -120, -60};

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
	float output = KP() * err + KI()*integral() + KD() * derivative() ;
	_errorPrior = err;
	return output;
}
