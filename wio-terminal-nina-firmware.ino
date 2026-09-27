#include "src/nina/nina_server.h"

void setup()
{
    // Vendor boot logging uses the dedicated LOG UART, never the SPI pins.
    // Initialize station networking and start the NINA protocol task.
    nina_server_start();
}

void loop()
{
    delay(1000);
}
