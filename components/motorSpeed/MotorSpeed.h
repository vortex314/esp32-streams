#ifndef MOTORSPEED_H
#define MOTORSPEED_H

#include <Hardware.h>
#include <Streams.h>
#include <coroutine.h>
#include <BTS7960.h>
#include <Device.h>
#include <MedianFilter.h>

#include "driver/mcpwm.h"
#include "driver/pcnt.h"
#include "soc/mcpwm_reg.h"
#include "soc/mcpwm_struct.h"
#include "soc/rtc.h"



typedef enum { SIG_CAPTURED = 2 } Signal;

class MotorSpeed : public Device {

		BTS7960 _bts7960;

		float _errorPrior = 0;
		TimerSource _pulseTimer;
		TimerSource _reportTimer;
		TimerSource _controlTimer;

	public:
		ValueFlow<int> rpmTarget=40;
		ConfigFlow<float> KP= {"motor/KP",0.05};
		ConfigFlow<float> KI= {"motor/KI",0.2};
		ConfigFlow<float> KD= {"motor/KD",0};
		ValueFlow <float> pwm=0.0;
		ValueFlow<float> error=0.0;
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
