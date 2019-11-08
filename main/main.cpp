
#include <Config.h>
#include <Wifi.h>
#include <Mqtt.h>
#include <LedBlinker.h>
#include "freertos/task.h"

//______________________________________________________________________
//
#define PIN_LED 2

Timer ledTimer(1, 10, true);
LedBlinker led(PIN_LED, 100);

CoroutinePool blockingPool, nonBlockingPool;

Wifi wifi;
Mqtt mqtt;
Log logger(1024);

//______________________________________________________________________
//
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

#define REMOTE

#ifdef REMOTE
#include <HwStream.h>
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

#include <MedianFilter.h>
TimerSource timerPot(1, 100, true);
Pot potLeft(36);
Pot potRight(39);
Button buttonLeft(13);
Button buttonRight(16);
LedLight ledLeft(32);
LedLight ledRight(23);
Router<TimerMsg> roundRobin;
TimerSource propertyTimer(1, 100, true);
//______________________________________________________________________
//
template <class T>
class LambdaSource : public Flow<TimerMsg, T>
{
    std::function<T()> _handler;

  public:
    LambdaSource(std::function<T()> handler)
        : _handler(handler){};
    void request() { this->emit(_handler()); }
    void onNext(TimerMsg) { this->emit(_handler()); }
};

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

//______________________________________________________________________
//
ValueFlow<std::string> systemBuild  ;
ValueFlow<std::string> systemHostname ;
LambdaSource<uint64_t> systemUptime([]( ) { return Sys::millis(); });
/*
 * LambdaSource<T>(handler) >> mqtt.ToMqtt<T>("")
 */
#define PRO_CPU 0
#define APP_CPU 1

extern "C" void app_main(void)
{
    Sys::hostname(S(HOSTNAME));
	systemHostname = S(HOSTNAME);
	systemBuild = __DATE__ " " __TIME__;
    blockingPool.add(wifi);
    blockingPool.add(mqtt);

    wifi.connected >> mqtt.wifiConnected;
    mqtt.connected >> led.blinkSlow;

    propertyTimer >> roundRobin >>
        *new LambdaFlow<TimerMsg, uint32_t>([](TimerMsg t) { return xPortGetFreeHeapSize(); }) >>
        mqtt.toTopic<uint32_t>("system/heap");

    //   roundRobin >> new HandlerFlow<TimerMsg, const char *>([](TimerMsg t) { return __DATE__ " " __TIME__; }) >>
    //   mqtt.toTopic<const char *>("system/build");
    /*
         * propertyTimer >> roundRobin >> LambdaSource<uint32_t>([]() { return xPortGetFreeHeapSize(); }) >>
        mqtt.toTopic<uint32_t>("system/heap")

            auto t = new LambdaSource<uint32_t>([]() { return xPortGetFreeHeapSize(); }); */

    roundRobin >> *new LambdaSource<uint32_t>([]() { return xPortGetFreeHeapSize(); }) >>
        mqtt.toTopic<uint32_t>("system/heap");

    roundRobin >> *new LambdaSource<uint32_t>([]() { return Sys::millis(); }) >>
        mqtt.toTopic<uint32_t>("system/upTime");

    wifi.ipAddress >> mqtt.toTopic<std::string>("wifi/ipAddress");
	wifi.rssi >> mqtt.toTopic<int>("wifi/rssi");
	wifi.ssid >> mqtt.toTopic<std::string>("wifi/ssid");
	systemBuild >> mqtt.toTopic<std::string>("system/build");
	systemHostname >> mqtt.toTopic<std::string>("system/hostname");

#ifdef GPS
    gps >> mqtt;
    nonBlockingPool.add(gps);
#endif
#ifdef US
    ultrasonic.distance >> mqtt.toTopic<int32_t>("us/distance");
    nonBlockingPool.add(ultrasonic);
#endif
#ifdef REMOTE
    potLeft.init();
    potRight.init();
    buttonLeft.init();
    buttonRight.init();
    ledLeft.init();
    ledRight.init();

    timerPot >> potLeft >> *new Median<int, 11>() >> mqtt.toTopic<int>("rermote/potLeft");
    timerPot >> potRight >> *new Median<int, 11>() >> mqtt.toTopic<int>("remote/potRight");
    timerPot >> buttonLeft >> mqtt.toTopic<bool>("remote/buttonLeft");
    timerPot >> buttonRight >> mqtt.toTopic<bool>("remote/buttonRight");
    mqtt.fromTopic<bool>("remote/ledLeft") >> ledLeft;
    mqtt.fromTopic<bool>("remote/ledRight") >> ledRight;

    potLeft >> *new LambdaSink<int>([](int v) { INFO(" potValue : %d", v); });

#endif
#ifdef MOTOR
    tacho >> mqtt.toTopic<int32_t>("motor/rpmMeasured");
    tacho >> motor.rpmMeasured;

    motor.output >> mqtt.toTopic<float>("motor/pwm");
    motor.integral >> mqtt.toTopic<float>("motor/I");
    motor.derivative >> mqtt.toTopic<float>("motor/D");
    motor.proportional >> mqtt.toTopic<float>("motor/P");
    //    motor.rpmTarget >> mqtt.toTopic<int>("motor/rpmTarget");
    mqtt.fromTopic<int>("motor/rpmTarget") >> motor.rpmTarget;

    servo.output >> *new Wait<float>(1000) >> mqtt.toTopic<float>("servo/pwm");
    servo.integral >> mqtt.toTopic<float>("servo/I");
    servo.derivative >> mqtt.toTopic<float>("servo/D");
    servo.proportional >> mqtt.toTopic<float>("servo/P");
//    servo.angleTarget >> mqtt.toTopic<int>("servo/angleTarget");
 //   servo.angleMeasured >> mqtt.toTopic<int>("servo/angleMeasured");
    mqtt.fromTopic<int>("servo/angleTarget") >> servo.angleTarget;

    //    nonBlockingPool.add(tacho);
    //    nonBlockingPool.add(motor);
    nonBlockingPool.add(servo);
#endif

//    auto t1 = motor.rpmTarget >> new ToMqtt<int>("motor/rpmTarget1") >> new FromMqtt<int>("motor/rpmTarget1") >>
//              motor.rpmTarget;
//    t1.emit(1);

    blockingPool.setupAll();
    nonBlockingPool.setupAll();

    xTaskCreatePinnedToCore([](void*) {
	while(true) {
	    timerPot.request();
	    led.blinkTimer.request();
	    blockingPool.loopAll();
	    vTaskDelay(1);
	}
    }, "blocking", 10000, NULL, 10, NULL, PRO_CPU);

    xTaskCreatePinnedToCore([](void*) {
	while(true) {
	    mqtt.request();
	    nonBlockingPool.loopAll();
	    vTaskDelay(1);
	}
    }, "nonBlocking", 10000, NULL, 10, NULL, APP_CPU);
}
