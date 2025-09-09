#include "usb_serial.h"

#define BUF_SIZE (1024)
#define STACK_SIZE (4096)

static void usb_rx_task(void *pvParameters)
{
    RingbufHandle_t ringbuf = (RingbufHandle_t)pvParameters;

    // Configure a temporary buffer for the incoming data
    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);
    if (data == NULL) {
        ESP_LOGE("usb rx", "no memory for data");
        return;
    }

    while (1) {
        int len = usb_serial_jtag_read_bytes(data, BUF_SIZE, pdMS_TO_TICKS(20));
        if (len) {
            // ESP_LOGE("usb rx", "%d bytes in", len);

            UBaseType_t res = xRingbufferSend(ringbuf, data, len, pdMS_TO_TICKS(1000));
            if (res != pdTRUE) {
                ESP_LOGE("usb rx", "Failed to send item");
            }
        }
    }
}

static void usb_tx_task(void *pvParameters) {
    RingbufHandle_t ringbuf = (RingbufHandle_t)pvParameters;

    while (1) {
        //Receive data from byte buffer
        size_t item_size;
        char *data = (char *)xRingbufferReceiveUpTo(ringbuf, &item_size, pdMS_TO_TICKS(1000), 1000);

        //Check received data
        if (data != NULL) {
            // ESP_LOGI("usb_tx", "write %d bytes to tx", item_size);

            usb_serial_jtag_write_bytes((const char *) data, item_size, 20 / portTICK_PERIOD_MS);

            //Return Item
            vRingbufferReturnItem(ringbuf, (void *)data);
        } else {
            //Failed to receive item
            // printf("Failed to receive item\n");
        }
    }
}

void create_usb_serial_task(RingbufHandle_t rx_buffer, RingbufHandle_t tx_buffer)
{
    // Configure USB SERIAL JTAG
    usb_serial_jtag_driver_config_t usb_serial_jtag_config = {
        .tx_buffer_size = BUF_SIZE,
        .rx_buffer_size = BUF_SIZE,
    };

    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_serial_jtag_config));
    ESP_LOGI("usb_serial_jtag echo", "USB_SERIAL_JTAG init done");

    xTaskCreate(usb_rx_task, "USB rx", STACK_SIZE, (void*)rx_buffer, 10, NULL);
    xTaskCreate(usb_tx_task, "USB tx", STACK_SIZE, (void*)tx_buffer, 10, NULL);
}
