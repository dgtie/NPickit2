#include <xc.h>
#include "usb.h"
#include "usb_config.h"

#define STAT_PPBI 4
#define STAT_DIR 8
#define STAT_ENDPT 0xf0

char *get_std_descriptor(short type);
void ClassInitEndpoint(int config);
void Class_TRN_Handler(int length);
char *ClassTrfSetupHandler(setup_packet*);
void ClassCtrlTrfSetupComplete(setup_packet*);

void bd_fill(int index, char *buf, int size, int stat);

typedef struct {
    union {
        struct {
            union {
                char val;
                struct __attribute__((packed)) {
                    unsigned int non:2;
                    unsigned int pid:4;
                    unsigned int data:1;
                    unsigned int uown:1;                
                };
            } stat;
            unsigned char nothing;
            unsigned short bytecount;
        };
        unsigned all;
    };
    char *buf_ptr;
} BDT;

namespace {
    
    BDT __attribute__((aligned(512))) bdt[USB_EP_COUNT << 2];
    setup_packet SetupPkt;
    short pp0out, pp0in;
    int USBActiveConfiguration;

    char *virt2phy(char* adr) { return (char*)(((unsigned)adr) & 0x1fffffff); } 
    char *phy2virt(char* adr) { return (char*)(((unsigned)adr) | 0xa0000000); }
    
    void prepare_for_setup(void) {
        U1EP0 = 0xd;                    // EPRXEN, EPTXEN, EPHSHK
        bd_fill(pp0out, (char*)&SetupPkt, USB_EP0_BUFF_SIZE, 0x84);
        //                                                   UOWN, BSTALL
    }
    
    void reset_non_zero_endpoint(void) {
        U1CONbits.PPBRST = 1;
        U1EP1 = U1EP2 = 0;
        for (int i = 0; i < USB_EP_COUNT << 2; i++) bdt[i].all = 0;
        pp0out = 0; pp0in = 2;
        U1CONbits.PPBRST = 0;        
    }
    
    void reset(void) {
        IEC1bits.USBIE = 0;
        U1ADDR = U1EIR = U1IR = U1EP0 = 0;
        reset_non_zero_endpoint();
        prepare_for_setup();
        U1CONbits.PKTDIS = 0;
        IEC1bits.USBIE = 1;        
    }
    
    bool USBCtrlTrfSetupHandler(char* &phy_buffer) {
        if (SetupPkt.type == TYPE_STANDARD) {
            switch (SetupPkt.request) {
                case GET_DESCRIPTOR:
                    if ((phy_buffer = virt2phy(get_std_descriptor(SetupPkt.value))))
                        return true;
                    break;
                case SET_CONFIGURATION:
                case SET_ADDRESS: return true;
                default: return false;
            }
        }
        if (SetupPkt.type == TYPE_CLASS)
            return phy_buffer = virt2phy(ClassTrfSetupHandler(&SetupPkt));
        return false;
    }
    
    void USBCtrlTrfSetupComplete(void) {
        if (SetupPkt.type == TYPE_STANDARD) {
            switch (SetupPkt.request) {
                case SET_ADDRESS: U1ADDR = SetupPkt.value; break;
                case SET_CONFIGURATION:
                    reset_non_zero_endpoint();
                    ClassInitEndpoint(USBActiveConfiguration = SetupPkt.value);
                    break;
                default:;
            }
        }
        if (SetupPkt.type == TYPE_CLASS) ClassCtrlTrfSetupComplete(&SetupPkt);
        prepare_for_setup();
    }
    
    void TRN_EP0_Handler(void) {
        BDT *bd = &bdt[U1STAT >> 2];
        char *phy_buffer;
        int stall;
        if (U1STAT & STAT_DIR) pp0in = U1STAT & STAT_PPBI ? 2 : 3;
        else pp0out = U1STAT & STAT_PPBI ? 0 : 1;
        switch (bd->stat.pid) {
            case PID_SETUP:
                stall = USBCtrlTrfSetupHandler(phy_buffer) ? 0 : 4;
                bdt[pp0in].buf_ptr = bdt[pp0out].buf_ptr = phy_buffer;
                bdt[pp0out].bytecount = USB_EP0_BUFF_SIZE;
                bdt[pp0in].bytecount = SetupPkt.direction ?
                    (
                        SetupPkt.length > USB_EP0_BUFF_SIZE ?
                            USB_EP0_BUFF_SIZE : SetupPkt.length
                    ) : 0;
                bdt[pp0in].stat.val = bdt[pp0out].stat.val = 0xc0 | stall;    // UOWN, DATA 
                U1CONbits.PKTDIS = 0;
                break;
            case PID_IN:
                if (SetupPkt.direction) {
                    if (SetupPkt.length -= bd->bytecount) {
                        bdt[pp0in].buf_ptr = bd->buf_ptr + bd->bytecount;
                        bdt[pp0in].bytecount = SetupPkt.length > USB_EP0_BUFF_SIZE ?
                            USB_EP0_BUFF_SIZE : SetupPkt.length;
                        bdt[pp0in].stat.val = bd->stat.data ? 0x80 : 0xc0;
                    }
                } else USBCtrlTrfSetupComplete();
                break;
            case PID_OUT:
                if (SetupPkt.direction) USBCtrlTrfSetupComplete();
                else {
                    if (SetupPkt.length -= bd->bytecount) {
                        bdt[pp0out].buf_ptr = bd->buf_ptr + bd->bytecount;
                        bdt[pp0out].bytecount = USB_EP0_BUFF_SIZE;
                        bdt[pp0out].stat.val = bd->stat.data ? 0x80 : 0xc0;
                    }                    
                }
                break;
            default:;
        }
    }
    
}

void USBDeviceInit(void) {
    while (U1PWRCbits.USBBUSY);	// wait for USB module
    U1PWRCbits.USBPWR = 1;		// power on USB
    unsigned address = (unsigned)virt2phy((char*)&bdt);
    U1BDTP1 = address >> 8;
    U1BDTP2 = address >> 16;
    U1BDTP3 = address >> 24;
    IPC7bits.USBIP = 1;
    IEC1bits.USBIE = 1;
    U1IE = 0x89;                // STALLIF, TRNIF, URSTIF
    U1CONbits.USBEN = 1;
}

void bd_fill(int index, char *buf, int size, int stat) {
    bdt[index].buf_ptr = virt2phy(buf);
    bdt[index].bytecount = size;
    bdt[index].stat.val = stat;
}

extern "C"
void __attribute__((interrupt(ipl1soft), vector(_USB_1_VECTOR), nomips16))
_USB1Interrupt(void){
    if (U1IRbits.STALLIF) {
        prepare_for_setup();
        U1IRbits.STALLIF = 1;
    }
    if (U1IRbits.URSTIF) {
        reset();
        U1IRbits.URSTIF = 1;
    }
    while (U1IRbits.TRNIF) {
        if (U1STAT & STAT_ENDPT) Class_TRN_Handler(bdt[U1STAT >> 2].bytecount);
        else TRN_EP0_Handler();
        U1IRbits.TRNIF = 1;
    }
    IFS1bits.USBIF = 0;
}