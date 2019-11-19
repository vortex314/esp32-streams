#include "Streams.h"

//______________________________________________________________________________
//
#ifdef ARDUINO

void Thread::addTimer(TimerSource& ts)
{
    _requestables.push_back(&ts);
};
void Thread::addRequestable(Requestable& rq)
{
    _requestables.push_back(&rq);
};
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
void Thread::addTimer(TimerSource* ts)
{
    _timers.push_back(ts);
}

void Thread::run() // FREERTOS block thread until awake or timer expired.
{
    while(true) {
        uint64_t expTime = Sys::millis() + 5000;
        TimerSource* expiredTimer = 0;

        for(auto timer : _timers) {
            if(timer->expireTime() < expTime) {
                expTime = timer->expireTime();
                expiredTimer = timer;
            }
        }
        if(expiredTimer && expTime < Sys::millis()) {
//            INFO("[%X]:%d:%llu  timer already expired ", expiredTimer, expiredTimer->interval(),expiredTimer->expireTime());
            uint64_t startTime = Sys::millis();
            if(expiredTimer) expiredTimer->request();
            //                INFO("[%X]:%d timer request took : %llu
            //                ",expiredTimer,expiredTimer->interval(),Sys::millis()-startTime);
        } else {
            Requestable* prq;
            if(xQueueReceive(_workQueue, &prq, (TickType_t)pdMS_TO_TICKS(expTime - Sys::millis()) + 1) == pdTRUE) {
                prq->request();
                //                    INFO("executed request [%X]",prq);
            } else {
                if(expiredTimer) {
                    //                        INFO("[%X]:%d:%llu timer request
                    //                        ",expiredTimer,expiredTimer->interval(),expiredTimer->expireTime());
                    expiredTimer->request();
                }
            }
        }
    }
}


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
