
#include <Config.h>
#include <Wifi.h>
#include <Mqtt.h>
#include <LedBlinker.h>
#include "freertos/task.h"



//______________________________________________________________________
//
template <class T>
class LambdaSource : public Source< T> {
		std::function<T()> _handler;

	public:
		LambdaSource(std::function<T()> handler)
			: _handler(handler) {};
		void request() { this->emit(_handler()); }
};

//______________________________________________________________________
//
template <class T>
class Wait : public Flow<T, T> {
		uint64_t _last;
		uint32_t _delay;

	public:
		Wait(uint32_t delay)
			: _delay(delay) {
		}
		void onNext(T value) {
			uint32_t delta = Sys::millis() - _last;
			if(delta > _delay) {
				this->emit(value);
			}
			_last = Sys::millis();
		}
};

//______________________________________________________________________
//
class Poller : public Coroutine {
		std::vector<Requestable*> _requestables;
		uint32_t _interval;
		uint32_t _idx=0;
	public:
		ValueFlow<bool> run=false;
		Poller(uint32_t interval):Coroutine("publisher"),_interval(interval) {};
		void setup() {timeout(_interval);};
		void loop() {
			PT_BEGIN();
			while(true) {
				PT_YIELD_UNTIL(timeout());
				_idx++;
				if ( _idx >= _requestables.size()) _idx=0;
				if ( _requestables.size() && run() ) {
					_requestables[_idx]->request();
				}
				timeout(_interval);
			}
			PT_END();
		}
		Poller& operator()(Requestable& rq) { _requestables.push_back(&rq); return *this;}
};

//______________________________________________________________________
//


/*
 * LambdaSource<T>(handler) >> mqtt.ToMqtt<T>("")
 */
#define PRO_CPU 0
#define APP_CPU 1

template <class T>
class Loggy : public Flow<T,T> {
		std::string _name;
	public:
		Loggy(const char* name) {_name=name;}
		void onNext(T t) {
			INFO(" Log %s ",_name.c_str());
			this->emit(t);
		}
		void request() {};
};
//______________________________________________________________________
//

#define PIN_LED 2

LedBlinker led(PIN_LED, 100);

CoroutinePool blockingPool, nonBlockingPool;

Wifi wifi;
Mqtt mqtt;
Log logger(1024);

#ifdef GPS
#include <Neo6m.h>
Connector uextGps(GPS);
Neo6m gps(&uextGps);
#endif

#ifdef US
#include <UltraSonic.h>
Connector uextUs(US);
UltraSonic ultrasonic(&uextUs);
#endif


#ifdef MOTOR
#include <RotaryEncoder.h>
#include <MotorSpeed.h>
#include <MotorServo.h>

Connector uextMotor(MOTOR);
RotaryEncoder tacho(uextMotor.toPin(LP_SCL), uextMotor.toPin(LP_SDA));
MotorSpeed motor(&uextMotor);
Connector uextServo(2);
MotorServo servo(&uextServo);
#endif

#ifdef REMOTE
#include <HwStream.h>
#include <MedianFilter.h>

Pot potLeft(36);
Pot potRight(39);
Button buttonLeft(13);
Button buttonRight(16);
LedLight ledLeft(32);
LedLight ledRight(23);
#endif


ValueFlow<std::string> systemBuild  ;
ValueFlow<std::string> systemHostname;
ValueFlow<bool> systemAlive=true;
LambdaSource<uint32_t> systemHeap([]() { return xPortGetFreeHeapSize(); });
LambdaSource<uint64_t> systemUptime([]( ) { return Sys::millis(); });

Poller slowPoller(100);
Poller fastPoller(10);
Poller ticker(10);

extern "C" void app_main(void) {
	Sys::hostname(S(HOSTNAME));
	systemHostname = S(HOSTNAME);
	systemBuild = __DATE__ " " __TIME__;
	systemAlive=true;

	blockingPool.add(wifi);
	blockingPool.add(mqtt);
	ticker.run = true;

	wifi.connected.emitOnChange(true);
	mqtt.connected.emitOnChange(true);

	wifi.connected >> mqtt.wifiConnected;

	mqtt.connected >> led.blinkSlow;
	mqtt.connected >> slowPoller.run;
	mqtt.connected >> fastPoller.run;

	systemAlive >> mqtt.toTopic<bool>("system/alive");
	systemHeap >> mqtt.toTopic<uint32_t>("system/heap");
	systemUptime >> mqtt.toTopic<uint64_t>("system/upTime");
	systemBuild >> mqtt.toTopic<std::string>("system/build");
	systemHostname >> mqtt.toTopic<std::string>("system/hostname");
	wifi.ipAddress >> mqtt.toTopic<std::string>("wifi/ipAddress");
	wifi.rssi >> mqtt.toTopic<int>("wifi/rssi");
	wifi.ssid >> mqtt.toTopic<std::string>("wifi/ssid");

	slowPoller(systemAlive)(systemHeap)(systemUptime)(systemBuild)(systemHostname)(wifi.ipAddress)(wifi.rssi)(wifi.ssid);
	led.setup();
	ticker( led.blinkTimer);
#ifdef GPS
	gps >> mqtt;
	nonBlockingPool.add(gps);
#endif

#ifdef US
	ultrasonic.distance >> mqtt.toTopic<int32_t>("us/distance");
	slowPoller(ultrasonic.distance);
	nonBlockingPool.add(ultrasonic);
#endif

#ifdef REMOTE
	potLeft.init();
	potRight.init();
	buttonLeft.init();
	buttonRight.init();
	ledLeft.init();
	ledRight.init();

	potLeft >> *new Median<int, 11>() >> mqtt.toTopic<int>("remote/potLeft");
	potRight >> *new Median<int, 11>() >> mqtt.toTopic<int>("remote/potRight");
	buttonLeft >> mqtt.toTopic<bool>("remote/buttonLeft");
	buttonRight >> mqtt.toTopic<bool>("remote/buttonRight");
	mqtt.fromTopic<bool>("remote/ledLeft") >> ledLeft;
	mqtt.fromTopic<bool>("remote/ledRight") >> ledRight;
	fastPoller(potLeft)(potRight)(buttonLeft)(buttonRight);
#endif

#ifdef MOTOR
	tacho >> mqtt.toTopic<int32_t>("motor/rpmMeasured");
	tacho >> motor.rpmMeasured;

	motor.output >> mqtt.toTopic<float>("motor/pwm");
	motor.integral >> mqtt.toTopic<float>("motor/I");
	motor.derivative >> mqtt.toTopic<float>("motor/D");
	motor.proportional >> mqtt.toTopic<float>("motor/P");
	motor.rpmTarget >> mqtt.toTopic<int>("motor/rpmTarget");
	mqtt.fromTopic<int>("motor/rpmTarget") >> motor.rpmTarget;

	servo.output >> *new Wait<float>(1000) >> mqtt.toTopic<float>("servo/pwm");
	servo.integral >> mqtt.toTopic<float>("servo/I");
	servo.derivative >> mqtt.toTopic<float>("servo/D");
	servo.proportional >> mqtt.toTopic<float>("servo/P");
	servo.angleTarget >> mqtt.toTopic<int>("servo/angleTarget");
	servo.angleMeasured >> mqtt.toTopic<int>("servo/angleMeasured");
	mqtt.fromTopic<int>("servo/angleTarget") >> servo.angleTarget;

	//    nonBlockingPool.add(tacho);
	//    nonBlockingPool.add(motor);
	nonBlockingPool.add(servo);
#endif

	nonBlockingPool.add(ticker);
	nonBlockingPool.add(slowPoller);
	nonBlockingPool.add(fastPoller);

	blockingPool.setupAll();
	nonBlockingPool.setupAll();

	xTaskCreatePinnedToCore([](void*) {
		while(true) {
			nonBlockingPool.loopAll();
			vTaskDelay(1);
		}
	}, "nonBlocking", 20000, NULL, 16, NULL, APP_CPU);

	/*	xTaskCreatePinnedToCore([](void*) {
			while(true) {
				blockingPool.loopAll();
				vTaskDelay(1);
			}
		}, "blocking", 20000,NULL, 15, NULL, PRO_CPU);*/
	blockingPool.loopAll();

}
