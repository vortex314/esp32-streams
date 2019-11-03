#ifndef PROTOTHREAD_H
#define PROTOTHREAD_H
#define ARDUINOJSON_ENABLE_PROGMEM 0
#include <ArduinoJson.h>
#undef min
#undef max
#include <Streams.h>
#include <atomic>
#include <vector>


//_______________________________________________________________________________________________________________
//
class Timer {
	private:
		uint32_t _delta;
		uint64_t _timeout;
		bool _repeat;
		bool _actif;

	public:
		Timer(uint32_t delta, bool repeat, bool actif);
		bool isRepeating();
		void repeat(bool rep);
		void start();
		void stop();
		bool timeout();
		void timeout(uint32_t delay);
};

//_______________________________________________________________________________________________________________
//
class Coroutine {
		typedef unsigned short LineNumber;
		static std::vector<Coroutine *> *_pts;
		static const LineNumber LineNumberInvalid = (LineNumber)(-1);
		static std::vector<Coroutine *> *pts();

		uint64_t _timeout;
		Timer _defaultTimer;
		std::string _name;

		std::atomic<uint32_t> _bits;

	public:
		LineNumber _ptLine;
		Coroutine(const char* name);
		const char* name();
		virtual ~Coroutine();
		bool timeout();
		void timeout(uint32_t delay);
		virtual void loop() = 0;
		virtual void setup() = 0;
		static void setupAll();
		static void loopAll();
		void restart();
		void stop();
		bool isRunning();
		bool isReady();
		bool setBits(uint32_t bits);
		bool clrBits(uint32_t bits);
		bool hasBits(uint32_t bits);
};

// Declare start of protothread (use at start of Run() implementation).
#define PT_BEGIN()       \
	bool ptYielded = true; \
	switch (_ptLine) {     \
	case 0:

// Stop protothread and end it (use at end of Run() implementation).
#define PT_END() \
	default:;      \
	}            \
	stop();      \
	return;

// Cause protothread to wait until given condition is true.
#define PT_WAIT_UNTIL(condition)     \
	do {                               \
		_ptLine = __LINE__;              \
	case __LINE__:                   \
		if (!(condition)) return true; \
	} while (0)

// Cause protothread to wait while given condition is true.
#define PT_WAIT_WHILE(condition) PT_WAIT_UNTIL(!(condition))

// Cause protothread to wait until given child protothread completes.
#define PT_WAIT_THREAD(child) PT_WAIT_WHILE((child).dispatch(msg))

// Restart and spawn given child protothread and wait until it completes.
#define PT_SPAWN(child)    \
	do {                     \
		(child).restart();     \
		PT_WAIT_THREAD(child); \
	} while (0)

// Restart protothread's execution at its PT_BEGIN.
#define PT_RESTART() \
	do {               \
		restart();       \
		return true;     \
	} while (0)

// Stop and exit from protothread.
#define PT_EXIT() \
	do {            \
		stop();       \
		return false; \
	} while (0)

// Yield protothread till next call to its Run().
#define PT_YIELD()                 \
	do {                             \
		ptYielded = false;             \
		_ptLine = __LINE__;            \
	case __LINE__:                 \
		if (!ptYielded) return true; \
	} while (0)

// Yield protothread until given condition is true.
#define PT_YIELD_UNTIL(condition)             \
	do {                                        \
		ptYielded = false;                        \
		_ptLine = __LINE__;                       \
	case __LINE__:                            \
		if (!ptYielded || !(condition)) return; \
	} while (0)
//_______________________________________________________________________________________________________________
//
#endif
