#include "freertos/ringbuf.h"

// create a task that reads/writes from stm32 main serial (bootloader enabled)
// and puts all the bytes in a ringbuffer.
void create_stm32_serial_task(RingbufHandle_t rx_buffer, RingbufHandle_t tx_buffer);