#include "usb_config.h"

#define USB_DESCRIPTOR_DEVICE 1
#define USB_DESCRIPTOR_CONFIGURATION 2
#define USB_DESCRIPTOR_STRING 3
#define USB_DESCRIPTOR_INTERFACE 4
#define USB_DESCRIPTOR_ENDPOINT 5

#define DSC_HID 0x21
#define DSC_RPT 0x22

#define HID_INTF 0x03

#define _DEFAULT 0x80

#define _INTERRUPT 3
#define _OUT 0
#define _IN 0x80

#define DEVICE 0x0100
#define CONFIG1 0x0200
#define CONFIG2 0x0201
#define STRING 0x0300
#define STRING1 0x0301
#define STRING2 0x0302
#define HID 0x2100
#define RPT 0x2200

typedef struct __attribute__((packed)) {
    char bLength;
    char bDscType;
    short bcdHID;
    char bCountryCode;
    char bNumDsc;
    char bDscType0;
    short wDscLength;
} USB_HID_DSC;

typedef struct __attribute__((packed)) {
    char bLength;
    char bDescriptorType;
    short wTotalLength;
    char bNumInterface;
    char bConfigurationValue;
    char iConfiguration;
    unsigned char bmAttributes;
    char bMaxPower;
} CONFIGURATION_DESCRIPTOR;

typedef struct {
    char bLength;
    char bDescriptorType;
    char bInterfaceNumber;
    char bAlternateSetting;
    char bNumEndpoints;
    unsigned char bInterfaceClass;
    char bInterfaceSubClass;
    char bInterfaceProtocol;
    char iInterface;
} INTERFACE_DESCRIPTOR;

typedef struct __attribute__((packed)) {
    char bLength;
    char bDescriptorType;
    unsigned char bEndpointAddress;
    char bmAttributes;
    short wMaxPacketSize;
    unsigned char bInterval;
} ENDPOINT_DESCRIPTOR;

namespace
{

const struct {
    char bLength;
    char bDescriptorType;
    short bcdUSB;
    char bDeviceClass;
    char bDeviceSubClass;
    char bDeviceProtocol;
    char bMaxPacketSize0;
    short idVendor;
    short idProduct;
    short bcdDevice;
    char iManufacture;
    char iProduct;
    char iSerialNumber;
    char bNumConfigurations;
} device_descriptor = {
    0x12,                   // Size of this descriptor in bytes
    USB_DESCRIPTOR_DEVICE,  // DEVICE descriptor type
    0x0200,                 // USB Spec Release Number in BCD format
    0x00,                   // Class Code
    0x00,                   // Subclass code
    0x00,                   // Protocol code
    USB_EP0_BUFF_SIZE,      // Max packet size for EP0, see usb_config.h
    0x04d8,                 // Vendor ID: MICROCHIP
    0x0033,                 // Product ID: PICKIT2
    0x0002,                 // Device release number in BCD format
    0x01,                   // Manufacturer string index
    0x02,                   // Product string index
    0x00,                   // Device serial number string index
    0x02                    // Number of possible configurations
};

const struct {
    unsigned char report[29];
} hid_rpt01 = {
    0x06, 0x00, 0xFF,       // Usage Page (Vendor Defined)
    0x09, 0x01,             // Usage (Vendor Usage)
    0xA1, 0x01,             // Collection (Application)
    0x19, 0x01,             //      Usage Minimum (Vendor Usage = 1)
//    0x29, 0x08,             //      Usage Maximum (Vendor Usage = 8)
    0x29, 0x40,             //      Usage Maximum (Vendor Usage = 64)
    0x15, 0x00,             //      Logical Minimum (Vendor Usage = 0)
    0x26, 0xFF, 0x00,       //      Logical Maximum (Vendor Usage = 255)
    0x75, 0x08,             //      Report Size (Vendor Usage = 8)
//    0x95, 0x08,             //      Report Count (Vendor Usage = 8)
    0x95, 0x40,             //      Report Count (Vendor Usage = 64)
    0x81, 0x02,             //      Input (Data, Var, Abs)
    0x19, 0x01,             //      Usage Minimum (Vendor Usage = 1)
//    0x29, 0x08,             //      Usage Maximum (Vendor Usage = 8)
    0x29, 0x40,             //      Usage Maximum (Vendor Usage = 64)
    0x91, 0x02,             //      Output (Data, Var, Ads)
    0xC0                  // End Collection
};

const struct __attribute__((packed)) {
    CONFIGURATION_DESCRIPTOR    cd01;
    INTERFACE_DESCRIPTOR        i00a00;
    USB_HID_DSC                 hid_i00a00;
    ENDPOINT_DESCRIPTOR         ep01i_i00a00;
    ENDPOINT_DESCRIPTOR         ep01o_i00a00;
} cfg01 = {
    {
        9,//sizeof(USB_CFG_DSC),// Size of this descriptor in bytes
        USB_DESCRIPTOR_CONFIGURATION,                // CONFIGURATION descriptor type
        sizeof(cfg01),          // Total length of data for this cfg   67, 62
        1,                      // Number of interfaces in this cfg
        1,                      // Index value of this configuration
        2,                      // Configuration string index
        _DEFAULT,               // Attributes, see usb_device.h
        50                     // Max power consumption (2X mA)
    },
    {
        9,//sizeof(USB_INTF_DSC),   // Size of this descriptor in bytes
        USB_DESCRIPTOR_INTERFACE,               // INTERFACE descriptor type
        0,                      // Interface Number
        0,                      // Alternate Setting Number
        2,                      // Number of endpoints in this intf
        HID_INTF,              // Class code
        0,                      // Subclass code
        0,                      // Protocol code
        0                      // Interface string index
    },
    {
        sizeof(USB_HID_DSC),    // Size of this descriptor in bytes
        DSC_HID,                // HID descriptor type
        0x0001,                 // HID Spec Release Number in BCD format
        0x00,                   // Country Code (0x00 for Not supported)
        1,                      // Number of class descriptors, see usbcfg.h
        DSC_RPT,                // Report descriptor type
        sizeof(hid_rpt01),      // Size of the report descriptor
    },
    {
        7, /*sizeof(USB_EP_DSC)*/
        USB_DESCRIPTOR_ENDPOINT,   //Endpoint Descriptor
        1 | _IN,                   //EndpointAddress
        _INTERRUPT,                //Attributes
        USB_EP1_BUFF_SIZE,              //size
        1                        //Interval
    },
    {
        7, /*sizeof(USB_EP_DSC)*/
        USB_DESCRIPTOR_ENDPOINT,   //Endpoint Descriptor
        1 | _OUT,                   //EndpointAddress
        _INTERRUPT,                //Attributes
        USB_EP1_BUFF_SIZE,              //size
        1                        //Interval
    }
};

const struct __attribute__((packed)) {
    CONFIGURATION_DESCRIPTOR    cd01;
    INTERFACE_DESCRIPTOR        i00a00;
    ENDPOINT_DESCRIPTOR         ep01i_i00a00;
    ENDPOINT_DESCRIPTOR         ep01o_i00a00;
} cfg02 = {
    {
        9,//sizeof(USB_CFG_DSC),// Size of this descriptor in bytes
        USB_DESCRIPTOR_CONFIGURATION,                // CONFIGURATION descriptor type
        sizeof(cfg02),          // Total length of data for this cfg   67, 62
        1,                      // Number of interfaces in this cfg
        1,                      // Index value of this configuration
        0,                      // Configuration string index
        _DEFAULT,               // Attributes, see usb_device.h
        50                     // Max power consumption (2X mA)
    },
    {
        9,//sizeof(USB_INTF_DSC),   // Size of this descriptor in bytes
        USB_DESCRIPTOR_INTERFACE,               // INTERFACE descriptor type
        0,                      // Interface Number
        0,                      // Alternate Setting Number
        2,                      // Number of endpoints in this intf
        0xFF,              // Class code
        0,                      // Subclass code
        0,                      // Protocol code
        0                      // Interface string index
    },
    {
        7, /*sizeof(USB_EP_DSC)*/
        USB_DESCRIPTOR_ENDPOINT,   //Endpoint Descriptor
        1 | _IN,                   //EndpointAddress
        _INTERRUPT,                //Attributes
        USB_EP1_BUFF_SIZE,              //size
        1                        //Interval
    },
    {
        7, /*sizeof(USB_EP_DSC)*/
        USB_DESCRIPTOR_ENDPOINT,   //Endpoint Descriptor
        1 | _OUT,                   //EndpointAddress
        _INTERRUPT,                //Attributes
        USB_EP1_BUFF_SIZE,              //size
        1                        //Interval
    }    
};

const struct {
    char bLength;
    char bDescriptorType;
    short string[1];
} sd000 = {
    sizeof(sd000),
    USB_DESCRIPTOR_STRING,
    { 0x0409 }
};

const struct {
    char bLength;
    char bDescriptorType;
    short string[25];
} sd001 = {
    sizeof(sd001),
    USB_DESCRIPTOR_STRING,
    {
        'M','i','c','r','o','c','h','i','p',' ',
        'T','e','c','h','n','o','l','o','g','y',
        ' ','I','n','c','.'
    }
};

const struct {
    char bLength;
    char bDescriptorType;
    short string[35];
} sd002 = {
    sizeof(sd002),
    USB_DESCRIPTOR_STRING,
    {
        'P','I','C','k','i','t',' ','2',' ','M',
        'i','c','r','o','c','o','n','t','r','o',
        'l','l','e','r',' ','P','r','o','g','r',
        'a','m','m','e','r'
    }
};

} // anonymous

char *get_std_descriptor(short type) {
    switch (type) {
        case DEVICE: return (char*)&device_descriptor;
        case CONFIG1: return (char*)&cfg01;
        case CONFIG2: return (char*)&cfg01;
        case STRING: return (char*)&sd000;
        case STRING1: return (char*)&sd001;
        case STRING2: return (char*)&sd002;
        case HID: return (char*)&cfg01.hid_i00a00;
        case RPT: return (char*)&hid_rpt01;
        default: return 0;
    }
}