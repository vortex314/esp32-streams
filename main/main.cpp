
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
class Poller : public Coroutine
{
    std::vector<Requestable*> _requestables;
    uint32_t _interval;
    uint32_t _idx=0;
public:
    ValueFlow<bool> run=false;
    Poller(uint32_t interval):Coroutine("publisher"),_interval(interval) {};
    void setup()
    {
        timeout(_interval);
    };
    void loop()
    {
        PT_BEGIN();
        while(true) {
            PT_YIELD_UNTIL(timeout());
            _idx++;
            if ( _idx >= _requestables.size()) _idx=0;
            if ( _requestables.size() && run() ) {
                _requestables[_idx]->request();
            }
            timeout(_interval/(_requestables.size()+1));
        }
        PT_END();
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
template <class T>
class TimeoutFlow : public Flow<T,T>
{
    uint32_t _interval;
    T _defaultValue;
public:
    TimerSource timeoutSource;
    TimeoutFlow(uint32_t timeout,T defaultValue)  : timeoutSource(1,timeout,true),_defaultValue(defaultValue)
    {
        timeoutSource >> * new LambdaSink<TimerMsg>([&](TimerMsg tm) {
            INFO("timeout fired");
            this->emit(_defaultValue);
        });
    }
    void onNext(T t)
    {
        this->emit(t);
        timeoutSource.start();
    };
    void request() {};

};

#define PRO_CPU 0
#define APP_CPU 1


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


ValueFlow<std::string> systemBuild  ;
ValueFlow<std::string> systemHostname;
LambdaSource<uint32_t> systemHeap([]()
{
    return xPortGetFreeHeapSize();
});
LambdaSource<uint64_t> systemUptime([]( )
{
    return Sys::millis();
});

Poller slowPoller(5000);
Poller fastPoller(100);
Poller ticker(10);
Thread mqttThread;
Thread thisThread;
Thread motorThread;

extern "C" void app_main(void)
{
    Sys::hostname(S(HOSTNAME));
    systemHostname = S(HOSTNAME);
    systemBuild = __DATE__ " " __TIME__;

    ticker.run = true;

    wifi.init();
#ifndef HOSTNAME
    std::string hn;
    string_format(hn,"ESP32-%d",wifi.mac() & 0xFFFF);
    Sys::hostname(hn.c_str());
    systemHostname=hn;
#endif
    mqtt.init();

    wifi.connected.emitOnChange(true);
    mqtt.connected.emitOnChange(true);

    wifi.connected >> mqtt.wifiConnected;

    mqtt.connected >> led.blinkSlow;
    mqtt.connected >> slowPoller.run;
    mqtt.connected >> fastPoller.run;

    mqtt.observeOn(mqttThread);

    systemHeap >> mqtt.toTopic<uint32_t>("system/heap");
    systemUptime >> mqtt.toTopic<uint64_t>("system/upTime");
    systemBuild >> mqtt.toTopic<std::string>("system/build");
    systemHostname >> mqtt.toTopic<std::string>("system/hostname");
    wifi.ipAddress >> mqtt.toTopic<std::string>("wifi/ipAddress");
    wifi.rssi >> mqtt.toTopic<int>("wifi/rssi");
    wifi.ssid >> mqtt.toTopic<std::string>("wifi/ssid");
    wifi.macAddress >> mqtt.toTopic<std::string>("wifi/mac");

    slowPoller(systemHeap)(systemUptime)(systemBuild)(systemHostname)(wifi.ipAddress)(wifi.rssi)(wifi.ssid)(wifi.macAddress);
    led.setup();
    ticker( led.blinkTimer);
    ticker(mqtt);
#ifdef GPS
    gps >> mqtt.outgoing;
    nonBlockingPool.add(gps);
#endif

#ifdef US
    ultrasonic.distance >> *new Throttle<int>(1000) >> mqtt.toTopic<int32_t>("us/distance");
    fastPoller(ultrasonic.distance);
    nonBlockingPool.add(ultrasonic);
#endif

#ifdef REMOTE
    potLeft.init();
    potRight.init();
    buttonLeft.init();
    buttonRight.init();
    ledLeft.init();
    ledRight.init();

    potLeft >> *new Median<int, 11>() >> *new Throttle<int>(1000) >> mqtt.toTopic<int>("remote/potLeft");
    potRight >> *new Median<int, 11>() >> *new Throttle<int>(1000) >> mqtt.toTopic<int>("remote/potRight");
    buttonLeft >> *new Throttle<bool>(1000) >> mqtt.toTopic<bool>("remote/buttonLeft");
    buttonRight >> *new Throttle<bool>(1000) >> mqtt.toTopic<bool>("remote/buttonRight");
    mqtt.fromTopic<bool>("remote/ledLeft") >> ledLeft;
    mqtt.fromTopic<bool>("remote/ledRight") >> ledRight;
    fastPoller(potLeft)(potRight)(buttonLeft)(buttonRight);
#endif

#ifdef MOTOR
    rotaryEncoder.init();
    rotaryEncoder.observeOn(motorThread);
    rotaryEncoder._captures.observeOn(motorThread);
    auto rotaryTimeout = new TimeoutFlow<int>(1000,0);
    rotaryTimeout->timeoutSource.subscribeOn(motorThread);
    rotaryEncoder >> *new Median<int, 11>()  >> *rotaryTimeout >> motor.rpmMeasured;
    motor.rpmMeasured >> mqtt.toTopic<int>("motor/rpmMeasured");

    motor.output >> mqtt.toTopic<float>("motor/pwm");
    motor.integral >> mqtt.toTopic<float>("motor/I");
    motor.derivative >> mqtt.toTopic<float>("motor/D");
    motor.proportional >> mqtt.toTopic<float>("motor/P");
    motor.rpmTarget >> mqtt.toTopic<int>("motor/rpmTarget");
    mqtt.fromTopic<int>("motor/rpmTarget") >> motor.rpmTarget;

    motor.setup();
    motor.observeOn(motorThread);

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

    nonBlockingPool.add(ticker);
    nonBlockingPool.add(slowPoller);
    nonBlockingPool.add(fastPoller);

    blockingPool.setupAll();
    nonBlockingPool.setupAll();

    xTaskCreatePinnedToCore([](void*) {
        INFO("motorThread started.");
        motorThread.run();
    }, "motorThread", 20000,NULL, 17, NULL, PRO_CPU);

    xTaskCreatePinnedToCore([](void*) {
        while(true) {
            nonBlockingPool.loopAll();
            vTaskDelay(100);
        }
    }, "nonBlocking", 20000, NULL, 17, NULL, APP_CPU);

    xTaskCreatePinnedToCore([](void*) {
        while(true) {
            blockingPool.loopAll();
            vTaskDelay(100);
        }
    }, "blocking", 20000,NULL, 17, NULL, PRO_CPU);

    xTaskCreatePinnedToCore([](void*) {
        INFO("mqttThread started.");
        mqttThread.run();
    }, "mqttThread", 20000,NULL, 17, NULL, PRO_CPU);



}
