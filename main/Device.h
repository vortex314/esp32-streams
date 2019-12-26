#ifndef DEVICE_H
#define DEVICE_H

#include <Streams.h>

class Device {

	public:
		ValueFlow<bool> running=false;
		ValueFlow<std::string> deviceMessage;
		Device();
		~Device();
		void run() { deviceMessage="OK"; running=true;};
		void stop(const char* reason) { deviceMessage=reason; running=false; }
		bool isRunning() { return running();}
		bool isStopped() { return !running(); }

};

#endif // DEVICE_H
