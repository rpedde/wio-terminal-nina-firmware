#include "src/nina/nina_spi_proof.h"

void setup()
{
    // Phase 2: no Serial2, eRPC, BLE, or network workers. Vendor boot logging
    // uses the dedicated LOG UART, never the SPI pins. Failure leaves READY high.
    nina_transport_start(nina_spi_proof_reply);
}

void loop()
{
    delay(1000);
}
