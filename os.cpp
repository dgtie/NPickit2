#include <xc.h>                    // Device specific definitions

/*** DEVCFG0 ***/
#pragma config DEBUG =      OFF
#pragma config JTAGEN =     OFF
#pragma config ICESEL = ICS_PGx1   // ICE/ICD Comm Channel Select
#pragma config PWP =        OFF
#pragma config BWP =        OFF
#pragma config CP =         OFF

/*** DEVCFG1 ***/
#pragma config FNOSC =      PRIPLL
#pragma config FPBDIV =     DIV_1
#pragma config FSOSCEN =    OFF
#pragma config IESO =       ON
#pragma config POSCMOD =    XT
#pragma config OSCIOFNC =   OFF
#pragma config FCKSM =      CSDCMD
#pragma config WDTPS =      PS1048576
#pragma config FWDTEN =     OFF
#pragma config WINDIS =     OFF
#pragma config FWDTWINSZ =  WINSZ_25

/*** DEVCFG2 ***/
#pragma config FPLLIDIV =   DIV_2    // 8 MHz -> 4 MHz
#pragma config FPLLMUL =    MUL_20   // 80 MHz
#pragma config FPLLODIV =   DIV_2    // 40 MHz System Clock
#pragma config UPLLEN =     ON
#pragma config UPLLIDIV =   DIV_2

/*** DEVCFG3 ***/
#pragma config FVBUSONIO =  ON
#pragma config USERID =     0xFFFF
#pragma config PMDL1WAY =   OFF
#pragma config IOL1WAY =    OFF
#pragma config FUSBIDIO =   OFF

#define MS 20000

void init(void) {
    __builtin_disable_interrupts();
    _CP0_SET_COMPARE(MS);
    IPC0bits.CTIP = 1;
    IEC0bits.CTIE = 1;
    INTCONSET = _INTCON_MVEC_MASK;
    __builtin_enable_interrupts();
}

void poll(unsigned);

namespace {
    
    volatile unsigned tick;
    
    void loop(unsigned t) {
        static unsigned tick;
        if (tick == t) return;
        poll(tick = t);
    }
    
}//anonymous

bool wait(unsigned i) {
    unsigned u = tick;
    for (loop(u); i--; u = tick) while (u == tick) loop(u);
    return true;
}

extern "C"
void __attribute__((interrupt(ipl1soft), vector(_CORE_TIMER_VECTOR), nomips16))
ctISR(void) {
    _CP0_SET_COMPARE(_CP0_GET_COMPARE() + MS);
    tick++;
    IFS0bits.CTIF = 0;
}

unsigned getTimeMilli(void) { return tick; }
