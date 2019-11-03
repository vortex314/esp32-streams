#include "coroutine.h"

//_______________________________________________________________________________________________________________
//


Timer::Timer(uint32_t delta, bool repeat, bool actif) {
	_delta = delta;
	_repeat = repeat;
	_actif = actif;
}

bool Timer::isRepeating() {
	return _repeat;
}
void Timer::repeat(bool rep) {
	_repeat = rep;
}
void Timer::start() {
	_timeout = Sys::millis() + _delta;
	_actif = true;
}

void Timer::stop() {
	_actif = false;
}

bool Timer::timeout() {
	if (_actif)
		return Sys::millis() > _timeout;
	return false;
}

void Timer::timeout(uint32_t delay) {
	_timeout = Sys::millis() + delay;
	_actif = true;
}

//_______________________________________________________________________________________________________________
//
// to avoid the problem that static objects are created in disorder, it is
// created when used.
//
std::vector<Coroutine *> *Coroutine::_pts;

std::vector<Coroutine *> *Coroutine::pts() {
	if (Coroutine::_pts == 0) {
		Coroutine::_pts = new std::vector<Coroutine *>();
	}
	return Coroutine::_pts;
}

Coroutine::Coroutine(const char* name) : _defaultTimer(1, false, false), _ptLine(0) {
	//      LOG(" new protoThread");
	_name=name;
	_bits = 0;
	pts()->push_back(this);
}

Coroutine::~Coroutine() {}
bool Coroutine::timeout() {
	return _defaultTimer.timeout();
}
void Coroutine::timeout(uint32_t delay) {
	if (delay == 0)
		_defaultTimer.stop();
	else {
		_defaultTimer.timeout(delay);
	}
}

void Coroutine::setupAll() {
	INFO(" found  %d protothreads.",pts()->size());
	for (Coroutine *pt : *pts()) {
		INFO(" setup protohread : %s",pt->name());
		pt->setup();
	}
}

void Coroutine::loopAll() {
	for (Coroutine *pt : *pts()) {
		uint32_t startTime=Sys::millis();
		pt->loop();
		uint32_t delta = Sys::millis()-startTime;
		if ( delta >10) {
			WARN(" slow protothread %s : %d msec.",pt->name(),delta);
		}
	}
}

const char* Coroutine::name() {
	return _name.c_str();
}
void Coroutine::restart() {
	_ptLine = 0;
}
void Coroutine::stop() {
	_ptLine = LineNumberInvalid;
}
bool Coroutine::isRunning() {
	return _ptLine != LineNumberInvalid;
}
bool Coroutine::isReady() {
	return _ptLine == LineNumberInvalid;
}

bool Coroutine::setBits(uint32_t bits) {
	uint32_t expected = _bits;
	uint32_t desired = _bits | bits;
	return _bits.compare_exchange_strong(expected, desired);
}

bool Coroutine::clrBits(uint32_t bits) {
	uint32_t expected = _bits;
	uint32_t desired = _bits & bits;
	return _bits.compare_exchange_strong(expected, desired);
}

bool Coroutine::hasBits(uint32_t bits) {
	return (_bits & bits);
}
//_______________________________________________________________________________________________________________
//

//_______________________________________________________________________________________________________________
//
