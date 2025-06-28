#include "uart.h"

#include <stdio.h>
#include <algorithm>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "soc/uart_struct.h"
#include "soc/uart_reg.h"
#include "sdkconfig.h"
#include "esp_log.h"

#define BUF_SIZE (256)
#define STACK_SIZE (4096 * 2)

// #define ECHO_TEST_TXD 18
// #define ECHO_TEST_RXD 17
#define ECHO_TEST_TXD 43
#define ECHO_TEST_RXD 44

#define ECHO_UART_PORT_NUM      UART_NUM_0
#define ECHO_UART_BAUD_RATE     115200

static const char *TAG = "uart_events";
static QueueHandle_t uart_queue;


// static void serial_rx_task(void *pvParameters)
// {
//     RingbufHandle_t ringbuf = (RingbufHandle_t)pvParameters;

//     // Configure a temporary buffer for the incoming data
//     uint8_t *data = (uint8_t *) malloc(BUF_SIZE);
//     if (data == NULL) {
//         ESP_LOGE("uart rx", "no memory for data");
//         return;
//     }

//     while (1) {
//         // Read data from UART
//         size_t len = 0;
//         ESP_ERROR_CHECK(uart_get_buffered_data_len(ECHO_UART_PORT_NUM, &len));
//         len = std::min(len, (size_t)BUF_SIZE);
//         len = std::max(len, (size_t)1);
//         len = uart_read_bytes(ECHO_UART_PORT_NUM, data, len, pdMS_TO_TICKS(20));

//         // int len = uart_read_bytes(ECHO_UART_PORT_NUM, data, BUF_SIZE, pdMS_TO_TICKS(100));
//         if (len) {
//             // ESP_LOGE("uart rx", "%d bytes in", len);

//             UBaseType_t res = xRingbufferSend(ringbuf, data, len, pdMS_TO_TICKS(1000));
//             if (res != pdTRUE) {
//                 printf("Failed to send item\n");
//             }
//         }
//     }
// }



static void uart_rx_task(void *pvParameters)
{
    RingbufHandle_t ringbuf = (RingbufHandle_t)pvParameters;

    uart_event_t event;
    UBaseType_t res;
    uint8_t* dtmp = (uint8_t*) malloc(BUF_SIZE);
    for (;;) {
        if (xQueueReceive(uart_queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
            bzero(dtmp, BUF_SIZE);
            switch (event.type) {
            case UART_DATA:
                TRY_STATIC_ASSERT(event.size <= BUF_SIZE, "UART RX BUF too small");
                // ESP_LOGI(TAG, "[UART DATA]: %d %i", event.size, event.timeout_flag);
                uart_read_bytes(ECHO_UART_PORT_NUM, dtmp, event.size, portMAX_DELAY);
                res = xRingbufferSend(ringbuf, dtmp, event.size, pdMS_TO_TICKS(1000));
                if (res != pdTRUE) {
                    ESP_LOGE(TAG, "Failed to send item");
                }
                break;
            //Event of HW FIFO overflow detected
            case UART_FIFO_OVF:
                ESP_LOGI(TAG, "hw fifo overflow");
                // If fifo overflow happened, you should consider adding flow control for your application.
                // The ISR has already reset the rx FIFO,
                // As an example, we directly flush the rx buffer here in order to read more data.
                uart_flush_input(ECHO_UART_PORT_NUM);
                xQueueReset(uart_queue);
                break;
            //Event of UART ring buffer full
            case UART_BUFFER_FULL:
                ESP_LOGI(TAG, "ring buffer full");
                // If buffer full happened, you should consider increasing your buffer size
                // As an example, we directly flush the rx buffer here in order to read more data.
                uart_flush_input(ECHO_UART_PORT_NUM);
                xQueueReset(uart_queue);
                break;
            //Others
            default:
                ESP_LOGI(TAG, "uart event type: %d", event.type);
                break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}


static void uart_tx_task(void *pvParameters) {
    RingbufHandle_t ringbuf = (RingbufHandle_t)pvParameters;

    while (1) {
        //Receive data from byte buffer
        size_t item_size;
        char *data = (char *)xRingbufferReceiveUpTo(ringbuf, &item_size, pdMS_TO_TICKS(1000), 100);

        //Check received data
        if (data != NULL) {
            // ESP_LOGI("uart tx", "write %d bytes to tx", item_size);

            // Write data back to the UART
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char *) data, item_size);

            //Return Item
            vRingbufferReturnItem(ringbuf, (void *)data);
        } else {
            //Failed to receive item
            // printf("Failed to receive item\n");
        }
    }
}

void create_stm32_serial_task(RingbufHandle_t rx_buffer, RingbufHandle_t tx_buffer)
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = ECHO_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        // .source_clk = UART_SCLK_DEFAULT,
        .source_clk = UART_SCLK_XTAL,
    };

    // ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, 0, 20, &uart_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));

    ESP_ERROR_CHECK(uart_set_rx_full_threshold(ECHO_UART_PORT_NUM, 32));
    ESP_ERROR_CHECK(uart_set_rx_timeout(ECHO_UART_PORT_NUM, 5));
    ESP_ERROR_CHECK(uart_enable_rx_intr(ECHO_UART_PORT_NUM));

    // ESP_ERROR_CHECK(uart_intr_config(ECHO_UART_PORT_NUM, &uart_intr));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));


    // xTaskCreate(serial_rx_task, "uart rx task", STACK_SIZE, (void*)rx_buffer, 10, NULL);
    xTaskCreate(uart_rx_task, "uart rx task", STACK_SIZE, (void*)rx_buffer, 10, NULL);
    xTaskCreate(uart_tx_task, "uart tx task", STACK_SIZE, (void*)tx_buffer, 10, NULL);
}
