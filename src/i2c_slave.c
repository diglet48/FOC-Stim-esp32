#include "i2c_slave.h"

#include "freertos/FreeRTOS.h"
#include <string.h>
#include "esp_log.h"

#include "i2c_slave_driver.h"
#include "wifi.h"


static const char *TAG = "i2c_slave";

#define STACK_SIZE (4096)

#define I2C_SLAVE_SCL_IO (GPIO_NUM_7)
#define I2C_SLAVE_SDA_IO (GPIO_NUM_1)
#define I2C_SLAVE_NUM    0
#define DATA_LENGTH      100


i2c_slave_device_t *slave_handle = NULL;

QueueHandle_t wifi_update_params_queue;

uint8_t txbuffer[256];
uint8_t txbuffer_len;

uint8_t ssid[32];
uint8_t password[64];

#define ESP32_I2C_ADDRESS   0x72

#define ESP32_COMMAND_FWVERSION         0x01    // read
#define ESP32_COMMAND_IP                0x02    // read
#define ESP32_COMMAND_WIFI_SSID         0x03    // write
#define ESP32_COMMAND_WIFI_PASSWORD     0x04    // write
#define ESP32_COMMAND_WIFI_RECONNECT    0x05    // write



static bool slave_callback(struct i2c_slave_device_t *dev, I2CSlaveCallbackReason reason)
{
    if (reason == I2C_CALLBACK_REPEAT_START) {
        // ignore
    }
    if (reason == I2C_CALLBACK_DONE) {
        txbuffer_len = 0;
        uint8_t len;

        if (dev->bufend >= 1) {
            uint8_t cmd = dev->buffer[0];
            switch (cmd) {
                case ESP32_COMMAND_FWVERSION:
                    // TODO
                    break;

                case ESP32_COMMAND_IP:
                    uint32_t ip = wifi_get_ip();
                    txbuffer[3] = (ip >> 24) & 0xff;
                    txbuffer[2] = (ip >> 16) & 0xff;
                    txbuffer[1] = (ip >> 8) & 0xff;
                    txbuffer[0] = (ip >> 0) & 0xff;
                    txbuffer_len = 4;
                    break;

                case ESP32_COMMAND_WIFI_SSID:
                    memset(ssid, 0, 32);
                    len = dev->bufend - 1;
                    if (len <= 32) {
                        memcpy(ssid, dev->buffer + 1, len);
                        xQueueSendFromISR(wifi_update_params_queue, &cmd, NULL);
                    }
                    break;

                case ESP32_COMMAND_WIFI_PASSWORD:
                    memset(password, 0, 64);
                    len = dev->bufend - 1;
                    if (len <= 64) {
                        memcpy(password, dev->buffer + 1, len);
                        xQueueSendFromISR(wifi_update_params_queue, &cmd, NULL);
                    }
                    break;
                case ESP32_COMMAND_WIFI_RECONNECT:
                    xQueueSendFromISR(wifi_update_params_queue, &cmd, NULL);
                    break;
                default:
            }
        }
    }
    if (reason == I2C_CALLBACK_SEND_DATA) {
        uint8_t len = txbuffer_len;
        i2c_slave_send_data(dev, txbuffer, &len);
    }

    return true;
}

static void wifi_update_task(void *pvParameters) {
    while (1) {
        uint8_t cmd = 0;
        if (xQueueReceive(wifi_update_params_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            // ESP_LOGW(TAG, "receive cmd: %i", cmd);
            switch (cmd) {
                case ESP32_COMMAND_WIFI_SSID:
                    wifi_set_ssid(ssid);
                    break;
                case ESP32_COMMAND_WIFI_PASSWORD:
                    wifi_set_password(password);
                    break;
                case ESP32_COMMAND_WIFI_RECONNECT:
                    wifi_reconnect();
                    break;
            }
        } else {
            ESP_LOGW(TAG, "Failed to receive item");
        }
    }
}

void init_i2c_slave()
{
    i2c_slave_config_t slave_config = {
        .callback = slave_callback,
        .address = ESP32_I2C_ADDRESS,
        .gpio_scl = I2C_SLAVE_SCL_IO,
        .gpio_sda = I2C_SLAVE_SDA_IO,
        .i2c_port = I2C_SLAVE_NUM
    };

    ESP_ERROR_CHECK(i2c_slave_new(&slave_config, &slave_handle));

    wifi_update_params_queue = xQueueCreate(10, 1);
    xTaskCreate(wifi_update_task, "I2C slave", STACK_SIZE, (void*)NULL, 10, NULL);
}