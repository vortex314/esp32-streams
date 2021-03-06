#ifndef STREAMS_H
#define STREAMS_H
#include <ArduinoJson.h>
#include <atomic>
#include <deque>
#include <functional>
#include <list>
#include <vector>

#ifdef ARDUINO

#elif defined(__linux__)
#define LINUX
#else
#define FREERTOS
#include <FreeRTOS.h>

#include "freertos/task.h"
#include <freertos/queue.h>
#include <freertos/semphr.h>

#endif

#ifdef ARDUINO
class Sys {
	public:
		static uint64_t millis();
		static const char *hostname();
};
//#include <printf.h>
#define INFO(fmt, ...)                                                         \
	{                                                                            \
		char line[256];                                                            \
		int len = snprintf(line, sizeof(line), "I %06lld | %.12s:%.3d | ",         \
		                   Sys::millis(), __FILE__, __LINE__);                     \
		snprintf((char *)(line + len), sizeof(line) - len, fmt, ##__VA_ARGS__);    \
		Serial.println(line);                                                      \
	}
#define WARN(fmt, ...)                                                         \
	{                                                                            \
		char line[256];                                                            \
		int len = snprintf(line, sizeof(line), "W %06lld | %.12s:%.3d | ",         \
		                   Sys::millis(), __FILE__, __LINE__);                     \
		snprintf((char *)(line + len), sizeof(line) - len, fmt, ##__VA_ARGS__);    \
		Serial.println(line);                                                      \
	}

#else
#include <Log.h>
#endif
//______________________________________________________________________________
//
template <class T> class Observer {
	public:
		virtual void onNext(const T &) = 0;
};
template <class IN> class Sink : public Observer<IN> {};
//______________________________________________________________________________
class Thread;
template <class IN, class OUT> class Flow;
//
// DD : used vector of void pointers and not vector of pointers to template
// class, to avoid explosion of vector implementations
class Requestable {
	public:
		virtual void request() = 0;
		//{ WARN(" I am abstract Requestable. Don't call me."); };
};
//______________________________________________________________________________
//
class TimerSource;
class Thread {
		std::vector<TimerSource *> _timers;
		std::vector<Requestable *> _requestables;
		void *_tcb;
#ifdef FREERTOS
		QueueHandle_t _workQueue = 0;
#endif

	public:
		void addTimer(TimerSource *ts);
		void addRequestable(Requestable &rq);
		Thread();
		int awakeRequestable(Requestable *rq);
		int awakeRequestableFromIsr(Requestable *rq);
		void run();
		TimerSource &operator|(TimerSource &ts);
		void *id();
		static void *currentId();
};
// not sure these extra inheritance are useful
template <class T> class Source : public Requestable {
		std::vector<void *> _observers;

	protected:
		uint32_t size() { return _observers.size(); }
		Observer<T> *operator[](uint32_t idx) {
			return static_cast<Observer<T> *>(_observers[idx]);
		}
		Thread *_observerThread = 0;

	public:
		virtual void subscribe(Observer<T> &observer) {
			_observers.push_back((void *)&observer);
		}

		void emit(const T &t) {
			if ((_observerThread == 0) ||
			        (_observerThread && _observerThread->id() == Thread::currentId()))
				for (void *pv : _observers) {
					Observer<T> *pObserver = static_cast<Observer<T> *>(pv);
					pObserver->onNext(t);
				} else
				_observerThread->awakeRequestable(this);
		}
		Source<T> &observeOn(Thread &thread) {
			_observerThread = &thread;
			return *this;
		}
		Thread *observerThread() { return _observerThread; }
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
//_________________________________________CompositeFlow_______________________________
//
template <class IN, class INTERM, class OUT>
class CompositeFlow : public Flow<IN, OUT> {
		Flow<IN, INTERM> &_in;
		Flow<INTERM, OUT> &_out;

	public:
		CompositeFlow(Flow<IN, INTERM> &a, Flow<INTERM, OUT> &b)
			: Flow<IN, OUT>(a, b), _in(a), _out(b) {};
		void request() { _in.request(); };
		void onNext(const IN &in) { _in.onNext(in); }
		void subscribe(Observer<OUT> &observer) { _out.subscribe(observer); }
};
//______________________________________________________________________________________
//
template <class IN, class INTERM, class OUT>
Flow<IN, OUT> &operator>>(Flow<IN, INTERM> &flow1, Flow<INTERM, OUT> &flow2) {
	Flow<IN, OUT> *cflow = new CompositeFlow<IN, INTERM, OUT>(flow1, flow2);
	flow1.subscribe(flow2);
	return *cflow;
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

template <class OUT>
Requestable &operator>>(Source<OUT> &source, Sink<OUT> &sink) {
	source.subscribe(sink);
	return source;
};

template <class T>
Flow<T, T> &operator==(Flow<T, T> &flow1, Flow<T, T> &flow2) {
	flow1.subscribe(flow2);
	flow2.subscribe(flow1);
	return flow1;
};

//______________________________________________________________________________
//
//______________________________________________________________________________
//

template <class T> class ValueFlow : public Flow<T, T> {
		T _value;
		bool _emitOnChange = true;

	public:
		ValueFlow() {}
		ValueFlow(T x) : Flow<T, T>(), _value(x) {};
		void request() { this->emit(_value); }
		void onNext(const T &value) {
			if (_emitOnChange) {
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
		void onNext(const T &event) { _handler(event); };
};
//________________________________________________________________________
//
template <class T> class LambdaSource : public Source<T> {
		std::function<T()> _handler;

	public:
		LambdaSource(std::function<T()> handler) : _handler(handler) {};
		void request() { this->emit(_handler()); }
};
//______________________________________________________________________________
//
template <class IN, class OUT> class LambdaFlow : public Flow<IN, OUT> {
		std::function<OUT(IN)> _handler;

	public:
		LambdaFlow() {};
		LambdaFlow(std::function<OUT(IN)> handler) : _handler(handler) {};
		void handler(std::function<OUT(IN)> handler) { _handler = handler; };
		void onNext(const IN &event) {
			OUT out = _handler(event);
			this->emit(out);
		};
		void request() {};
};
//__________________________________________________________________________`
//
// filter values on comparison
// if value equals filter condition it's emitted
//
//__________________________________________________________________________
template <class T> class Filter : public Flow<T, T> {
		T _value;

	public:
		Filter(T value) { _value = value; }
		void onNext(T &in) {
			if (in == _value)
				this->emit(in);
		}
};
//__________________________________________________________________________`
//
// MedianFilter : calculates median of N samples
// it will only emit after enough samples
// x : number of samples
//
//__________________________________________________________________________
#include <MedianFilter.h>

template <class T, int x> class Median : public Flow<T, T> {
		MedianFilter<T, x> _mf;

	public:
		Median() {};
		void onNext(const T &value) {
			_mf.addSample(value);
			if (_mf.isReady()) {
				this->emit(_mf.getMedian());
			}
		};
		void request() { this->emit(_mf.getMedian()); }
};

//__________________________________________________________________________`
//
// Router : uses round robin to ditribute values to consumers
// can be used as load-balancing or sequential invocation
//
//__________________________________________________________________________

template <class T> class Router : public Flow<T, T> {
		uint32_t _idx;

	public:
		void onNext(T &t) {
			_idx++;
			if (_idx > this->size())
				_idx = 0;
			(*this)[_idx]->onNext(t);
		}
};

//__________________________________________________________________________`
//
// Atomic Source : emit sequential counter independent of concurrency
//
//__________________________________________________________________________
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
//__________________________________________________________________________`
//
// Throttle : limits the number of emits per second
// it compares the new and old value, if it didn't change it's not forwarded
// delta : after which time the value is forwarded
//
//__________________________________________________________________________
template <class T> class Throttle : public Flow<T, T> {
		uint32_t _delta;
		uint64_t _nextEmit;
		T _lastValue;

	public:
		Throttle(uint32_t delta) {
			_delta = delta;
			_nextEmit = Sys::millis() + _delta;
		}
		void onNext(const T &value) {
			uint64_t now = Sys::millis();
			if (now > _nextEmit) {
				this->emit(value);
				_nextEmit = now + _delta;
			}
			_lastValue = value;
		}
		void request() { this->emit(_lastValue); };
};
//__________________________________________________________________________`
//
// TimerSource
// id : the timer id send with the timer expiration
// interval : time after which the timer expires
// repeat : repetitive timer
//
// run : sink bool to stop or run timer
//	start : restart timer from now+interval
//__________________________________________________________________________
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
		ValueFlow<bool> running = true;
		TimerSource(int id, uint32_t interval, bool repeat) {
			_id = id;
			_interval = interval;
			_repeat = repeat;
			start();
		}
		void start() {
			running = true;
			_expireTime = Sys::millis() + _interval;
		}
		void stop() { running = false; }
		void interval(uint32_t i) { _interval = i; }
		void request() {
			//       INFO("[%X] %d request() ",this,_id);
			if (running()) {
				if (Sys::millis() >= _expireTime) {
					//                INFO("[%X]:%d:%llu timer emit
					//                ",this,interval(),expireTime());
					_expireTime += _interval;
					this->emit({_id});
				}
			} else {
				WARN(" timer not running ");
			}
		}
		uint64_t expireTime() { return _expireTime; }
		inline uint32_t interval() { return _interval; }
		void subscribeOn(Thread &thread) { thread.addTimer(this); }
		void observeOn(Thread &thread) { thread.addTimer(this); }
};
//__________________________________________________________________________`
//
// single value async passed across threads
//
//
//__________________________________________________________________________

template <class T> class AsyncValueFlow : public Flow<T, T> {
		T _value;

	public:
		void onNext(T &value) {
			_value = value;
			if (this->observerThread())
				this->observerThread()->awakeRequestable(this);
		}
		void request() { this->emit(_value); }
};
//__________________________________________________________________________`
//
// Buffered flow , to connect 2 threads
// size : max number of values buffered
//
//
//__________________________________________________________________________
#ifdef FREERTOS

template <class T> class AsyncFlow : public Flow<T, T> {
		std::deque<T> _buffer;
		uint32_t _queueDepth;
		SemaphoreHandle_t xSemaphore = NULL;

	public:
		LambdaSink<T> fromIsr;
		AsyncFlow(uint32_t size) : _queueDepth(size) {
			xSemaphore = xSemaphoreCreateBinary();
			xSemaphoreGive(xSemaphore);
			fromIsr.handler([&](T value) { onNextFromIsr(value); });
		}
		void onNext(const T &event) {
			if (xSemaphoreTake(xSemaphore, (TickType_t)10) == pdTRUE) {
				if (_buffer.size() >= _queueDepth) {
					_buffer.pop_front();
					//					WARN(" buffer overflow in
					// BufferedSink
					//");
				}
				_buffer.push_back(event);
				xSemaphoreGive(xSemaphore);
				if (this->observerThread())
					this->observerThread()->awakeRequestable(this);
				return;
			} else {
				WARN(" timeout on async buffer ! ");
			}
		}
		void onNextFromIsr(T &event) { // ATTENTION !! no logging from Isr
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
				if (this->observerThread())
					this->observerThread()->awakeRequestableFromIsr(this);
				return;
			} else {
				//           WARN(" timeout on async buffer ! "); // no log from ISR
			}
		}

		void request() {
			while (true) {
				bool hasData = false;
				if (xSemaphoreTake(xSemaphore, (TickType_t)10) == pdTRUE) {
					T t;
					if (_buffer.size()) {
						t = _buffer.front();
						_buffer.pop_front();
						hasData = true;
					}
					xSemaphoreGive(xSemaphore);
					if (hasData)
						this->emit(t);
				} else {
					WARN(" timeout on async buffer ! ");
				}
				if (!hasData)
					break;
			}
		}
};
#endif

#ifdef ARDUINO

template <class T> class AsyncFlow : public Flow<T, T> {
		std::deque<T> _buffer;
		uint32_t _queueDepth;

	public:
		LambdaSink<T> fromIsr;

		AsyncFlow(uint32_t size) : _queueDepth(size) {
			fromIsr.handler([&](T value) { onNextFromIsr(value); });
		}
		void onNext(const T &event) {
			noInterrupts();
			if (_buffer.size() >= _queueDepth) {
				_buffer.pop_front();
				//					WARN(" buffer overflow in
				// BufferedSink ");
			}
			_buffer.push_back(event);
			interrupts();
		}

		void onNextFromIsr(T &event) {
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
};
#endif
//__________________________________________________________________________`
//
// calculates moving average
// samples : number of samples on which average is calculated
// timeout : after which time the value is forwarded
//
//__________________________________________________________________________
template <class T> class MovingAverage : public Flow<T, T> {
		double _ratio;
		std::list<T> _samples;
		uint64_t _expTime;
		uint32_t _interval;
		uint32_t _sampleCount;

	public:
		MovingAverage(uint32_t samples, uint32_t timeout) {
			_sampleCount = samples;
			_interval = timeout;
		};
		T average() {
			T sum = 0;
			uint32_t count = _samples.size();
			for (auto it = _samples.begin(); it != _samples.end(); it++) {
				sum += *it;
			}
			if (count == 0)
				return 0;
			return sum / count;
		}
		void onNext(const T &value) {
			if (_samples.size() > _sampleCount)
				_samples.pop_back();
			_samples.push_front(value);
			if (Sys::millis() > _expTime) {
				_expTime = Sys::millis() + _interval;
				this->emit(average());
			}
		}
		void request() { this->emit(average()); }
};
//__________________________________________________________________________`
//
// filter doubles with previous values
// ratio : 0...1 value to indicate percentage of new value is taken
// timeout : after which time the value is forwarded
//
//__________________________________________________________________________

template <class T> class ExponentialFilter : public Flow<T, T> {
		T _ratio;
		T _lastValue;
		T _total;

	public:
		ExponentialFilter(T ratio, T total) {
			_ratio = ratio;
			_total = total;
			_lastValue = 0;
		};
		void onNext(const T &value) {
			_lastValue = ((_total - _ratio) * _lastValue + _ratio * value) / _total;
			this->emit(_lastValue);
		}
		void request() { this->emit(_lastValue); }
};
//__________________________________________________________________________`
//
// emits defaultValue if no new value arrived within timeout
// defaultValue : value pushed
// timeout : after which time the value is forwarded, if no other arrive
//
//__________________________________________________________________________
template <class T> class TimeoutFlow : public Flow<T, T> {
		T _defaultValue;

	public:
		TimerSource timer;
		TimeoutFlow(uint32_t timeout, T defaultValue)
			: _defaultValue(defaultValue), timer(1, timeout, true) {
			timer >> *new LambdaSink<TimerMsg>(
			[&](TimerMsg tm) { this->emit(_defaultValue); });
		}
		void onNext(const T &t) {
			this->emit(t);
			timer.start();
		};
		void request() { this->emit(_defaultValue); };
};

class ConfigStore {
	public:
		static void init();
		bool load(const char *name, void *value, uint32_t length);
		bool save(const char *name, void *value, uint32_t length);
		bool load(const char *name, std::string &value);
		bool save(const char *name, std::string &value);
};

template <class T> class ConfigFlow : public Flow<T, T>, public ConfigStore {
		std::string _name;
		T _value;

	public:
		ConfigFlow(const char *name, T defaultValue)
			: _name(name), _value(defaultValue) {
			_value = defaultValue;
			init();
			if (load(_name.c_str(), &_value, sizeof(T))) {
				INFO(" Config load %s : %f", _name.c_str(), _value);
			} else {
				INFO(" Config default %s : %f", _name.c_str(), _value);
			}
		}

		void onNext(const T &value) {
			_value = value;
			save(_name.c_str(), &_value, sizeof(T));
			request();
		}
		void request() { this->emit(_value); }
		inline void operator=(T value) { onNext(value); };
		inline T operator()() { return _value; }
};

template <class T>
class TimeSerie : public Sink<T> {
		std::list<T> _values;
	public :
		TimeSerie(uint32_t count,uint32_t interval) ;
		Source<T>& max();
		Source<T>& min();
		Source<T>& avg();
		Source<T>& median();
		Source<T>& integral();
		Source<T>& differential();
		Source<uint32_t>& count();
};

#endif // STREAMS_H
