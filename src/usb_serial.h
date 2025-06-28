#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "driver/usb_serial_jtag.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

// create a task that reads/writes from usb-serial-jtag and puts all bytes in ring buffer.
void create_usb_serial_task(RingbufHandle_t rx_buffer, RingbufHandle_t tx_buffer);