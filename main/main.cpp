
#include <Config.h>
#include <Wifi.h>
#include <Mqtt.h>
#include <LedBlinker.h>
#include "freertos/task.h"

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
#endif

#ifdef MOTOR
#include <RotaryEncoder.h>
#include <MotorSpeed.h>
Connector uextMotor(MOTOR);
RotaryEncoder rotaryEncoder(uextMotor.toPin(LP_SCL), uextMotor.toPin(LP_SDA));
MotorSpeed motor(&uextMotor);
#endif

#ifdef SERVO
#include <MotorServo.h>
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

ValueFlow<std::string> systemBuild;
ValueFlow<std::string> systemHostname;
LambdaSource<uint32_t> systemHeap([]()
{
    return xPortGetFreeHeapSize();
});
LambdaSource<uint64_t> systemUptime([]()
{
    return Sys::millis();
});

Poller slowPoller(5000);
Thread mqttThread;
Thread thisThread;

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

    slowPoller(systemHeap)(systemUptime)(systemBuild)(systemHostname)(wifi.ipAddress)(wifi.rssi)(wifi.ssid)(
        wifi.macAddress);

#ifdef GPS
    gps.init(); // no thread , driven from interrupt
    gps >> *new Throttle<MqttMessage>(1000)  >> mqtt.outgoing.fromIsr;
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
    potLeft >> *new Median<int, 11>() >> *new Throttle<int>(1000) >> mqtt.toTopic<int>("remote/potLeft"); // timer driven
    potRight >> *new Median<int, 11>() >> *new Throttle<int>(1000) >> mqtt.toTopic<int>("remote/potRight"); // timer driven
    buttonLeft >> *new Throttle<bool>(1000) >> mqtt.toTopic<bool>("remote/buttonLeft");   // ISR driven
    buttonRight >> *new Throttle<bool>(1000) >> mqtt.toTopic<bool>("remote/buttonRight"); // ISR driven
    mqtt.fromTopic<bool>("remote/ledLeft") >> ledLeft;
    mqtt.fromTopic<bool>("remote/ledRight") >> ledRight;
#endif

#ifdef MOTOR

    INFO(" init motor ");
    rotaryEncoder.init();
    rotaryEncoder.observeOn(thisThread);
    rotaryEncoder.rpmMeasured >> motor.rpmMeasured;
    rotaryEncoder.rpmMeasured >>*new Throttle<int32_t>(1000)>> mqtt.toTopic<int>("motor/rpmMeasured");

    motor.init();
    motor.output >> mqtt.toTopic<float>("motor/pwm");
    motor.integral >> mqtt.toTopic<float>("motor/I");
    motor.derivative >> mqtt.toTopic<float>("motor/D");
    motor.proportional >> mqtt.toTopic<float>("motor/P");
    motor.rpmTarget >> mqtt.toTopic<int>("motor/rpmTarget");
    mqtt.fromTopic<int>("motor/rpmTarget") >> motor.rpmTarget;

    motor.observeOn(thisThread);


    /*       servo.output >> *new Throttle<float>(1000) >> mqtt.toTopic<float>("servo/pwm");
           servo.integral >> mqtt.toTopic<float>("servo/I");
           servo.derivative >> mqtt.toTopic<float>("servo/D");
           servo.proportional >> mqtt.toTopic<float>("servo/P");
           servo.angleTarget >> mqtt.toTopic<int>("servo/angleTarget");
           servo.angleMeasured >> mqtt.toTopic<int>("servo/angleMeasured");
           mqtt.fromTopic<int>("servo/angleTarget") >> servo.angleTarget;

           //
           nonBlockingPool.add(servo);*/
#endif




    xTaskCreatePinnedToCore([](void*) {
        INFO("mqttThread started.");
        mqttThread.run();
    }, "T-mqtt", 20000, NULL, 17, NULL, PRO_CPU);

    thisThread.run();
}
