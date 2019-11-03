
#include <deque>
#include <functional>
#include <stdint.h>
#include <vector>
#ifndef STREAMS_H
#define STREAMS_H
#include <Log.h>

//______________________________________________________________________________
//
template <class T> class AbstractSink
{
public:
    virtual void recv(T event) = 0;
};
//______________________________________________________________________________
//
template <typename T> class AbstractSource
{
public:
    virtual void emit(T event) = 0;
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
    void recv(T event)
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
    void addSink(AbstractSink<T> *_sink)
    {
        _sinks.push_back(_sink);
    }
    void operator>>(std::function<void(T)> handler)
    {
        addSink(new HandlerSink<T>(handler));
    };
    void operator>>(AbstractSink<T> &sink)
    {
        addSink(&sink);
    }
    void emit(T event)
    {
        for (AbstractSink<T> *sink : _sinks) {
            sink->recv(event);
        }
    };
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
    source.addSink(&flow);
    return flow;
};

template <class IN, class OUT>
Source<OUT> &operator>>(Source<IN> &source, Flow<IN, OUT> *flow)
{
    source.addSink(flow);
    return *flow;
};
//______________________________________________________________________________
//
template <class T> class BufferedSink : public AbstractSink<T>
{
    std::deque<T> _buffer;
    uint32_t _queueDepth;

public:
    BufferedSink(uint32_t size) : _queueDepth(size) {}
    void recv(T event)
    {
        if (_buffer.size() >= _queueDepth) {
            _buffer.pop_front();
            WARN(" buffer overflow in BufferedSink ");
        }
        _buffer.push_back(event);
    };
    void getNext(T& t)
    {
        t = _buffer.front();
        _buffer.pop_front();
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
    void recv(T value)
    {
        _value=value;
    }
    inline T get()
    {
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
public:
    ValueSource(T x)
    {
        _value=x;
    };
    void pub()
    {
        this->emit(_value);
    }
    inline void operator=(T value)
    {
        _value=value;
    };
    T get()
    {
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
    inline T get()
    {
        return _value;
    }
    inline void operator=(T value)
    {
        _value=value;
    }
    inline void recv(T value)
    {
        _value=value;
    }
    inline   void pub()
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
    void recv(T value)
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


#endif
