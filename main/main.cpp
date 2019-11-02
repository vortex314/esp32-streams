
#include <Config.h>
#include <Wifi.h>
#include <Mqtt.h>
#include <LedBlinker.h>
#include "freertos/task.h"

#define PIN_LED 2

LedBlinker led(PIN_LED,100);

Wifi wifi;
Mqtt mqtt;
Log logger(1024);

class Publisher : public ProtoThread, public Source<MqttMessage>
{
    std::string _systemPrefix;

public:
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
            string_format(s,"%s%s",_systemPrefix.c_str(),"upTime");
            emit({s,"1000"});
            timeout(1000);
            PT_YIELD_UNTIL(timeout());
        }
        PT_END();
    }
};

Publisher publisher;

extern "C" void app_main(void)
{
    ProtoThread::setupAll();
    wifi.connected >> mqtt.wifiConnected;
    mqtt.connected >> led;
    publisher >> mqtt;

    while(true) {
        ProtoThread::loopAll();
        vTaskDelay(1);
    }
}
