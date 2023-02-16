#include <xc.h>
#include "pickit.h"

bool HIDReportRxd(void), HIDReportTxd(void);
void HIDRxReport(void);
void HIDTxReport(unsigned char *buf);
void wait(unsigned i);

// RC0 - VPP
// RB2 - PGC
// RB3 - PGD
// RB8 - SWITCH
// RB9 - BUSY LED

// MCLR 3V3 GND PGD PGC
// C0           B3  B2

#define BUSY_LED        LATBbits.LATB9
#define PROG_SWITCH_pin PORTBbits.RB8
#define MCLR_TGT_pin    LATCbits.LATC0
#define Vpp_ON_pin      !TRISCbits.TRISC0

#define P32SetMode(bits, mode) jtag2w4ph(mode, 0, 1 << (bits - 1))
#define P32SendCommand(command) jtag2w4ph(0x303, command << 4, 0x400)
#define P32XferData8(data) (jtag2w4ph(0xc01, data << 3, 0x1000) >> 2)

unsigned getTimeMilli(void);

unsigned char inbuffer[BUF_SIZE];            	 // input to USB device buffer
unsigned char outbuffer[BUF_SIZE];            	 // output to USB device buffer

namespace
{

unsigned char icsp_baud, icsp_pins;

unsigned char uc_download_buffer[DOWNLOAD_SIZE]; // Download Data Buffer
unsigned char uc_upload_buffer[UPLOAD_SIZE];     // Upload Data Buffer

union {		// Status bits
    unsigned short Status;
	struct{
		// StatusLow
		unsigned VddGNDOn:1;	// bit 0
		unsigned VddOn:1;
		unsigned VppGNDOn:1;
		unsigned VppOn:1;
		unsigned VddError:1;
		unsigned VppError:1;
        unsigned ButtonPressed:1;
		unsigned :1;
		//StatusHigh
        unsigned Reset:1;       // bit 0
		unsigned UARTMode:1;			
        unsigned ICDTimeOut:1;
		unsigned UpLoadFull:1;
		unsigned DownloadEmpty:1;
		unsigned EmptyScript:1;
		unsigned ScriptBufOvrFlow:1;
		unsigned DownloadOvrFlow:1;
	};
    enum { RESETMASK=0x103, ERRMASK=0xFE00 };
} Pk2Status;

class RingBufferManager {
public:
    RingBufferManager(unsigned char *b, int s): buffer(b), size(s)
    { clearBuffer(); };
    void clearBuffer(void) { read_index = write_index = 0; }
    unsigned char readByte(void) {
        unsigned char c;
        if (read_index == write_index)
        { Pk2Status.DownloadEmpty = 1; return 0; }
        c = buffer[read_index++];
        if (read_index == size) read_index = 0;
        return c;
    }
    int read2buffer(unsigned char *buf, int max) {
        int count = 0;
        while ((read_index != write_index) && (count < max))
            buf[count++] = readByte();
        return count;
    }
    unsigned readInt(void) {
        unsigned i = 0;
        for (int j = 0; j < 32; j += 8) i += (readByte() << j);
        return i;
    }
    void writeByte(unsigned char c) {
        if (Pk2Status.UpLoadFull) return;
        buffer[write_index++] = c;
        if (write_index == size) write_index = 0;
        if (read_index == write_index) Pk2Status.UpLoadFull = 1;
    }
    unsigned char *writeBuffer(unsigned char *src) {
        int count = *src++;
        while (count--) writeByte(*src++);
        return src;
    }
    void writeInt(unsigned i) {
        for (int j = 0; j < 32; j += 8) writeByte(i >> j);
    }
private:
    unsigned char *buffer;
    int size, read_index, write_index;
};
RingBufferManager ucDownloadBuffer(uc_download_buffer, DOWNLOAD_SIZE);
RingBufferManager ucUploadBuffer(uc_upload_buffer, UPLOAD_SIZE);

void ShiftByteOutICSP(unsigned byte) {
    LATBbits.LATB3 = byte & 1 ? 1 : 0;
    TRISBCLR = 0xc;     // PGD & PGC as output
    byte ^= byte << 1;
    byte |= 0x100;
    while ((byte >>= 1) != 1) {
        LATBSET = 4;                    // CLK high
        LATBINV = byte & 1 ? 12 : 4;    // CLK low
    }
    LATBSET = 4;                    // CLK high
    asm("nop");
    LATBCLR = 12;                   // CLK low
    TRISBSET = 8;     // PGD as input (KEEP PGC as output)
}

unsigned jtag2w4ph(unsigned TMS, unsigned TDI, unsigned TDO) {
    unsigned mark = TDO;
    TMS ^= TDI;
    LATBCLR = 0xc;
    while (TDO) {
        TRISBCLR = 0xc;                 // PGD & PGC as output
        LATBINV = TDI & 1 ? 12 : 4;     // CLK high
        TDI >>= 1;
        LATBCLR = 4;                    // CLK low
        LATBINV = TMS & 1 ? 12 : 4;     // CLK high
        TMS >>= 1;
        LATBCLR = 4;                   // CLK low
        TRISBSET = 8;                   // PGD as input
        LATBSET = 4;                    // CLK high
        TDO >>= 1;        
        LATBCLR = 4;                    // CLK low
        asm("nop");
        LATBSET = 4;                    // CLK high
        if (PORTB & 8) TDI |= mark;      // read PORT
        LATBCLR = 12;                    // CLK low
    }
    TRISBSET = 8;                 // PGD as input (KEEP PGC as output)
    return TDI;
}

unsigned P32XferData32(unsigned data){
    unsigned lower = data & 0xffff;
    unsigned upper = data >> 16;
    lower = jtag2w4ph(1, lower << 3, 0x40000);
    upper = jtag2w4ph(0x18000, upper, 0x20000);
    return (lower >> 2) | (upper << 17);
}

unsigned P32XferFastData32(unsigned data) {
    unsigned lower = data & 0xffff;
    unsigned upper = data >> 16;
    lower = jtag2w4ph(1, lower << 4, 0x80000);
    if (!(lower & 4)) {
        P32SetMode(5, 0x1f);
        Pk2Status.ICDTimeOut = 1;
        return 0;
    }
    upper = jtag2w4ph(0x18000, upper, 0x20000);
    return (lower >> 3) | (upper << 17);
}

unsigned P32XferInstruction(unsigned ins) {
    unsigned response;
    unsigned t = getTimeMilli() + 1400;
    P32SendCommand(0x0A);           // ETAP_CONTROL
    while (!(P32XferData32(0x4d000) & 0x40000)) {
        wait(0);
        if (getTimeMilli() == t) {
            Pk2Status.ICDTimeOut = 1;
            return 0;          
        }
    }
    P32SendCommand(0x09);           // ETAP_DATA
    response = P32XferData32(ins);
    P32SendCommand(0x0A);           // ETAP_CONTROL
    P32XferData32(0xc000);
    return response;
}

unsigned P32GetPEResponse(void) {
    unsigned response;
    unsigned t = getTimeMilli() + 1400;
    P32SendCommand(0x0A);           // ETAP_CONTROL
    while (!(P32XferData32(0x4d000) & 0x40000)) {
        wait(0);
        if (getTimeMilli() == t) {
            Pk2Status.ICDTimeOut = 1;
            return 0;          
        }
    }
    P32SendCommand(0x09);           // ETAP_DATA
    response = P32XferData32(0);
    P32SendCommand(0x0A);           // ETAP_CONTROL
    P32XferData32(0xc000);
    return response;
}

///////////////////////////////////////////////////////////////////
///   SCRIPT ENGINE
///   abort - VPP_PWM_OFF, VDD_OFF, VDD_GND_ON, VPP_PWM_ON
    
unsigned char *jt2_sendcmd(unsigned char *p) {
    P32SendCommand(*++p);
    return ++p;
}

unsigned char *jt2_xferdata32_lit(unsigned char *p) {
    unsigned i = 0;
    for (int j = 0; j < 32; j += 8) i += (*++p) << j;
    ucUploadBuffer.writeInt(P32XferData32(i));
    return ++p;
}

unsigned char *jt2_xferdata8_lit(unsigned char *p) {
    ucUploadBuffer.writeByte(P32XferData8(*++p));
    return ++p;
}

unsigned char *jt2_setmode(unsigned char *p) {
    int bits = *++p;
    P32SetMode(bits, *++p);
    return ++p;
}

unsigned char *jt2_xferinst_buf(unsigned char *p) {
    P32XferInstruction(ucDownloadBuffer.readInt());
    return ++p;
}

unsigned char *jt2_xfrfastdat_buf(unsigned char *p) {
    P32XferFastData32(ucDownloadBuffer.readInt());
    return ++p;
}

unsigned char *jt2_xfrfastdat_lit(unsigned char *p) {
    unsigned i = 0;
    for (int j = 0; j < 32; j += 8) i += (*++p) << j;
    P32XferFastData32(i);
    return ++p;
}

unsigned char *jt2_get_pe_resp(unsigned char *p) {
    ucUploadBuffer.writeInt(P32GetPEResponse());
    return ++p;
}

unsigned char *jt2_wait_pe_resp(unsigned char *p) {
    P32GetPEResponse();
    return ++p;
}

unsigned char *vpp_off(unsigned char *p) {
    TRISCbits.TRISC0 = 1;
    return ++p;
}

unsigned char *vpp_on(unsigned char *p) {
    TRISCbits.TRISC0 = 0;
    return ++p;
}

unsigned char *mclr_gnd_on(unsigned char *p) {
    LATCbits.LATC0 = 0;
    TRISCbits.TRISC0 = 0;
    return ++p;
}

unsigned char *mclr_gnd_off(unsigned char *p) {
    LATCbits.LATC0 = 1;
    return ++p;
}

unsigned char *set_icsp_pins(unsigned char *p) {
    icsp_pins = *++p;
    LATBbits.LATB2 = icsp_pins & 4 ? 1 : 0;     // PGC logic
    LATBbits.LATB3 = icsp_pins & 8 ? 1 : 0;     // PGD logic
    TRISBbits.TRISB2 = icsp_pins & 1 ? 1 : 0;   // PGC dir
    TRISBbits.TRISB3 = icsp_pins & 2 ? 1 : 0;   // PGD dir
    return ++p;
}

void delay_64(unsigned t) {     // t * 6.4 us
    TMR1 = 0;
    T1CONbits.ON = 1;
    while (TMR1 < t) wait(0);
    T1CONbits.ON = 0;    
}

unsigned char *delay_short(unsigned char *p) {
    delay_64(*++p * 7);          // 6.4us * 7 = 44.8us (3)
    return ++p;
}

unsigned char *delay_long(unsigned char *p) {
    delay_64(*++p * 853);       // 6.4us * 781 = 4998us 853
    return ++p;
}

unsigned char *busy_led_off(unsigned char *p) {
    BUSY_LED = 0;
    return ++p;
}

unsigned char *busy_led_on(unsigned char *p) {
    BUSY_LED = 1;
    return ++p;
}

unsigned char *loop(unsigned char *p) {
    static unsigned char *loopindex;
    static int loopcount = 0;
    if (loopcount) {
        wait(0);
        if (!--loopcount) loopindex = p + 3;
    } else {
        loopindex = p - p[1];
        loopcount = p[2];
    }
    return loopindex;
}

unsigned char *write_byte_literal(unsigned char *p) {
    ShiftByteOutICSP(*++p);
    return ++p;
}

unsigned char *set_icsp_speed(unsigned char *p) {
    icsp_baud = *++p;
    return ++p;
}

unsigned char *nop(unsigned char *p) { return ++p; }

unsigned char *abort(unsigned char*) { return 0; }

typedef unsigned char* (*sf)(unsigned char*);

const sf script[] = {
abort, // JT2_PE_PROG_RESP
jt2_wait_pe_resp, // JT2_WAIT_PE_RESP
jt2_get_pe_resp, // JT2_GET_PE_RESP
jt2_xferinst_buf, // JT2_XFERINST_BUF
jt2_xfrfastdat_buf, // JT2_XFRFASTDAT_BUF
jt2_xfrfastdat_lit, // JT2_XFRFASTDAT_LIT
jt2_xferdata32_lit, // JT2_XFERDATA32_LIT
jt2_xferdata8_lit, // JT2_XFERDATA8_LIT
jt2_sendcmd, // JT2_SENDCMD
jt2_setmode, // JT2_SETMODE
abort, // UNIO_TX_RX
abort, // UNIO_TX
abort, // MEASURE_PULSE
abort, // ICDSLAVE_TX_BUF_BL
abort, // ICDSLAVE_TX_LIT_BL
abort, // ICDSLAVE_RX_BL
abort, // SPI_RDWR_BYTE_BUF
abort, // SPI_RDWR_BYTE_LIT
abort, // SPI_RD_BYTE_BUF
abort, // SPI_WR_BYTE_BUF
abort, // SPI_WR_BYTE_LIT
abort, // I2C_RD_BYTE_NACK
abort, // I2C_RD_BYTE_ACK
abort, // I2C_WR_BYTE_BUF
abort, // I2C_WR_BYTE_LIT
abort, // I2C_STOP
abort, // I2C_START
abort, // AUX_STATE_BUFFER
abort, // SET_AUX
abort, // WRITE_BITS_BUF_HLD
abort, // WRITE_BITS_LIT_HLD
abort, // CONST_WRITE_DL  
abort, // WRITE_BUFBYTE_W 
abort, // WRITE_BUFWORD_W 
abort, // RD2_BITS_BUFFER 
abort, // RD2_BYTE_BUFFER 
abort, // VISI24
abort, // abort24
abort, // COREINST24
abort, // COREINST18
abort, // POP_DOWNLOAD
abort, // ICSP_STATES_BUFFER
abort, // LOOPBUFFER
abort, // ICDSLAVE_TX_BUF 
abort, // ICDSLAVE_TX_LIT 
abort, // ICDSLAVE_RX
abort, // POKE_SFR
abort, // PEEK_SFR
abort, // EXIT_SCRIPT
abort, // GOTO_INDEX
abort, // IF_GT_GOTO
abort, // IF_EQ_GOTO
delay_short, // DELAY_SHORT
delay_long, // DELAY_LONG
loop, // LOOP
set_icsp_speed, // SET_ICSP_SPEED
abort, // READ_BITS
abort, // READ_BITS_BUFFER
abort, // WRITE_BITS_BUFFER
abort, // WRITE_BITS_LITERAL
abort, // READ_BYTE
abort, // READ_BYTE_BUFFER
abort, // WRITE_BYTE_BUFFER
write_byte_literal, // WRITE_BYTE_LITERAL
set_icsp_pins, // SET_ICSP_PINS
busy_led_off, // BUSY_LED_OFF
busy_led_on, // BUSY_LED_ON
mclr_gnd_off, // MCLR_GND_OFF
mclr_gnd_on, // MCLR_GND_ON
nop, // VPP_PWM_OFF
nop, // VPP_PWM_ON
vpp_off, // VPP_OFF
vpp_on, // VPP_ON
abort, // VDD_GND_OFF
nop, // VDD_GND_ON
nop // VDD_OFF   
};

unsigned char *scriptEngine(unsigned char *ptr, unsigned len) {
    unsigned char *end = ptr + len;
    int index;
    while ((ptr) && (ptr < end)) {
        index = *ptr - SCRIPT_JT2_PE_PROG_RESP;
        ptr = index < 0 ? 0 : (*script[index])(ptr);
    }
    return ptr;
}

void SendStatusUSB(void) {
    while (!HIDReportTxd()) wait(0);
    Pk2Status.Status &= 0xFFF3;    // clear bits to be tested
    if (Vpp_ON_pin)         // active high
        Pk2Status.VppOn = 1;
    if (!MCLR_TGT_pin)       // active high
        Pk2Status.VppGNDOn = 1;

	outbuffer[0] = Pk2Status.Status & 0xff;
	outbuffer[1] = Pk2Status.Status >> 8;

    // Now that it's in the USB buffer, clear errors & flags
    Pk2Status.Status &= 0x8F;
    BUSY_LED = 0;                   // ensure it stops blinking at off.

    // transmit status
    HIDTxReport(outbuffer);
} // end void SendStatusUSB(void)

} // anonymous namespace

void ProcessIO(void) {
    unsigned char *ptr = inbuffer;
    unsigned temp;
    if (!PROG_SWITCH_pin)   // active low
        Pk2Status.ButtonPressed = 1;
    if (HIDReportRxd()) {
        while ((ptr) && (ptr < (inbuffer + 64)))
            switch ((int)*ptr) {
                case CMD_EXECUTE_SCRIPT:
                    temp = *++ptr;
                    ptr = scriptEngine(++ptr, temp);
                    break;
                case CMD_CLEAR_DOWNLOAD_BUFFER:
                    ucDownloadBuffer.clearBuffer();
                    ptr++; break;
                case CMD_CLEAR_UPLOAD_BUFFER:
                    ucUploadBuffer.clearBuffer();
                    ptr++; break;
                case CMD_DOWNLOAD_DATA:
                    ptr = ucDownloadBuffer.writeBuffer(++ptr);
                    break;
                case CMD_UPLOAD_DATA:
                    while (!HIDReportTxd()) wait(0);
                    *outbuffer = ucUploadBuffer.read2buffer(outbuffer + 1, 63);
                    HIDTxReport(outbuffer); ptr++; break;
                case CMD_UPLOAD_DATA_NOLEN:
                    while (!HIDReportTxd()) wait(0);
                    ucUploadBuffer.read2buffer(outbuffer, 64);
                    HIDTxReport(outbuffer); ptr++; break; 
                case CMD_GET_VERSION:
                    while (!HIDReportTxd()) wait(0);
                    outbuffer[0] = MAJORVERSION;
                    outbuffer[1] = MINORVERSION;
                    outbuffer[2] = DOTVERSION;
                    HIDTxReport(outbuffer); ptr++; break;                     
                case CMD_READ_STATUS:
                    SendStatusUSB();
                case CMD_NO_OPERATION: ptr++; break;
                case CMD_SET_VDD:
                case CMD_SET_VPP: ptr += 4; break;
                default: ptr = 0;
            }
        HIDRxReport();
    }
}

void pickit_init(void) {
    T1CONbits.TCKPS = 3;            // prescalar = 256
    ANSELBCLR = 0xc;                // B2,B3
    ANSELCbits.ANSC0 = 0;
    LATCSET = TRISCSET = 1;         // VPP (C0)
    TRISBSET = 4;                   // PGC (B2) default input
    TRISBCLR = LATBCLR = 0x200;     // BUSY_LED (B9)
    CNPUBbits.CNPUB8 = 1;           // SW pull up
	icsp_pins = 0x03;		// default inputs
	icsp_baud = 0x00;		// default fastest
    Pk2Status.Status = Pk2Status.RESETMASK;
}

unsigned test(unsigned data) {
    return P32XferFastData32(data);
}

void led(bool b) { BUSY_LED = b ? 1 : 0; }

int key;

void button(unsigned t) {
    if ((!(t & 15)) && (key != (PORTB & 0x100))) {
        key ^= 0x100;
        if (!key) LATBINV = 0x200;
    }
}