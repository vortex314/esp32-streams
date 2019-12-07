#include "Streams.h"

//______________________________________________________________________________
//
TimerSource& Thread::operator|(TimerSource& ts)
{
    addTimer(&ts);
    return ts;
}

Requestable& Thread::operator|(Requestable& rq)
{
    addRequestable(rq);
    return rq;
}

void Thread::addTimer(TimerSource* ts)
{
    _timers.push_back(ts);
}

void Thread::addRequestable(Requestable& rq)
{
    _requestables.push_back(&rq);
};

#ifdef ARDUINO


Thread::Thread() {};

void Thread::awakeRequestable(Requestable& rq) {};
void Thread::awakeRequestableFromIsr(Requestable& rq) {};

void Thread::run() // ARDUINO single thread version ==> continuous polling
{
    for(auto timer : _timers) timer->request();
    for(auto requestable : _requestables) requestable->request();
}

#endif
#ifdef FREERTOS

Thread::Thread()
{
    _workQueue = xQueueCreate(20, sizeof(Requestable*));
};
void Thread::awakeRequestable(Requestable* rq)
{
    if(_workQueue)
        if(xQueueSend(_workQueue, &rq, (TickType_t)0) != pdTRUE) {
            WARN(" queue overflow ");
        }
};
void Thread::awakeRequestableFromIsr(Requestable* rq)
{
    if(_workQueue)
        if(xQueueSendFromISR(_workQueue, &rq, (TickType_t)0) != pdTRUE) {
            //  WARN("queue overflow"); // cannot log here concurency issue
        }
};


void Thread::run() // FREERTOS block thread until awake or timer expired.
{
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

nvs_handle _nvs=0;



#endif // FREERTOS

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
