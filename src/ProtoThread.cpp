#include "ProtoThread.h"

//_______________________________________________________________________________________________________________
//


Timer::Timer(uint32_t delta, bool repeat, bool actif) {
  _delta = delta;
  _repeat = repeat;
  _actif = actif;
}

bool Timer::isRepeating() { return _repeat; }
void Timer::repeat(bool rep) { _repeat = rep; }
void Timer::start() {
  _timeout = Sys::millis() + _delta;
  _actif = true;
}

void Timer::stop() { _actif = false; }

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
std::vector<ProtoThread *> *ProtoThread::_pts;

std::vector<ProtoThread *> *ProtoThread::pts() {
  if (ProtoThread::_pts == 0) {
    ProtoThread::_pts = new std::vector<ProtoThread *>();
  }
  return ProtoThread::_pts;
}

ProtoThread::ProtoThread(const char* name) : _defaultTimer(1, false, false), _ptLine(0) {
  //      LOG(" new protoThread");
  _bits = 0;
  pts()->push_back(this);
}

ProtoThread::~ProtoThread() {}
bool ProtoThread::timeout() { return _defaultTimer.timeout(); }
void ProtoThread::timeout(uint32_t delay) {
  if (delay == 0)
    _defaultTimer.stop();
  else {
    _defaultTimer.timeout(delay);
  }
}

void ProtoThread::setupAll() {
  INFO(" found  %d protothreads."," + String(pts()->size()) + ");
  for (ProtoThread *pt : *pts()) {
    pt->setup();
  }
}

void ProtoThread::loopAll() {
  for (ProtoThread *pt : *pts()) {
  uint32_t startTime=Sys::millis();
    pt->loop();
    if ( (Sys::millis()-startTime)>10) {
      WARN(" slow protothread %s : %d msec.",pt->name(),Sys::millis()-startTime);
    }
  }
}

const char* ProtoThread::name() {
  return _name.c_str();
}
void ProtoThread::restart() { _ptLine = 0; }
void ProtoThread::stop() { _ptLine = LineNumberInvalid; }
bool ProtoThread::isRunning() { return _ptLine != LineNumberInvalid; }
bool ProtoThread::isReady() { return _ptLine == LineNumberInvalid; }

bool ProtoThread::setBits(uint32_t bits) {
  uint32_t expected = _bits;
  uint32_t desired = _bits | bits;
  return _bits.compare_exchange_strong(expected, desired);
}

bool ProtoThread::clrBits(uint32_t bits) {
  uint32_t expected = _bits;
  uint32_t desired = _bits & bits;
  return _bits.compare_exchange_strong(expected, desired);
}

bool ProtoThread::hasBits(uint32_t bits) { return (_bits & bits); }
//_______________________________________________________________________________________________________________
//

//_______________________________________________________________________________________________________________
//
