
#include <Config.h>
#include <Wifi.h>
#include <Mqtt.h>
#include <LedBlinker.h>
#include "freertos/task.h"

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

Wifi wifi;
Mqtt mqtt;
Log logger(1024);
MqttLambdaSource<uint64_t> systemUptime("system/uptTime",[]()
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
//______________________________________________________________________
//

class Publisher : public ProtoThread, public Source<MqttMessage>
{
    std::string _systemPrefix;

public:
    LastValueSink<bool> run;
    Publisher() : ProtoThread("Publisher") {};
    void setup()
    {
        string_format(_systemPrefix, "src/%s/system/",Sys::hostname());
    }
    void loop()
    {
        std::string s;

        PT_BEGIN();
        while (true) {
            if ( run.value() ) {
                systemUptime.txd();
                systemHeap.txd();
                systemHostname.txd();
                rssiSource.txd();
                wifiIpAddress.txd();
                wifiSsid.txd();
            }
            timeout(100);
            PT_YIELD_UNTIL(timeout());
        }
        PT_END();
    }
};
//______________________________________________________________________
//
Publisher publisher;

extern "C" void app_main(void)
{
    ProtoThread::setupAll();
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

    while(true) {
        ProtoThread::loopAll();
        vTaskDelay(1);
    }
}
