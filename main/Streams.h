#ifndef STREAMS_H
#define STREAMS_H
#include <ArduinoJson.h>
#include <atomic>
#include <deque>
#include <functional>
#include <vector>

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
class Sys {
	public:
		static String hostname;
		static String cpu;
		static String build;
		static String board;
		static uint64_t millis() { return ::millis(); }
};
#else
#define FREERTOS
#include <Log.h>
#endif
//______________________________________________________________________________
//
template <class T> class Observer {
	public:
		virtual void onNext(const T) = 0;
};
template <class IN> class Sink : public Observer<IN> {};
//______________________________________________________________________________
//
// DD : used vector of void pointers and not vector of pointers to template
// class, to avoid explosion of vector implementations
class Requestable {
	public:
		virtual void
		request() = 0; //{ WARN(" I am abstract Requestable. Don't call me."); };
};
// not sure these extra inheritance are useful
template <class T> class Source : public Requestable {
		std::vector<void *> _observers;

	protected:
		uint32_t size() { return _observers.size(); }
		Observer<T> *operator[](uint32_t idx) {
			return static_cast<Observer<T> *>(_observers[idx]);
		}

	public:
		void subscribe(Observer<T> &observer) {
			_observers.push_back((void *)&observer);
		}

		void emit(const T t) {
			for (void *pv : _observers) {
				Observer<T> *pObserver = static_cast<Observer<T> *>(pv);
				pObserver->onNext(t);
			}
		}
};

// A flow can be both Sink and Source. Most of the time it will be in the middle
// of a stream
//______________________________________________________________________________________
//
template <class IN, class OUT>
class Flow : public Sink<IN>, public Source<OUT> {
	public:
		Flow() {};
		Flow<IN, OUT>(Sink<IN> &a, Source<OUT> &b) : Sink<IN>(a), Source<OUT>(b) {};
};
//_________________________________________ CompositeFlow
//_____________________________________________
//
template <class IN, class OUT> class CompositeFlow : public Flow<IN, OUT> {
		Sink<IN> &_in;
		Source<OUT> &_out;

	public:
		CompositeFlow(Sink<IN> &a, Source<OUT> &b) : _in(a), _out(b) {};
		void request() { _out.request(); };
		void onNext(const IN in) { _in.onNext(in); }
};
//______________________________________________________________________________________
//
template <class IN, class INTERM, class OUT>
Flow<IN, OUT> &operator>>(Flow<IN, INTERM> &flow1, Flow<INTERM, OUT> &flow2) {
	flow1.subscribe(flow2);
	return *new CompositeFlow<IN, OUT>(flow1, flow2);
};

template <class IN, class OUT>
Sink<IN> &operator>>(Flow<IN, OUT> &flow, Sink<OUT> &sink) {
	flow.subscribe(sink);
	return flow;
};

template <class IN, class OUT>
Source<OUT> &operator>>(Source<IN> &source, Flow<IN, OUT> &flow) {
	source.subscribe(flow);
	return flow;
};

template <class OUT> void operator>>(Source<OUT> &source, Sink<OUT> &sink) {
	source.subscribe(sink);
};

//______________________________________________________________________________
//
//______________________________________________________________________________
//

template <class T> class ValueFlow : public Flow<T, T> {
		T _value;
		bool _emitOnChange = false;

	public:
		ValueFlow() {}
		ValueFlow(T x) { _value = x; };
		void request() { this->emit(_value); }
		void onNext(const T value) {
			if (_emitOnChange && (_value != value)) {
				this->emit(value);
			}
			_value = value;
		}
		void emitOnChange(bool b) { _emitOnChange = b; };
		inline void operator=(T value) { onNext(value); };
		inline T operator()() { return _value; }
};
//______________________________________________________________________________
//
template <class T> class LambdaSink : public Sink<T> {
		std::function<void(T)> _handler;

	public:
		LambdaSink() {};
		LambdaSink(std::function<void(T)> handler) : _handler(handler) {};
		void handler(std::function<void(T)> handler) { _handler = handler; };
		void onNext(T event) { _handler(event); };
};
//________________________________________________________________________
//
template <class T>
class LambdaSource : public Source< T> {
		std::function<T()> _handler;

	public:
		LambdaSource(std::function<T()> handler)
			: _handler(handler) {};
		void request() {
			this->emit(_handler());
		}
};
//______________________________________________________________________________
//
template <class IN, class OUT> class LambdaFlow : public Flow<IN, OUT> {
		std::function<OUT(IN)> _handler;

	public:
		LambdaFlow() {};
		LambdaFlow(std::function<OUT(IN)> handler) : _handler(handler) {};
		void handler(std::function<OUT(IN)> handler) { _handler = handler; };
		void onNext(IN event) { _handler(event); };
};
//______________________________________________________________________________
//
template <class T> class Filter : public Flow<T, T> {
		T _value;

	public:
		Filter(T value) { _value = value; }
		void onNext(T in) {
			if (in == _value)
				this->emit(in);
		}
};

#include <MedianFilter.h>

template <class T, int x> class Median : public Flow<T, T> {
		MedianFilter<T, x> _mf;

	public:
		Median() {};
		void onNext(T value) {
			_mf.addSample(value);
			if (_mf.isReady()) {
				this->emit(_mf.getMedian());
			}
		};
		void request() { WARN(" not made for polling "); }
};

template <class T> class Router : public Flow<T, T> {
		uint32_t _idx;

	public:
		void onNext(T t) {
			_idx++;
			if (_idx > this->size())
				_idx = 0;
			(*this)[_idx]->onNext(t);
		}
};
//______________________________________________________________________________
//
#ifdef FREERTOS
#include <FreeRTOS.h>
#include "freertos/task.h"
#include <freertos/semphr.h>

template <class T> class AsyncFlow : public Flow<T, T> {
		std::deque<T> _buffer;
		uint32_t _queueDepth;
		SemaphoreHandle_t xSemaphore = NULL;
		Thread* _subscriberThread;

	public:
		AsyncFlow(uint32_t size) : _queueDepth(size) {
			xSemaphore = xSemaphoreCreateBinary();
			xSemaphoreGive(xSemaphore);
		}
		void onNext(T event) {
			if (xSemaphoreTake(xSemaphore, (TickType_t)10) == pdTRUE) {
				if (_buffer.size() >= _queueDepth) {
					_buffer.pop_front();
					//					WARN(" buffer overflow in
					// BufferedSink
					//");
				}
				_buffer.push_back(event);
				xSemaphoreGive(xSemaphore);
				_subscriberThread.awakeRequestable(this);
				return;
			} else {
				WARN(" timeout on async buffer ! ");
			}
		}
		void onNextFromIsr(T event) {
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
				if ( _subscriberThread) _subscriberThread->awakeRequestableFromIsr(this);
				return;
			} else {
				//           WARN(" timeout on async buffer ! "); // no log from ISR
			}
		}

		void request() {
			if (xSemaphoreTake(xSemaphore, (TickType_t)10) == pdTRUE) {
				T t;
				bool hasData = false;
				if (_buffer.size()) {
					t = _buffer.front();
					_buffer.pop_front();
					hasData = true;
				}
				xSemaphoreGive(xSemaphore);
				if (hasData)
					this->emit(t);
				return;
			} else {
				WARN(" timeout on async buffer ! ");
			}
		}

		void subscribeOn(Thread* thread){_subscriberThread=thread;};
};
#else

template <class T> class AsyncFlow : public Flow<T, T> {
		std::deque<T> _buffer;
		uint32_t _queueDepth;
		Thread& _subscriberThread;

	public:
		AsyncFlow(uint32_t size) : _queueDepth(size) {}
		void onNext(T event) {
			noInterrupts();
			if (_buffer.size() >= _queueDepth) {
				_buffer.pop_front();
				//					WARN(" buffer overflow in
				// BufferedSink ");
			}
			_buffer.push_back(event);
			interrupts();
		}

		void onNextFromIsr(T event) {
			if (_buffer.size() >= _queueDepth) {
				_buffer.pop_front();
			}
			_buffer.push_back(event);
		}

		void request() {
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

		void subscribeOn(Thread* thread){_subscriberThread=thread;};

};
#endif
//______________________________________________________________________________
//
template <class T> class AsyncValueFlow : Flow<T, T> {
		T _value;

	public:
		void onNext(T value) { _value = value; }
		void request() { this->emit(_value); }
};
//______________________________________________________________________________
//
class AtomicSource : public Source<uint32_t> {
		std::atomic<uint32_t> _atom;

	public:
		void inc() { _atom++; }
		void request() {
			if (_atom > 0) {
				emit(_atom);
				_atom--;
			}
		}
};
template <class T>
class Throttle : public Flow<T,T> {
		uint32_t _delta;
		uint64_t _nextEmit;
		T _lastValue;
	public:
		Throttle(uint32_t delta) {
			_delta = delta;
		}
		void onNext(T value) {
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
class TimerMsg {
	public:
		uint32_t id;
};

class TimerSource : public Source<TimerMsg> {
		uint32_t _interval;
		bool _repeat;
		uint64_t _expireTime;
		uint32_t _id;

	public:
		TimerSource(int id, uint32_t interval, bool repeat) {
			_id = id;
			_interval = interval;
			_repeat = repeat;
			_expireTime = Sys::millis() + _interval;
		}
		void interval(uint32_t i) { _interval = i; }
		void request() {
			if (Sys::millis() > _expireTime) {
				_expireTime += _interval;
				this->emit({_id});
			}
		}
		uint64_t expireTime() { return _expireTime; }
};
//______________________________________________________________________________
//

class Thread {
	std::vector<Requestable*> _requestables;
	std::vector<TimerSource*> _timers;
	 QueueHandle_t xQueueCreate( UBaseType_t uxQueueLength,
                             UBaseType_t uxItemSize );
	public:
	void addTimer(TimerSource& ts){ _requestables.push_back(&ts)};
	void addRequestable(Requestable& rq){ _requestables.push_back(&rq)};
#ifdef ARDUINO
	Thread(){};

	void awakeRequestable(Requestable& rq) { };
	void awakeRequestableFromIsr(Requestable& rq) {};

	void run { // ARDUINO single thread version ==> continuous polling
		for( auto timer:_timers) timer->request();
		for( auto requestable:_requestables) requestable->request();
	}
#else
private:
	QueueHandle_t _workQueue;
public:
	Thread()
{    _workQueue = xQueueCreate( 10, sizeof( Requestable*) );
};
	void awakeRequestable(Requestable& rq) { xQueueSend( _workQueue, ( void * ) &rq, ( TickType_t ) 0 ); };
	void awakeRequestableFromIsr(Requestable& rq) {xQueueSendFromIsr( _workQueue, ( void * ) &rq, ( TickType_t ) 0 ); };

	void run { // FREERTOS block thread until awake or timer expired.
	while(true){
		for( auto timer:_timers)  {
			uint64_t expTime=Sys::millis()+1000;
			Timer* expiredTimer=0;
			if ( timer->expireTime() < expTime){
				expTime=timer->expireTime();
				expiredTimer = timer;
			}
		}
			if ( expTime < Sys::millis()) {
				expiredTimer->request();
			} else {
				Requestable* prq;
				 if( xQueueReceive( _workQueue, &prq, ( TickType_t ) (expTime-Sys::millis()) ) )
				 {
					 prq->request();
				 } 
			}
		}
	}
}
#endif
void xx(){

	S::Sink<xx>const 
	S::Thread ==> freeRtos or Arduino 

		addTimer() ==> TimerSource.expireTime() ==> TimerSource.request()
		awakeRequestable(requestable) ==> queue.push(requestable)
		awakeRequestableFromIsr(requestable)

		for(timer : _timers) {
			if ( timer->expireTime() < expTime){
				expTime=timer->expireTime
				expTimer = timer;
			}
		}
		if ( expTime < Sys::millis()) timer-> request()
		waitQueue(expTime-Sys::millis())
		if ( expired ) timer->request();
		else ( queueVal->request());
		
		

	timer1 >> mqtt;
	timer2 >> mqtt;
	timer3 >> mqtt;
	outgoing >> mqtt;
	mqtt >> incoming;
	mqtt.connected >> poller.run;
	serial.incoming >> mqtt.serialIn;
	mqtt.serialOut >> serial.outgoing;

	timer1.SubscribeOn(mqttThread);   ==> mqttThread.addTimer(timer1);
	outgoing.SubscribeOn(mqttThread);  ==> mqttThread.addSource(outgoing); AsyncFlow::Thread* _subscribeThread

	outgoing.onNext(xx) => push_back(xx);_subscribeThread.awakeSource(this);

	Thread	 : awakeSource(Requestable xx) => xx on queue ==>  xx.request()


	/* thread
		==> get minimal expireTime
		==> wait on own queue
		==> if ( queue contains address ) ==> source.request();
		==> if ( timeout )
}