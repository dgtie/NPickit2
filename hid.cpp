#include <xc.h>
#include "usb_config.h"
#include "usb.h"

/* Class-Specific Requests */
#define GET_REPORT      0x01
#define GET_IDLE        0x02
#define GET_PROTOCOL    0x03
#define SET_REPORT      0x09
#define SET_IDLE        0x0A
#define SET_PROTOCOL    0x0B

void bd_fill(int index, char *buf, int size, int stat);

extern char inbuffer[], outbuffer[];

int bd_out, bd_in;
char idle_rate;
char active_protocol;               // [0] Boot Protocol [1] Report Protocol


bool HIDReportTxd(void) { return bd_in; }
bool HIDReportRxd(void) { return bd_out; }

void HIDRxReport(void) {
    int bd = bd_out ^ 1;
    bd_out = 0;
    bd_fill(bd, inbuffer, USB_EP1_BUFF_SIZE, bd & 1 ? 0xc0 : 0x80);
}

void HIDTxReport(unsigned char *buf) {
    int bd = bd_in ^ 1;
    bd_in = 0;
    bd_fill(bd, (char*)buf, USB_EP1_BUFF_SIZE, bd & 1 ? 0xc0 : 0x80);
}

void ClassInitEndpoint(int config) {
    if (config) {
        U1EP1 = 0x1d;   // EPCONDIS, EPRXEN, EPTXEN, EPHSHK
        bd_out = 5; bd_in = 7;
        HIDRxReport();
    }
}

void Class_TRN_Handler(int) {
    int bd = U1STAT >> 2;
    switch (bd) {
        case 4: // interrupt out
        case 5: bd_out = bd; break;
        case 6: // interrupt in
        case 7: bd_in = bd; break;
        default:;
    }
}

char *ClassTrfSetupHandler(setup_packet *SetupPkt) {
    switch (SetupPkt->request) {
        case GET_REPORT: return outbuffer;
        case SET_REPORT: return inbuffer;
        case SET_IDLE: idle_rate = SetupPkt->value << 8;
        case GET_IDLE: return &idle_rate;
        case SET_PROTOCOL: active_protocol = SetupPkt->value;
        case GET_PROTOCOL: return &active_protocol;
        default: return 0;
    }
}

void ClassCtrlTrfSetupComplete(setup_packet*) {}
