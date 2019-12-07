
#include <Config.h>
#include <Wifi.h>
#include <Mqtt.h>
#include <LedBlinker.h>
#include "freertos/task.h"
#define STRINGIFY(X) #X
#define S(X) STRINGIFY(X)

//______________________________________________________________________
//
template <class T>
class Wait : public Flow<T, T>
{
    uint64_t _last;
    uint32_t _delay;

  public:
    Wait(uint32_t delay)
        : _delay(delay)
    {
    }
    void onNext(T value)
    {
	uint32_t delta = Sys::millis() - _last;
	if(delta > _delay) {
	    this->emit(value);
	}
	_last = Sys::millis();
    }
};
//______________________________________________________________________
//
class Poller : public TimerSource, public Sink<TimerMsg>
{
    std::vector<Requestable*> _requestables;
    uint32_t _idx = 0;

  public:
    ValueFlow<bool> run = false;
    Poller(uint32_t iv)
        : TimerSource(1, 1000, true)
    {
	interval(iv);
	*this >> *this;
    };

    void onNext(const TimerMsg& tm)
    {
	_idx++;
	if(_idx >= _requestables.size()) _idx = 0;
	if(_requestables.size() && run()) {
	    _requestables[_idx]->request();
	}
    }

    Poller& operator()(Requestable& rq)
    {
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
class Templ : Flow<T1, T1>, Flow<T2, T2>
{
  public:
    void onNext(const T1& t1)
    {
	T2 t2;
	Flow<T2, T2>::emit(t2);
    }
    void onNext(const T2& t2)
    {
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
Thread usThread;
#endif

#ifdef MOTOR
#include <RotaryEncoder.h>
#include <MotorSpeed.h>
Connector uextMotor(MOTOR);
RotaryEncoder rotaryEncoder(uextMotor.toPin(LP_SCL), uextMotor.toPin(LP_SDA));
MotorSpeed motor(&uextMotor);
Thread motorThread;
#endif

#ifdef SERVO
#include <MotorServo.h>
Connector uextServo(2);
MotorServo servo(&uextServo);
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
LambdaSource<uint32_t> systemHeap([]() { return xPortGetFreeHeapSize(); });
LambdaSource<uint64_t> systemUptime([]() { return Sys::millis(); });

Poller slowPoller(5000);
Thread mqttThread;
Thread thisThread;

template <class T>
class ConfigFlow : public Flow<T, T>
{
    std::string _name;
    T _defaultValue;

  public:
    ConfigFlow(const char* name, T defaultValue)
        : _name(name)
        , _defaultValue(defaultValue){};
    void onNext(const T& value) { config.set(_name.c_str(), value); }
    void request()
    {
	T value;
	config.get<T,T>(_name.c_str(), value, _defaultValue);
	this->emit(value);
    }
};

ConfigFlow<std::string> wifiPrefix("wifi/prefix", "___");

extern "C" void app_main(void)
{
    Sys::hostname(S(HOSTNAME));
    systemHostname = S(HOSTNAME);
    systemBuild = __DATE__ " " __TIME__;

    wifi.init();
#ifndef HOSTNAME
    std::string hn;
    string_format(hn, "ESP32-%d", wifi.mac() & 0xFFFF);
    Sys::hostname(hn.c_str());
    systemHostname = hn;
#endif
    mqtt.init();
    led.init();
    wifi.connected >> mqtt.wifiConnected;
    mqtt.connected >> led.blinkSlow;
    mqtt.connected >> slowPoller.run;

    mqttThread | mqtt;
    mqttThread | led;
    mqttThread | slowPoller;

    mqtt.observeOn(mqttThread);

    systemHeap >> mqtt.toTopic<uint32_t>("system/heap");
    systemUptime >> mqtt.toTopic<uint64_t>("system/upTime");
    systemBuild >> mqtt.toTopic<std::string>("system/build");
    systemHostname >> mqtt.toTopic<std::string>("system/hostname");
    wifi.ipAddress >> mqtt.toTopic<std::string>("wifi/ipAddress");
    wifi.rssi >> mqtt.toTopic<int>("wifi/rssi");
    wifi.ssid >> mqtt.toTopic<std::string>("wifi/ssid");
    wifi.macAddress >> mqtt.toTopic<std::string>("wifi/mac");

    mqttThread | slowPoller(systemHeap)(systemUptime)(systemBuild)(systemHostname)(wifi.ipAddress)(wifi.rssi)(
                     wifi.ssid)(wifi.macAddress);

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
    potLeft.init();
    potRight.init();
    buttonLeft.init();
    buttonRight.init();
    ledLeft.init();
    ledRight.init();

    thisThread | potLeft.timer;
    thisThread | potRight.timer;
    potLeft >> *new Median<int, 11>() >> *new Throttle<int>(1000) >>
        mqtt.toTopic<int>("remote/potLeft"); // timer driven
    potRight >> *new Median<int, 11>() >> *new Throttle<int>(1000) >>
        mqtt.toTopic<int>("remote/potRight");                                             // timer driven
    buttonLeft >> *new Throttle<bool>(1000) >> mqtt.toTopic<bool>("remote/buttonLeft");   // ISR driven
    buttonRight >> *new Throttle<bool>(1000) >> mqtt.toTopic<bool>("remote/buttonRight"); // ISR driven
    mqtt.fromTopic<bool>("remote/ledLeft") >> ledLeft;
    mqtt.fromTopic<bool>("remote/ledRight") >> ledRight;
#endif

#ifdef MOTOR

    INFO(" init motor ");
    rotaryEncoder.init();
    rotaryEncoder.observeOn(motorThread);
    rotaryEncoder.rpmMeasured >> motor.rpmMeasured; // ISR driven !

    motor.init();
    motor.pwm >> *new Throttle<float>(1000) >> mqtt.toTopic<float>("motor/pwm");
    motor.rpmTarget >> mqtt.toTopic<int>("motor/rpmTarget");
    motor.rpmMeasured >> *new Throttle<int>(1000) >> mqtt.toTopic<int>("motor/rpmMeasured");
	
	*new ConfigFlow<float>("motor/KI",0.1) == motor.KI;
	motor.KI == *new MqttFlow<float>("motor/KI")::Flow<float,float>;

    mqtt.fromTopic<float>("motor/KI") >> motor.KI;
    motor.KI >> mqtt.toTopic<float>("motor/KI");
    mqtt.fromTopic<float>("motor/KP") >> motor.KP;
    motor.KP >> mqtt.toTopic<float>("motor/KP");
    mqtt.fromTopic<float>("motor/KD") >> motor.KD;
    motor.KD >> mqtt.toTopic<float>("motor/KD");
    mqtt.fromTopic<int>("motor/rpmTarget") >> motor.rpmTarget;
    motor.observeOn(motorThread);
    servo.observeOn(servoThread);
    xTaskCreatePinnedToCore([](void*) {
	INFO("motorThread started.");
	motorThread.run();
    }, "motor", 20000, NULL, 17, NULL, APP_CPU);
#endif

#ifdef SERVO
    servo.init();
    servo.pwm >> *new Throttle<float>(1000) >> mqtt.toTopic<float>("servo/pwm");
    servo.angleTarget >> mqtt.toTopic<int>("servo/angleTarget");
    servo.angleMeasured >> *new Throttle<int>(1000) >> mqtt.toTopic<int>("servo/angleMeasured");
    mqtt.fromTopic<float>("servo/KI") >> servo.KI;
    servo.KI >> mqtt.toTopic<float>("servo/KI");

    mqtt.fromTopic<float>("servo/KP") >> servo.KP;
    servo.KP >> mqtt.toTopic<float>("servo/KP");
    mqtt.fromTopic<float>("servo/KD") >> servo.KD;
    servo.KD >> mqtt.toTopic<float>("servo/KD");
    mqtt.fromTopic<int>("servo/angleTarget") >> servo.angleTarget;
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
}
