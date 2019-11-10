#include "RotaryEncoder.h"
/*
 * CAPTURE :
 * Rotary sensor generates 400 pulses per rotation
 * Capture generates an interrupt per 10 pulses
 * The capture counter works at 80 M pulses per second
 *
 * 10 rpm => 6000 pulses per min  => 600 isr per min => 10 isr per sec
 *
 *  delta_time = (capture / 80M)*10 => time in sec for 1 rotation
 *
 * rpm = (1/delta_time)*60
 *
 * rpm = (80M/capture)/10*60 = ( 80M/capture )*6
 *
 *
 *
 * */

#define CAPTURE_FREQ 80000000
#define PULSE_PER_ROTATION 400
#define CAPTURE_DIVIDER 1

#define CAP0_INT_EN BIT(27) // Capture 0 interrupt bit
#define CAP1_INT_EN BIT(28) // Capture 1 interrupt bit
#define CAP2_INT_EN BIT(29) // Capture 2 interrupt bit
static mcpwm_dev_t* MCPWM[2] = {&MCPWM0, &MCPWM1};


RotaryEncoder::RotaryEncoder(uint32_t pinTachoA, uint32_t pinTachoB)
    : _pinTachoA(pinTachoA), _dInTachoB(DigitalIn::create(pinTachoB)),_captures(20)
{
    _isrCounter = 0;
    _mcpwm_num=MCPWM_UNIT_0;
    _timer_num=MCPWM_TIMER_0;
    _captures >> * new LambdaSink<CaptureMsg>([=](CaptureMsg m) {
        onNext(m);
    });
}

RotaryEncoder::~RotaryEncoder() {}


void RotaryEncoder::setPwmUnit(uint32_t unit)
{
    if ( unit==0 ) {
        _mcpwm_num = MCPWM_UNIT_0;
    } else if ( unit ==1 ) {
        _mcpwm_num = MCPWM_UNIT_1;
    }
}
// capture signal on falling edge, prescale = 0 i.e.
// 80,000,000 counts is equal to one second
// Enable interrupt on  CAP0, CAP1 and CAP2 signal
void RotaryEncoder::init()
{
    INFO(" rotaryEncoder PWM[%d] capture : GPIO_%d direction : GPIO_%d ",_mcpwm_num,_pinTachoA,_dInTachoB.getPin());
    for (uint32_t i = 0; i < MAX_SAMPLES; i++)
        _samples[i] = 0;
    _dInTachoB.init();
    esp_err_t rc;
    rc = mcpwm_gpio_init(_mcpwm_num, MCPWM_CAP_0, _pinTachoA);
    if ( rc !=ESP_OK) {
        WARN("mcpwm_gpio_init()=%d");
    }

    rc = mcpwm_capture_enable(_mcpwm_num, MCPWM_SELECT_CAP0, MCPWM_NEG_EDGE,
                              CAPTURE_DIVIDER);
    if ( rc !=ESP_OK) {
        WARN("mcpwm_capture_enable()=%d");
    }
    MCPWM[_mcpwm_num]->int_ena.val = CAP0_INT_EN;
    // Set ISR Handler
    rc = mcpwm_isr_register(_mcpwm_num, isrHandler, this, ESP_INTR_FLAG_IRAM,
                            NULL);
    if (rc) {
        WARN("mcpwm_capture_enable()=%d", rc);
    }
}


const uint32_t weight = 10;

void IRAM_ATTR
RotaryEncoder::isrHandler(void* pv) // ATTENTION no float calculations in ISR
{
    RotaryEncoder* re = (RotaryEncoder*)pv;
    uint32_t mcpwm_intr_status;
    // check encoder B when encoder A has isr,
    // indicates phase or rotation direction
    int b = re->_dInTachoB.read();
    re->_direction = (b == 1) ? (0 - re->_directionSign) : re->_directionSign;

    mcpwm_intr_status = MCPWM[re->_mcpwm_num]->int_st.val; // Read interrupt
    // status
    re->_isrCounter++;
    // Check for interrupt on rising edge on CAP0 signal
    if (mcpwm_intr_status & CAP0_INT_EN) {
        uint32_t capt = mcpwm_capture_signal_get_value(
                            re->_mcpwm_num,
                            MCPWM_SELECT_CAP0); // get capture signal counter value
        re->_captures.onNextFromIsr({capt,Sys::millis()});
    }
    MCPWM[re->_mcpwm_num]->int_clr.val = mcpwm_intr_status;
}

void RotaryEncoder::onNext(CaptureMsg cm)
{
    _delta =cm.capture - _prevCapture;
    int32_t rpm = deltaToRpm(_delta,_direction);
//    INFO(" rpm %d , delta %d , time %llu ",rpm,_delta,cm.time);
    _prevCapture = cm.capture;
    _prevCaptureTime = cm.time;
    emit(rpm);
}

void RotaryEncoder::request()
{
    _captures.request();
}


int32_t RotaryEncoder::deltaToRpm(uint32_t delta, int32_t direction)
{
    float t = (60.0 * CAPTURE_FREQ * CAPTURE_DIVIDER) /
              (delta * PULSE_PER_ROTATION);
    int32_t rpm = ((int32_t)t) * _direction;
    return rpm;
}
