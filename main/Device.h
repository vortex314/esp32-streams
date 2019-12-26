#ifndef DEVICE_H
#define DEVICE_H

#include <Streams.h>

class Device {

	public:
		typedef enum {
			STOPPED,
			RUNNING
		} State;
		ValueFlow<State> deviceState=STOPPED;
		ValueFlow<std::string> deviceMessage;
		Device();
		~Device();
		void run() { deviceMessage="OK"; deviceState=RUNNING;};
		void stop(const char* reason) { deviceMessage=reason; deviceState=STOPPED; }
		bool isRunning() { return deviceState()==RUNNING;}
		bool isStopped() { return deviceState()==STOPPED; }

};

#endif // DEVICE_H
