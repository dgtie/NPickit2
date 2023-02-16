#include "pickit.h"

bool wait(unsigned i);
void USBDeviceInit(void), init(void), led(bool), button(unsigned);

int main(void) {
    init();
    pickit_init();
    led(true);
    wait(1000);
    led(false);
    USBDeviceInit();
    while (wait(0)) ProcessIO();
}

void poll(unsigned t) {
    button(t);
}