#ifndef STREAMS_H
#define STREAMS_H
#include <ArduinoJson.h>
#include <atomic>
#include <deque>
#include <functional>
#include <vector>
#include <list>

#ifdef ARDUINO
#elif defined(__linux__)
#define LINUX
#else
#define FREERTOS
#include <FreeRTOS.h>
#include "freertos/task.h"
#include <freertos/semphr.h>
#include <freertos/queue.h>

#endif


#ifdef ARDUINO
#include <printf.h>
#define LOG(fmt, ...)                                                          \
    {                                                                            \
        char line[256];                                                            \
        int len = snprintf(line, sizeof(line), "I %06lld | %.12s:%.3d | ",      \
                           Sys::millis(), __FILE__, __LINE__);                     \
        snprintf((char *)(line + len), sizeof(line) - len, fmt, ##__VA_ARGS__);    \
        Serial.println(line);                                                        \
    }
#define WARN(fmt, ...)                                                         \
    {                                                                            \
        char line[256];                                                            \
        int len = snprintf(line, sizeof(line), "W %06lld | %.12s:%.3d | ",      \
                           Sys::millis(), __FILE__, __LINE__);                     \
        snprintf((char *)(line + len), sizeof(line) - len, fmt, ##__VA_ARGS__);    \
        Serial.println(line);                                                        \
    }
#define INFO LOG
class Sys
{
public:
    static String hostname;
    static String cpu;
    static String build;
    static String board;
    static uint64_t millis()
    {
        return ::millis();
    }
};
#else
#include <Log.h>
#endif
//______________________________________________________________________________
//
template <class T> class Observer
{
public:
    virtual void onNext(const T) = 0;
};
template <class IN> class Sink : public Observer<IN>
{
};
//______________________________________________________________________________
class Thread;
template <class IN,class OUT> class Flow;
//
// DD : used vector of void pointers and not vector of pointers to template
// class, to avoid explosion of vector implementations
class Requestable
{
public:
    virtual void request() = 0;
    //{ WARN(" I am abstract Requestable. Don't call me."); };
};
// not sure these extra inheritance are useful
template <class T> class Source : public Requestable
{
    std::vector<void *> _observers;

protected:
    uint32_t size()
    {
        return _observers.size();
    }
    Observer<T> *operator[](uint32_t idx)
    {
        return static_cast<Observer<T> *>(_observers[idx]);
    }

public:
    virtual void subscribe(Observer<T> &observer)
    {
        _observers.push_back((void *)&observer);
    }

    void emit(const T t)
    {
        for (void *pv : _observers) {
            Observer<T> *pObserver = static_cast<Observer<T> *>(pv);
            pObserver->onNext(t);
        }
    }

};

class Async
{
    Thread* _observerThread=0;
public:
    void observeOn(Thread& thread)
    {
        _observerThread = &thread;
    }
    Thread* observerThread()
    {
        return _observerThread;
    }
};

// A flow can be both Sink and Source. Most of the time it will be in the middle
// of a stream
//______________________________________________________________________________________
//
template <class IN, class OUT>
class Flow : public Sink<IN>, public Source<OUT>
{
public:
    Flow() {};
    Flow<IN, OUT>(Sink<IN> &a, Source<OUT> &b) : Sink<IN>(a), Source<OUT>(b) {};
};
//_________________________________________CompositeFlow_______________________________
//
template <class IN, class INTERM, class OUT>
class CompositeFlow : public Flow<IN, OUT>
{
    Flow<IN,INTERM> &_in;
    Flow<INTERM,OUT> &_out;

public:
    CompositeFlow(Flow<IN,INTERM> &a, Flow<INTERM,OUT> &b) : Flow<IN,OUT>(a,b),_in(a),_out(b)
    {
    };
    void request()
    {
        _in.request();
    };
    void onNext(const IN in)
    {
        _in.onNext(in);
    }
    void subscribe(Observer<OUT>& observer)
    {
        _out.subscribe(observer);
    }
};
//______________________________________________________________________________________
//
template <class IN, class INTERM, class OUT>
Flow<IN, OUT> &operator>>(Flow<IN, INTERM> &flow1, Flow<INTERM, OUT> &flow2)
{
    Flow<IN,OUT>* cflow = new CompositeFlow<IN, INTERM,OUT>(flow1, flow2);
    flow1.subscribe(flow2);
    return *cflow;
};

template <class IN, class OUT>
Sink<IN> &operator>>(Flow<IN, OUT> &flow, Sink<OUT> &sink)
{
    flow.subscribe(sink);
    return flow;
};

template <class IN, class OUT>
Source<OUT> &operator>>(Source<IN> &source, Flow<IN, OUT> &flow)
{
    source.subscribe(flow);
    return flow;
};

template <class OUT> void operator>>(Source<OUT> &source, Sink<OUT> &sink)
{
    source.subscribe(sink);
};

//______________________________________________________________________________
//
//______________________________________________________________________________
//

template <class T> class ValueFlow : public Flow<T, T>
{
    T _value;
    bool _emitOnChange = false;

public:
    ValueFlow() {}
    ValueFlow(T x)  : Flow<T,T>(),_value(x) { };
    void request()
    {
        this->emit(_value);
    }
    void onNext(const T value)
    {
        if (_emitOnChange && (_value != value)) {
            this->emit(value);
        }
        _value = value;
    }
    void emitOnChange(bool b)
    {
        _emitOnChange = b;
    };
    inline void operator=(T value)
    {
        onNext(value);
    };
    inline T operator()()
    {
        return _value;
    }
};
//______________________________________________________________________________
//
template <class T> class LambdaSink : public Sink<T>
{
    std::function<void(T)> _handler;

public:
    LambdaSink() {};
    LambdaSink(std::function<void(T)> handler) : _handler(handler) {};
    void handler(std::function<void(T)> handler)
    {
        _handler = handler;
    };
    void onNext(T event)
    {
        _handler(event);
    };
};
//________________________________________________________________________
//
template <class T>
class LambdaSource : public Source< T>
{
    std::function<T()> _handler;

public:
    LambdaSource(std::function<T()> handler)
        : _handler(handler) {};
    void request()
    {
        this->emit(_handler());
    }
};
//______________________________________________________________________________
//
template <class IN, class OUT> class LambdaFlow : public Flow<IN, OUT>
{
    std::function<OUT(IN)> _handler;

public:
    LambdaFlow() {};
    LambdaFlow(std::function<OUT(IN)> handler) : _handler(handler) {};
    void handler(std::function<OUT(IN)> handler)
    {
        _handler = handler;
    };
    void onNext(IN event)
    {
        OUT out =_handler(event);
        this->emit(out);
    };
    void request() {};
};
//______________________________________________________________________________
//
template <class T> class Filter : public Flow<T, T>
{
    T _value;

public:
    Filter(T value)
    {
        _value = value;
    }
    void onNext(T in)
    {
        if (in == _value)
            this->emit(in);
    }
};

#include <MedianFilter.h>

template <class T, int x> class Median : public Flow<T, T>
{
    MedianFilter<T, x> _mf;

public:
    Median() {};
    void onNext(T value)
    {
        _mf.addSample(value);
        if (_mf.isReady()) {
            this->emit(_mf.getMedian());
        }
    };
    void request()
    {
        WARN(" not made for polling ");
    }
};



template <class T> class Router : public Flow<T, T>
{
    uint32_t _idx;

public:
    void onNext(T t)
    {
        _idx++;
        if (_idx > this->size())
            _idx = 0;
        (*this)[_idx]->onNext(t);
    }
};
//______________________________________________________________________________
//
class TimerSource;
class Thread
{
    std::vector<Requestable*> _requestables;
    std::vector<TimerSource*> _timers;
#ifdef FREERTOS
    QueueHandle_t _workQueue=0;
#endif

public:
    void addTimer(TimerSource* ts);
    void addRequestable(Requestable& rq);
    Thread();
    void awakeRequestable(Requestable* rq) ;
    void awakeRequestableFromIsr(Requestable* rq) ;
    void run()   ;
};


//______________________________________________________________________________
//
class AtomicSource : public Source<uint32_t>
{
    std::atomic<uint32_t> _atom;

public:
    void inc()
    {
        _atom++;
    }
    void request()
    {
        if (_atom > 0) {
            emit(_atom);
            _atom--;
        }
    }
};
template <class T>
class Throttle : public Flow<T,T>
{
    uint32_t _delta;
    uint64_t _nextEmit;
    T _lastValue;
public:
    Throttle(uint32_t delta)
    {
        _delta = delta;
    }
    void onNext(T value)
    {
        uint64_t now=Sys::millis();
        if ( (_lastValue != value) || ( now > _nextEmit)) {
            this->emit(value);
            _nextEmit=now + _delta;
        }
        _lastValue=value;
    }
    void request() {};
};
//______________________________________________________________________________
//
class TimerMsg
{
public:
    uint32_t id;
};

class TimerSource : public Source<TimerMsg>, public Async
{
    uint32_t _interval;
    bool _repeat;
    uint64_t _expireTime;
    uint32_t _id;

public:
    ValueFlow<bool> run=true;
    TimerSource(int id, uint32_t interval, bool repeat)
    {
        _id = id;
        _interval = interval;
        _repeat = repeat;
        start();
    }
    void start()
    {
        run=true;
        _expireTime = Sys::millis() + _interval;
    }
    void stop()
    {
        run=false;
    }
    void interval(uint32_t i)
    {
        _interval = i;
    }
    void request()
    {
        if(run())
            if (Sys::millis() >= _expireTime) {
//                INFO("[%X]:%d:%llu timer emit ",this,interval(),expireTime());
                _expireTime +=_interval;
                this->emit({_id});
            }
    }
    uint64_t expireTime()
    {
        return _expireTime;
    }
    inline uint32_t interval()
    {
        return _interval;
    }
    void subscribeOn(Thread& thread)
    {
        thread.addTimer(this);
    }
};


//______________________________________________________________________________
//
template <class T> class AsyncValueFlow : public Flow<T, T>,public Async
{
    T _value;

public:
    void onNext(T value)
    {
        _value = value;
        if ( this->observerThread() )
            this->observerThread()->awakeRequestable(this);
    }
    void request()
    {
        this->emit(_value);
    }

};

#ifdef FREERTOS

template <class T> class AsyncFlow : public Flow<T, T>,public Async
{
    std::deque<T> _buffer;
    uint32_t _queueDepth;
    SemaphoreHandle_t xSemaphore = NULL;

public:
    AsyncFlow(uint32_t size) : _queueDepth(size)
    {
        xSemaphore = xSemaphoreCreateBinary();
        xSemaphoreGive(xSemaphore);
    }
    void onNext(T event)
    {
        if (xSemaphoreTake(xSemaphore, (TickType_t)10) == pdTRUE) {
            if (_buffer.size() >= _queueDepth) {
                _buffer.pop_front();
                //					WARN(" buffer overflow in
                // BufferedSink
                //");
            }
            _buffer.push_back(event);
            xSemaphoreGive(xSemaphore);
            if ( this->observerThread() )
                this->observerThread()->awakeRequestable(this);
            return;
        } else {
            WARN(" timeout on async buffer ! ");
        }
    }
    void onNextFromIsr(T event)
    {
        BaseType_t higherPriorityTaskWoken;
        if (xSemaphoreTakeFromISR(xSemaphore, &higherPriorityTaskWoken) == pdTRUE) {
            if (_buffer.size() >= _queueDepth) {
                _buffer.pop_front();
                //					WARN(" buffer overflow in
                // BufferedSink
                //");
            }
            _buffer.push_back(event);
            xSemaphoreGive(xSemaphore);
            if ( this->observerThread()) this->observerThread()->awakeRequestableFromIsr(this);
            return;
        } else {
            //           WARN(" timeout on async buffer ! "); // no log from ISR
        }
    }

    void request()
    {
        while(true) {
            bool hasData = false;
            if (xSemaphoreTake(xSemaphore, (TickType_t)10) == pdTRUE) {
                T t;
                if (_buffer.size()) {
                    t = _buffer.front();
                    _buffer.pop_front();
                    hasData = true;
                }
                xSemaphoreGive(xSemaphore);
                if (hasData) this->emit(t);
            } else {
                WARN(" timeout on async buffer ! ");
            }
            if ( !hasData) break;
        }
    }

};
#endif

#ifdef ARDUINO

template <class T> class AsyncFlow : public Flow<T, T>,public Async
{
    std::deque<T> _buffer;
    uint32_t _queueDepth;

public:
    AsyncFlow(uint32_t size) : _queueDepth(size) {}
    void onNext(T event)
    {
        noInterrupts();
        if (_buffer.size() >= _queueDepth) {
            _buffer.pop_front();
            //					WARN(" buffer overflow in
            // BufferedSink ");
        }
        _buffer.push_back(event);
        interrupts();
    }

    void onNextFromIsr(T event)
    {
        if (_buffer.size() >= _queueDepth) {
            _buffer.pop_front();
        }
        _buffer.push_back(event);
    }

    void request()
    {
        noInterrupts();
        T t;
        bool hasData = false;
        if (_buffer.size()) {
            t = _buffer.front();
            _buffer.pop_front();
            hasData = true;
        }
        if (hasData)
            this->emit(t);
        interrupts();
    }


};
#endif

template <class T>
class MovingAverage :  public Flow<T,T>
{
    double _ratio;
    std::list<T> _samples;
    uint64_t _expTime;
    uint32_t _interval;
    uint32_t _sampleCount;


public:
    MovingAverage(uint32_t samples,uint32_t timeout )
    {
        _sampleCount=samples;
        _interval=timeout;
    };
    T average()
    {
        T sum=0;
        uint32_t count=_samples.size();
        for(auto it=_samples.begin(); it!=_samples.end(); it++) {
            sum+=*it;
        }
        if ( count==0) count=1;
        return sum/count;
    }
    void onNext(T value)
    {
        if ( _samples.size() >_sampleCount ) _samples.pop_back();
        _samples.push_front(value);
        if ( Sys::millis() > _expTime ) {
            INFO(" >>>> average %d ",average());
            _expTime+=_interval;
            this->emit(average());
        }
    }
    void request()
    {
        this->emit(average());
    }

};

class ExponentialFilter :  public Flow<double,double>
{
    double _ratio;
    double _lastValue;
    uint64_t _expTime;
    uint32_t _interval;


public:
    ExponentialFilter(double ratio, uint32_t samples,uint32_t timeout ) {};
    void onNext(double value)
    {
        _lastValue = ( 1- _ratio)*_lastValue + _ratio*value;
        if ( Sys::millis() > _expTime ) {
            _expTime+=_interval;
            emit(_lastValue);
        }
    }
    void request()
    {
        emit(_lastValue);
    }

};

template <class T>
class TimeoutFlow : public Flow<T,T>,public Async
{
    uint32_t _interval;
    T _defaultValue;
public:
    TimerSource timeoutSource;
    TimeoutFlow(uint32_t timeout,T defaultValue)  : _defaultValue(defaultValue),timeoutSource(1,timeout,true)
    {
        timeoutSource >> * new LambdaSink<TimerMsg>([&](TimerMsg tm) {
            this->emit(_defaultValue);
        });
    }
    void onNext(T t)
    {
        this->emit(t);
        timeoutSource.start();
    };
    void request() {};
    void subscribeOn(Thread& t)
    {
        timeoutSource.subscribeOn(t);
    }

};

#endif // STREAMS_H
