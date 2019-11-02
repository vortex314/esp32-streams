
#include <../Common/Config.h>
#include <Wifi.h>
#include <Mqtt.h>

Wifi wifi;
Mqtt mqtt;
Log logger(1024);

extern "C" void app_main(void)
{
    ProtoThread::setupAll();
wifi.connected >> mqtt.wifiConnected;
while(true){
    ProtoThread::loopAll();
    
}
}