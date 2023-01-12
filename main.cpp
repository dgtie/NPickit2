#include "pickit.h"

bool wait(unsigned i);
void USBDeviceInit(void), init(void);

int main(void) {
    init();
    pickit_init();
    wait(5000);
    USBDeviceInit();
    while (wait(0)) ProcessIO();
}

void poll(unsigned) {
    
}