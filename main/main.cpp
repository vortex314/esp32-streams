
#include <Config.h>
#include <Wifi.h>
#include <Mqtt.h>
#include <LedBlinker.h>
#include "freertos/task.h"

template <class T>
class MqttLambdaSource : public Source<MqttMessage>
{
    std::string _name;
    std::function<T()> _handler;

  public:
    MqttLambdaSource(const char* name, std::function<T()> handler)
        : _handler(handler)
        , _name(name){};
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

LedBlinker led(PIN_LED, 100);

CoroutinePool blockingPool, nonBlockingPool;

Wifi wifi;
Mqtt mqtt;
Log logger(1024);
MqttLambdaSource<uint64_t> systemUptime("system/upTime", []() { return Sys::millis(); });
MqttLambdaSource<uint32_t> systemHeap("system/heap", []() { return xPortGetFreeHeapSize(); });
MqttLambdaSource<std::string> systemHostname("system/hostname", []() { return Sys::hostname(); });
MqttLambdaSource<int> rssiSource("wifi/rssi", []() { return wifi.rssi(); });

MqttLambdaSource<std::string> wifiIpAddress("wifi/ipAddress", []() { return wifi.ipAddress(); });

MqttLambdaSource<std::string> wifiSsid("wifi/ssid", []() { return wifi.ssid(); });

MqttLambdaSource<std::string> systemBuild("system/build", []() { return __DATE__ " " __TIME__; });
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

template <class T, int x>
class Median : public Flow<T, T>
{
    MedianFilter<T, x> _mf;

  public:
    Median(){};
    void onNext(T value) { _mf.addSample(value); };
    void request()
    {
	if(_mf.isReady()) this->emit(_mf.getMedian());
    }
};

//______________________________________________________________________
//

class Publisher : public Coroutine, public Source<MqttMessage>
{
    std::string _systemPrefix;

  public:
    ValueSink<bool> run = false;
    Publisher()
        : Coroutine("Publisher"){};
    void setup() { string_format(_systemPrefix, "src/%s/system/", Sys::hostname()); }
    void loop()
    {
	std::string s;

	PT_BEGIN();
	while(true) {
	    if(run()) {
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
    ultrasonic.distance >> mqtt.toTopic<int32_t>("us/distance") >> mqtt;
    nonBlockingPool.add(ultrasonic);
#endif
#ifdef REMOTE
    potLeft.init();
    potRight.init();
    buttonLeft.init();
    buttonRight.init();
    ledLeft.init();
    ledRight.init();

    timerPot >> potLeft >> new Median<int, 11>() >> mqtt.toTopic<int>("rermote/potLeft");
    timerPot >> potRight >> new Median<int, 11>() >> mqtt.toTopic<int>("remote/potRight");
    timerPot >> buttonLeft >> mqtt.toTopic<bool>("remote/buttonLeft");
    timerPot >> buttonRight >> mqtt.toTopic<bool>("remote/buttonRight");
    mqtt.fromTopic<bool>("remote/ledLeft") >> ledLeft;
    mqtt.fromTopic<bool>("remote/ledRight") >> ledRight;

    potLeft >> new HandlerSink<int>([](int v) { INFO(" potValue : %d", v); });

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

    servo.output >> new Wait<float>(1000) >> mqtt.toTopic<float>("servo/pwm");
    servo.integral >> mqtt.toTopic<float>("servo/I");
    servo.derivative >> mqtt.toTopic<float>("servo/D");
    servo.proportional >> mqtt.toTopic<float>("servo/P");
    servo.angleTarget >> mqtt.toTopic<int>("servo/angleTarget");
    auto s = servo.angleMeasured >> mqtt.toTopic<int>("servo/angleMeasured");
    mqtt.fromTopic<int>("servo/angleTarget") >> servo.angleTarget;

    //    nonBlockingPool.add(tacho);
    //    nonBlockingPool.add(motor);
    nonBlockingPool.add(servo);
#endif

    auto t = motor.rpmTarget >> new ToMqtt<int>("motor/rpmTarget1") >> new FromMqtt<int>("motor/rpmTarget1") >> motor.rpmTarget;
    t.emit(1);

    blockingPool.setupAll();
    nonBlockingPool.setupAll();

    xTaskCreatePinnedToCore([](void*) {
	while(true) {
	    timerPot.request();
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
