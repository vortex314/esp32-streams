#include "Streams.h"

//______________________________________________________________________________
//
TimerSource& Thread::operator|(TimerSource& ts) {
	addTimer(&ts);
	return ts;
}


void Thread::addTimer(TimerSource* ts) {
	_timers.push_back(ts);
}


#ifdef ARDUINO


Thread::Thread() {};

void Thread::awakeRequestable(Requestable& rq) {};
void Thread::awakeRequestableFromIsr(Requestable& rq) {};

void Thread::run() { // ARDUINO single thread version ==> continuous polling
	for(auto timer : _timers) timer->request();
	for(auto requestable : _requestables) requestable->request();
}


void* Thread::id() {
	return 1;
}

void* Thread::currentId() {
	return 1;
}

#endif
#ifdef FREERTOS
extern void *pxCurrentTCB;
Thread::Thread() {
	_workQueue = xQueueCreate(20, sizeof(Requestable*));
};
void Thread::awakeRequestable(Requestable* rq) {
	if(_workQueue)
		if(xQueueSend(_workQueue, &rq, (TickType_t)0) != pdTRUE) {
			WARN(" queue overflow ");
		}
};
void Thread::awakeRequestableFromIsr(Requestable* rq) {
	if(_workQueue)
		if(xQueueSendFromISR(_workQueue, &rq, (TickType_t)0) != pdTRUE) {
			//  WARN("queue overflow"); // cannot log here concurency issue
		}
};

void* Thread::id() {
	return _tcb;
}

void* Thread::currentId() {
	return pxCurrentTCB;
}


void Thread::run() { // FREERTOS block thread until awake or timer expired.
	_tcb=currentId();
	while(true) {
		uint64_t now= Sys::millis();
		uint64_t expTime = now + 5000;
		TimerSource* expiredTimer = 0;
// find next expired timer if any within 5 sec
		for(auto timer : _timers) {
			if(timer->expireTime() < expTime) {
				expTime = timer->expireTime();
				expiredTimer = timer;
			}
		}

		if(expiredTimer && (expTime <= now)) {
			if(expiredTimer) expiredTimer->request();
		} else {
			Requestable* prq;
			uint32_t waitTime=pdMS_TO_TICKS(expTime - now) + 1;
			if ( waitTime < 0 ) waitTime=0;
			uint32_t queueCounter=0;
			while(xQueueReceive(_workQueue, &prq, (TickType_t)waitTime ) == pdTRUE) {
				prq->request();
				queueCounter++;
				if ( queueCounter>10 ) {
					WARN(" work queue > 10 ");
					break;
				}
				waitTime ==pdMS_TO_TICKS(expTime - Sys::millis());
				if ( waitTime > 0 ) waitTime=0;
			}
			if(expiredTimer) {
				expiredTimer->request();
			}
		}
	}
}




#endif // FREERTOS


#ifdef ESP32_IDF
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
nvs_handle _nvs=0;

void ConfigStore::init() {
	if(_nvs == 0) return;
	esp_err_t err = nvs_flash_init();
	if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	err = nvs_open("storage", NVS_READWRITE, &_nvs);
	if(err != ESP_OK) WARN(" non-volatile storage open fails.");
}
bool ConfigStore::load(const char* name, void* value, uint32_t length) {
	size_t required_size = length;
	esp_err_t err = nvs_get_blob(_nvs, name, value, &required_size);
	if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return false;
	return true;
}

bool ConfigStore::save(const char* name, void* value, uint32_t length) {
	INFO(" Config saved : %s ", name);
	esp_err_t err = nvs_set_blob(_nvs, name, value, length);
	if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return false;
	nvs_commit(_nvs);
	return true;
}
bool ConfigStore::load(const char* name, std::string& value) {
	char buffer[256];
	size_t required_size = sizeof(buffer);
	esp_err_t err = nvs_get_str(_nvs, name, buffer, &required_size);
	if(err == ESP_OK) {
		INFO("found %s", name);
		value = buffer;
		return true;
	}
	return false;
}

bool ConfigStore::save(const char* name, std::string& value) {
	char buffer[256];
	strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
	esp_err_t err = nvs_set_str(_nvs, name, buffer);
	if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return false;
	nvs_commit(_nvs);
	return true;
}

#endif

/*
namespace std {
void __throw_length_error(char const *) {
  WARN("__throw_length_error");
  while (1)
    ;
}
void __throw_bad_alloc() {
  WARN("__throw_bad_alloc");
  while (1)
    ;
}
void __throw_bad_function_call() {
  WARN("__throw_bad_function_call");
  while (1)
    ;
}
}  // namespace std
*/
