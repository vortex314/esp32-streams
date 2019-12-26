
#include <Config.h>
#ifdef MQTT_SERIAL
#include <MqttSerial.h>
#else
#include <Wifi.h>
#include <Mqtt.h>
#endif

#include <LedBlinker.h>
#include "freertos/task.h"
#define STRINGIFY(X) #X
#define S(X) STRINGIFY(X)
template <class T>
class ChangeFlow : public Flow<T, T> {
		T _value;
		int _delta;
		bool _emitOnChange = true;

	public:
		ChangeFlow(int delta) {
			_delta=delta;
		}
		/*    ChangeFlow(T x)
		        : Flow<T, T>()
		        , _value(x) {};*/
		void request() {
			this->emit(_value);
		}
		void onNext(const T& value) {
			if(_emitOnChange && abs(value - _value)>_delta) {
				this->emit(value);
			}
			_value = value;
		}
		void emitOnChange(bool b) {
			_emitOnChange = b;
		};
		inline void operator=(T value) {
			onNext(value);
		};
		inline T operator()() {
			return _value;
		}
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
class Poller : public TimerSource, public Sink<TimerMsg> {
		std::vector<Requestable*> _requestables;
		uint32_t _idx = 0;

	public:
		ValueFlow<bool> run = false;
		Poller(uint32_t iv)
			: TimerSource(1, 1000, true) {
			interval(iv);
			*this >> *this;
		};

		void onNext(const TimerMsg& tm) {
			_idx++;
			if(_idx >= _requestables.size()) _idx = 0;
			if(_requestables.size() && run()) {
				_requestables[_idx]->request();
			}
		}

		Poller& operator()(Requestable& rq) {
			_requestables.push_back(&rq);
			return *this;
		}
};
// ___________________________________________________________________________
//

//____________________________________________________________________________
//

#define PRO_CPU 0
#define APP_CPU 1
template <class T1, class T2>
class Templ : Flow<T1, T1>, Flow<T2, T2> {
	public:
		void onNext(const T1& t1) {
			T2 t2;
			Flow<T2, T2>::emit(t2);
		}
		void onNext(const T2& t2) {
			T1 t1;
			Flow<T1, T1>::emit(t1);
		}
		void request() {}
};
Templ<float, MqttMessage> templ;
//______________________________________________________________________
//

#define PIN_LED 2

LedBlinker led(PIN_LED, 100);
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
Thread usThread;
#endif

#ifdef MOTOR

#include <RotaryEncoder.h>
#include <MotorSpeed.h>
Connector uextMotor(MOTOR);
#endif

#ifdef SERVO
#include <MotorServo.h>
Connector uextServo(2);

Thread servoThread;
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

ValueFlow<std::string> systemBuild;
ValueFlow<std::string> systemHostname;
LambdaSource<uint32_t> systemHeap([]() {
	return xPortGetFreeHeapSize();
});
LambdaSource<uint64_t> systemUptime([]() {
	return Sys::millis();
});

Thread mqttThread;
Thread thisThread;
Thread motorThread;

extern "C" void app_main(void) {
	//    ESP_ERROR_CHECK(nvs_flash_erase());

	Sys::hostname(S(HOSTNAME));
	systemHostname = S(HOSTNAME);
	systemBuild = __DATE__ " " __TIME__;
	Poller& slowPoller = *new Poller(100);


#ifdef MQTT_SERIAL
	MqttSerial& mqtt = *new MqttSerial();
#else
	Wifi& wifi = *new Wifi();
	Mqtt& mqtt = *new Mqtt();
	wifi.connected >> mqtt.wifiConnected;
	wifi.init();
	wifi.ipAddress >> mqtt.toTopic<std::string>("wifi/ipAddress");
	wifi.rssi >> mqtt.toTopic<int>("wifi/rssi");
	wifi.ssid >> mqtt.toTopic<std::string>("wifi/ssid");
	wifi.macAddress >> mqtt.toTopic<std::string>("wifi/mac");
	wifi.prefix >> mqtt.toTopic<std::string>("wifi/prefix");
	mqtt.fromTopic<std::string>("wifi/prefix") >> wifi.prefix;
	mqttThread | slowPoller(wifi.ipAddress)(wifi.rssi)(wifi.ssid)(wifi.macAddress)(wifi.prefix);
#endif

#ifndef HOSTNAME
	std::string hn;
	string_format(hn, "ESP32-%d", wifi.mac() & 0xFFFF);
	Sys::hostname(hn.c_str());
	systemHostname = hn;
#endif
	mqtt.init();
	led.init();
	mqtt.connected >> led.blinkSlow;
	mqtt.connected >> slowPoller.run;

	mqtt.observeOn(mqttThread);
	mqttThread | led;
	mqttThread | slowPoller;

	mqtt.observeOn(mqttThread);

	systemHeap >> mqtt.toTopic<uint32_t>("system/heap");
	systemUptime >> mqtt.toTopic<uint64_t>("system/upTime");
	systemBuild >> mqtt.toTopic<std::string>("system/build");
	systemHostname >> mqtt.toTopic<std::string>("system/hostname");
	ValueFlow<uint32_t> dummy=123;
	dummy == mqtt.topic<uint32_t>("system/dummy");

	mqttThread | slowPoller(systemHeap)(systemUptime)(systemBuild)(systemHostname)(dummy);

#ifdef GPS
	gps.init(); // no thread , driven from interrupt
	gps >> *new Throttle<MqttMessage>(1000) >> mqtt.outgoing.fromIsr;

#endif

#ifdef US
	ultrasonic.init();
	ultrasonic.interval(200);
	thisThread | ultrasonic;
	ultrasonic.distance >> mqtt.toTopic<int32_t>("us/distance");
#endif

#ifdef REMOTE
	Poller& fastPoller = *new Poller(100);
	potLeft.init();
	potRight.init();
	buttonLeft.init();
	buttonRight.init();
	ledLeft.init();
	ledRight.init();

	thisThread | potLeft.timer;
	thisThread | potRight.timer;
	potLeft >> *new Median<int, 5>() >> *new Throttle<int>(100) >> *new ChangeFlow<int>(3)  >> mqtt.toTopic<int>("remote/potLeft");             // timer driven
	potRight >> *new Median<int, 5>() >> *new Throttle<int>(100) >> *new ChangeFlow<int>(3) >> mqtt.toTopic<int>("remote/potRight");           // timer driven
	buttonLeft >> *new Throttle<bool>(100) >> mqtt.toTopic<bool>("remote/buttonLeft");   // ISR driven
	buttonRight >> *new Throttle<bool>(100) >> mqtt.toTopic<bool>("remote/buttonRight"); // ISR driven
	mqtt.topic<bool>("remote/ledLeft") >> ledLeft;
	mqtt.topic<bool>("remote/ledRight") >> ledRight;
	fastPoller(buttonLeft)(buttonRight);
	thisThread | fastPoller;
#endif

#ifdef MOTOR
	RotaryEncoder& rotaryEncoder = *new RotaryEncoder(uextMotor.toPin(LP_SCL), uextMotor.toPin(LP_SDA));
	MotorSpeed& motor = *new MotorSpeed(&uextMotor); // cannot init as global var because of NVS
	INFO(" init motor ");
	rotaryEncoder.init();
	rotaryEncoder.observeOn(motorThread);
	rotaryEncoder.rpmMeasured >> motor.rpmMeasured; // ISR driven !

	motor.init();
	motor.pwm >> *new Throttle<float>(100) >> mqtt.toTopic<float>("motor/pwm");
	rotaryEncoder.rpmMeasured >> *new Throttle<int>(100) >> mqtt.toTopic<int>("motor/rpmMeasured");

	motor.KI == mqtt.topic<float>("motor/KI");
	motor.KP == mqtt.topic<float>("motor/KP");
	motor.KD == mqtt.topic<float>("motor/KD");
	motor.current == mqtt.topic<float>("motor/current");
	motor.rpmTarget == mqtt.topic<int>("motor/rpmTarget");
	motor.running == mqtt.topic<bool>("motor/running");
	motor.deviceMessage >> mqtt.toTopic<std::string>("motor/message");
	slowPoller(motor.KI)(motor.KP)(motor.KD)(motor.rpmTarget)(motor.deviceMessage)(motor.running);

	motor.observeOn(motorThread);
	xTaskCreatePinnedToCore([](void*) {
		INFO("motorThread started.");
		motorThread.run();
	}, "motor", 20000, NULL, 17, NULL, APP_CPU);
#endif

#ifdef SERVO
	MotorServo& servo = *new MotorServo(&uextServo);
	servo.init();
	servo.pwm >> *new Throttle<float>(100) >> mqtt.toTopic<float>("servo/pwm");
	servo.angleMeasured >> *new Throttle<int>(100) >> mqtt.toTopic<int>("servo/angleMeasured");
	servo.KI == mqtt.topic<float>("servo/KI");
	servo.KP == mqtt.topic<float>("servo/KP");
	servo.KD == mqtt.topic<float>("servo/KD");
	servo.current == mqtt.topic<float>("servo/current");
	servo.angleTarget == mqtt.topic<int>("servo/angleTarget");
	servo.running == mqtt.topic<bool>("servo/running");
	servo.deviceMessage >> mqtt.toTopic<std::string>("servo/message");
	slowPoller(servo.KI)(servo.KP)(servo.KD)(servo.angleTarget)(servo.deviceMessage)(servo.running);

	servo.observeOn(servoThread);
	xTaskCreatePinnedToCore([](void*) {
		INFO("servoThread started.");
		servoThread.run();
	}, "servo", 20000, NULL, 17, NULL, APP_CPU);
#endif

	xTaskCreatePinnedToCore([](void*) {
		INFO("mqttThread started.");
		mqttThread.run();
	}, "mqtt", 20000, NULL, 17, NULL, PRO_CPU);

	thisThread.run();
	// DON'T EXIT , local varaibale will be destroyed
}
