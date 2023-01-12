#ifndef _USB_H    /* Guard against multiple inclusion */
#define _USB_H

#define PID_SETUP 13                // PID type
#define PID_IN 9
#define PID_OUT 1

#define TYPE_STANDARD 0              // type
#define TYPE_CLASS 1
#define TYPE_VENDOR 2

#define CLEAR_FEATURE 1         // std Request
#define GET_CONFIGURATION 8
#define GET_DESCRIPTOR 6
#define GET_INTERFACE 10
#define GET_STATUS 0
#define SET_ADDRESS 5
#define SET_CONFIGURATION 9
#define SET_DESCRIPTOR 7
#define SET_FEATURE 3
#define SET_INTERFACE 11
#define SYNCH_FRAME 12

typedef struct {
        unsigned int recipient:5;
        unsigned int type:2;
        unsigned int direction:1;
        char request;
        unsigned short value;
        unsigned short index;
        unsigned short length;
} setup_packet;

#endif /* _USB_H */
