#include "Neo6m.h"

Neo6m::Neo6m(Connector* connector)
	: Coroutine("NEO-6M"),_connector(connector),_uart(connector->getUART()) {
}

Neo6m::~Neo6m() {
}


void Neo6m::setup() {
	_uart.setClock(9600);
	_uart.onRxd(onRxd,this);
	_uart.init();
}

void Neo6m::loop() {
}

void Neo6m::onRxd(void* me) {
	((Neo6m*) me)->handleRxd();
}

void Neo6m::handleRxd() {

	while ( _uart.hasData() ) {
		char ch = _uart.read();
		if ( ch=='\n' || ch=='\r') {
			if ( _line.size()>8 ) { // cannot use msgBuilder as out of thread
				std::string topic="neo6m/";
				topic+=_line.substr(1,5);
				emit({topic,_line.substr(6)});
			}
			_line.clear();
		} else {
			_line +=ch;
		}
	}
}