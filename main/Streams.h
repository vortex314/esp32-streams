
#include <deque>
#include <functional>
#include <stdint.h>
#include <vector>
#ifndef STREAMS_H
#define STREAMS_H
#include <Log.h>

//______________________________________________________________________________
//
template <typename T>  class AbstractSource;

template <class T> class AbstractSink
{
public:
    virtual void onNext(T event) = 0;
	virtual void onSubscribe(AbstractSource<T>*) {};
};
//______________________________________________________________________________
//
template <typename T> class AbstractSource
{
public:
    virtual void emit(T event) = 0;
	virtual void subscribe(AbstractSink<T>* sink)=0;
	virtual void request()=0;
};
//_______________________________________________________________________________
//
/*class SinkPool {
	std::vector<AbstractSink*> _sinks;
public:
SinkPool(){};
	void add(AbstractSink* sink)
		{
			_sinks.push_back(sink);
		}
		void run() {
			for(AbstractSink* as:_sinks){
				
			}
		}
};*/



//______________________________________________________________________________
//
template <class T> class Sink : public AbstractSink<T>
{
    std::vector<AbstractSource<T> *> _sources;

public:
    Sink() {};
    void onSubscribe(AbstractSource<T> *source)
    {
        _sources.push_back(source);
    }
	void request(){
		 for (AbstractSource<T> *source : _sources) {
            source->request();
        }
	}
};
//______________________________________________________________________________
//
template <class T> class HandlerSink : public AbstractSink<T>
{
    std::function<void(T)> _handler;

public:
    HandlerSink() {};
    HandlerSink(std::function<void(T)> handler) : _handler(handler) {};
    void handler(std::function<void(T)> handler)
    {
        _handler = handler;
    };
    void onNext(T event)
    {
        _handler(event);
    };
};
//______________________________________________________________________________
//
template <class T> class Source : public AbstractSource<T>
{
    std::vector<AbstractSink<T> *> _sinks;

public:
    Source() {};
    void subscribe(AbstractSink<T> *_sink)
    {
        _sinks.push_back(_sink);
    }
    Source<T>& operator>>(std::function<void(T)> handler)
    {
		HandlerSink<T> hs = new HandlerSink<T>(handler);
        subscribe(hs);
		return *this;
    };
    Source<T>& operator>>(AbstractSink<T> &sink)
    {
        subscribe(&sink);
		return  *this;
    }
	Source<T>& operator>>(AbstractSink<T>* sink)
    {
        subscribe(sink);
		return  *this;
    }
    void emit(T event)
    {
        for (AbstractSink<T> *sink : _sinks) {
            sink->onNext(event);
        }
    };
	void request(){
		
	}
};
//______________________________________________________________________________
//
template <class IN, class OUT>
class Flow : public AbstractSink<IN>, public Source<OUT> {};
//______________________________________________________________________________
//
template <class IN, class OUT>
Source<OUT> &operator>>(Source<IN> &source, Flow<IN, OUT> &flow)
{
    source.subscribe(&flow);
    return flow;
};

template <class IN, class OUT>
Source<OUT> &operator>>(Source<IN> &source, Flow<IN, OUT> *flow)
{
    source.subscribe(flow);
    return *flow;
};

template <class IN, class OUT>
AbstractSink<IN> & operator>>(Flow<IN,OUT> &flow, AbstractSink< OUT> & sink)
{
    flow.subscribe(sink);
    return flow;
};

template <class IN, class OUT>
AbstractSink<IN> & operator>>(Flow<IN,OUT> &flow, AbstractSink< OUT> * sink)
{
    flow.subscribe(sink);
    return flow;
};

//______________________________________________________________________________
//
#include <FreeRTOS.h>
#include <freertos/semphr.h>
template <class T> class BufferedSink : public AbstractSink<T>
{
    std::deque<T> _buffer;
    uint32_t _queueDepth;
    SemaphoreHandle_t xSemaphore = NULL;

public:
    BufferedSink(uint32_t size) : _queueDepth(size)
    {
        xSemaphore = xSemaphoreCreateBinary();
        xSemaphoreGive( xSemaphore );
    }
    void onNext(T event)
    {
        if( xSemaphoreTake( xSemaphore, ( TickType_t ) 10 ) == pdTRUE ) {
            if (_buffer.size() >= _queueDepth) {
                _buffer.pop_front();
                WARN(" buffer overflow in BufferedSink ");
            }
            _buffer.push_back(event);
            xSemaphoreGive( xSemaphore );
        }
    }

    void getNext(T& t)
    {
        if( xSemaphoreTake( xSemaphore, ( TickType_t ) 10 ) == pdTRUE ) {
            t = _buffer.front();
            _buffer.pop_front();
            xSemaphoreGive( xSemaphore );
        }
    }
    bool hasNext()
    {
        return !_buffer.empty();
    }
};
//______________________________________________________________________________
//
template <class T>
class ValueSink : public AbstractSink<T>
{
    T _value;

public:
    ValueSink(T value)
    {
        _value=value;
    }
    void onNext(T value)
    {
        _value=value;
    }
	T operator()(){
		return _value;
	}
};
//______________________________________________________________________________
//
template <class T>
class ReferenceSource : public Source<T>
{
    T* _ref;
public:
    ReferenceSource(T* ref):_ref(ref) {};
    void pub()
    {
        this->emit(*_ref);
    }
};
//______________________________________________________________________________
//
template <class T>
class ValueSource : public Source<T>
{
    T _value;
	bool _emitOnChange=false;
	bool _hasNewValue;
public:
    ValueSource(T x)
    {
        _value=x;
		_hasNewValue=true;
    };
    void request()
    {
		if ( _hasNewValue ){
        this->emit(_value);
		_hasNewValue=false;
		}
    }
    inline void operator=(T value)
    {
        _value=value;
		_hasNewValue=true;
    };
	T operator()(){
		return _value;
	}
};

//______________________________________________________________________________
//
template <class T>
class ValueFlow : public Source<T>,public AbstractSink<T>
{
    T _value;
public:
    ValueFlow(T x)
    {
        _value=x;
    };
    inline T operator()()
    {
        return _value;
    }
    inline void operator=(T value)
    {
        _value=value;
    }
    inline void onNext(T value)
    {
        _value=value;
    }
    inline  void request()
    {
        this->emit(_value);
    }
};
//______________________________________________________________________________
//
template <class T>
class PropertyFlow : public Flow<T,T>
{
    T _value;
    std::string _name;

public:
    PropertyFlow(const char* name) : _name(name) {}
    void operator=(T value)
    {
        _value = value;
        emit(value);
    }
    T value()
    {
        return _value;
    }
    void publish()
    {
        emit(value);
    }
    void onNext(T value)
    {
        _value=value;
        this->emit(value);
    }
};



template <class T>
void operator|(Flow<T,T>& x,Flow<T,T>& y)
{
    x >> y;
    y >> x;
}

template <class T> 
class Wait : public Flow<T,T> {
	uint64_t _last;
	uint32_t _delay;
public :
	Wait(uint32_t delay) : _delay(delay){}
	void onNext(T value){
		uint32_t delta =  Sys::millis()-_last;
		if ( delta > _delay ){
			this->emit(value);
		}
		_last = Sys::millis();
	}

};

struct TimerMsg {
	const int id;
};

class TimerSource : public Source<TimerMsg>
{
    uint32_t _interval;
    bool _repeat;
	uint64_t _expireTime;
	int _id;
public:
    TimerSource(int id,uint32_t interval,bool repeat) 
    {
		_id=id;
        _interval=interval;
        _repeat = repeat;
		_expireTime=Sys::millis()+_interval;
    }
    void request()
    {
		if ( Sys::millis() > _expireTime){
			_expireTime=Sys::millis()+_interval;
			emit({_id});
		}
    }

};



#endif
