
#include <Config.h>
#include <Wifi.h>
#include <Mqtt.h>
#include <LedBlinker.h>
#include "freertos/task.h"

typedef struct {
    uint32_t id;
} TimerMsg;

class TimerSource : public Source<uint32_t>,public Coroutine
{
    uint32_t _interval;
    uint32_t _id;
public:
    TimerSource(uint32_t interval,uint32_t id) : Coroutine("TimerSource")
    {
        _interval=interval;
        _id=id;
    }
    void setup() { };
    void loop()
    {
        PT_BEGIN();
        while(true) {
            timeout(_interval);
            PT_YIELD_UNTIL(timeout());
            emit(_id);
        }
        PT_END();
    }

};

template <class T>
class MqttSource : public Source<MqttMessage>
{
    std::string _name;
    T& _v;
public:
    MqttSource(const char* name,T& v):_v(v),_name(name) {};
    void txd()
    {
        std::string s;
        DynamicJsonDocument doc(100);
        JsonVariant variant = doc.to<JsonVariant>();
        variant.set(_v);
        serializeJson(doc, s);
        this->emit({_name, s});
    }
};

template <class T>
class MqttLambdaSource : public Source<MqttMessage>
{
    std::string _name;
    std::function<T()> _handler;
public:
    MqttLambdaSource(const char* name,std::function<T()> handler):_handler(handler),_name(name) {};
    void txd()
    {
        std::string s;
        DynamicJsonDocument doc(100);
        JsonVariant variant = doc.to<JsonVariant>();
        T v = _handler();
        variant.set(v);
        serializeJson(doc, s);
        this->emit({_name, s});
    }
};

//______________________________________________________________________
//
#define PIN_LED 2

LedBlinker led(PIN_LED,100);

CoroutinePool blockingPool,nonBlockingPool;

Wifi wifi;
Mqtt mqtt;
Log logger(1024);
MqttLambdaSource<uint64_t> systemUptime("system/upTime",[]()
{
    return Sys::millis();
});
MqttLambdaSource<uint32_t> systemHeap("system/heap",[]()
{
    return xPortGetFreeHeapSize();
});
MqttLambdaSource<std::string> systemHostname("system/hostname",[]()
{
    return Sys::hostname();
});
MqttLambdaSource< int> rssiSource("wifi/rssi",[]()
{
    return wifi.rssi();
});

MqttLambdaSource< std::string> wifiIpAddress("wifi/ipAddress",[]()
{
    return wifi.ipAddress();
});

MqttLambdaSource< std::string> wifiSsid("wifi/ssid",[]()
{
    return wifi.ssid();
});

MqttLambdaSource< std::string> systemBuild("system/build",[]()
{
    return __DATE__ " " __TIME__ ;
});
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

#ifdef REMOTE
#include <Controller.h>
Controller controller;
#endif

#ifdef MOTOR
#include <RotaryEncoder.h>
#include <MotorSpeed.h>
#include <MotorServo.h>

Connector uextMotor(MOTOR);
RotaryEncoder tacho(uextMotor.toPin(LP_SCL),uextMotor.toPin(LP_SDA));
MotorSpeed motor(&uextMotor);
Connector uextServo(2);
MotorServo servo(&uextServo);
#endif

TimerSource timer1(100,1);
//______________________________________________________________________
//

class Publisher : public Coroutine, public Source<MqttMessage>
{
    std::string _systemPrefix;

public:
    ValueSink<bool> run=false;
    Publisher() : Coroutine("Publisher") {};
    void setup()
    {
        string_format(_systemPrefix, "src/%s/system/",Sys::hostname());
    }
    void loop()
    {
        std::string s;

        PT_BEGIN();
        while (true) {
            if ( run.get() ) {
                systemUptime.txd();
                systemHeap.txd();
                systemHostname.txd();
                rssiSource.txd();
                wifiIpAddress.txd();
                wifiSsid.txd();
                systemBuild.txd();
            }
            timeout(1000);
            PT_YIELD_UNTIL(timeout());
        }
        PT_END();
    }
};
//______________________________________________________________________
//
Publisher publisher;
#define PRO_CPU 0
#define APP_CPU 1
extern "C" void app_main(void)
{
    Sys::hostname(S(HOSTNAME));
    blockingPool.add(wifi);
    blockingPool.add(mqtt);
    nonBlockingPool.add(led);
    nonBlockingPool.add(publisher);

    wifi.connected >> mqtt.wifiConnected;
    mqtt.connected >> led;
    mqtt.connected >> publisher.run;
    publisher >> mqtt;
    systemUptime >> mqtt;
    systemHeap >> mqtt;
    systemHostname >> mqtt;
    rssiSource >> mqtt;
    wifiSsid >> mqtt;
    wifiIpAddress >> mqtt;
    systemBuild >> mqtt;
#ifdef GPS
    gps >> mqtt;
    nonBlockingPool.add(gps);
#endif
#ifdef US
    ultrasonic.distance >> new ToMqtt<int32_t>("us/distance") >> mqtt;
    nonBlockingPool.add(ultrasonic);
#endif
#ifdef REMOTE
    controller.potLeft >> new ToMqtt<uint32_t>("remote/potLeft") >> mqtt;
    controller.potLeft >> new ToMqtt<uint32_t>("remote/potight") >> mqtt;
    controller.buttonLeft >> new ToMqtt<bool>("remote/buttonLeft") >> mqtt;
    controller.buttonRight >> new ToMqtt<bool>("remote/buttonRight") >> mqtt;
    mqtt >> new FromMqtt<bool>("remote/ledLeft") >> controller.ledLeft;
    mqtt >> new FromMqtt<bool>("remote/ledRight") >> controller.ledRight;
    blockingPool.add(controller);
#endif
#ifdef MOTOR
    tacho >> new ToMqtt<int32_t>("motor/rpmMeasured") >> mqtt;
    tacho >> motor.rpmMeasured;

    motor.output >> new ToMqtt<float>("motor/pwm") >> mqtt;
    motor.integral >> new ToMqtt<float>("motor/I") >> mqtt;
    motor.derivative >> new ToMqtt<float>("motor/D") >> mqtt;
    motor.proportional >> new ToMqtt<float>("motor/P") >> mqtt;
    motor.rpmTarget >> new ToMqtt<int>("motor/rpmTarget") >> mqtt;
    mqtt >> new FromMqtt<int>("motor/rpmTarget") >> motor.rpmTarget;

    servo.output >> new ToMqtt<float>("servo/pwm") >> mqtt;
    servo.integral >> new ToMqtt<float>("servo/I") >> mqtt;
    servo.derivative >> new ToMqtt<float>("servo/D") >> mqtt;
    servo.proportional >> new ToMqtt<float>("servo/P") >> mqtt;
    servo.angleTarget >> new ToMqtt<int>("servo/angleTarget") >> mqtt;
    servo.angleMeasured >> new ToMqtt<int>("servo/angleMeasured") >> mqtt;
    mqtt >> new FromMqtt<int>("servo/angleTarget") >> servo.angleTarget;

//    nonBlockingPool.add(tacho);
//    nonBlockingPool.add(motor);
    nonBlockingPool.add(servo);
#endif

    blockingPool.setupAll();
    nonBlockingPool.setupAll();

    xTaskCreatePinnedToCore([](void*) {
        while(true) {
            blockingPool.loopAll();
            vTaskDelay(1);
        }
    },"blocking",10000,NULL,( 2 | portPRIVILEGE_BIT ),NULL,PRO_CPU);

    xTaskCreatePinnedToCore([](void*) {
        while(true) {
            nonBlockingPool.loopAll();
            vTaskDelay(1);
        }
    },"nonBlocking",10000,NULL,10,NULL,APP_CPU);
}
