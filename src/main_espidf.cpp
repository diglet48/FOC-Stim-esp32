/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

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
#include "esp_sleep.h"
#include "esp_pm.h"
#include "esp_timer.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "usb_serial.h"
#include "uart.h"
#include "tcp_server.h"
#include "wifi.h"


RingbufHandle_t usb_serial_rx;
RingbufHandle_t usb_serial_tx;

RingbufHandle_t stm_serial_rx;
RingbufHandle_t stm_serial_tx;

RingbufHandle_t tcp_rx;
RingbufHandle_t tcp_tx;


struct RingbufferForwardParameters {
    RingbufHandle_t in;
    RingbufHandle_t out1;
    RingbufHandle_t out2;
    const char* tag;
};

static void ringbuffer_forward_task(void *pvParameters) {
    RingbufferForwardParameters params = *(RingbufferForwardParameters*)pvParameters;

    while (1) {
        //Receive data from byte buffer
        size_t len;
        char *data = (char *)xRingbufferReceiveUpTo(params.in, &len, pdMS_TO_TICKS(1000), 1000);

        //Check received data
        if (data != NULL) {
            // ESP_LOGI(params.tag, "write %d bytes", len);

            UBaseType_t res = xRingbufferSend(params.out1, data, len, pdMS_TO_TICKS(1000));
            if (res != pdTRUE) {
                ESP_LOGW(params.tag, "Failed to send item");
            }

            if (params.out2) {
                UBaseType_t res = xRingbufferSend(params.out2, data, len, pdMS_TO_TICKS(1000));
                if (res != pdTRUE) {
                    ESP_LOGW(params.tag, "Failed to send item (2)");
                }
            }

            //Return Item
            vRingbufferReturnItem(params.in, (void *)data);
        } else {
            //Failed to receive item
            // printf("Failed to receive item\n");
        }
    }

}

void forward(const char* taskname, RingbufHandle_t rx, RingbufHandle_t tx1, RingbufHandle_t tx2) {
    // LEAK!!
    RingbufferForwardParameters* params = new RingbufferForwardParameters{
        .in = rx,
        .out1 = tx1,
        .out2 = tx2,
        .tag = taskname,
    };
    xTaskCreate(ringbuffer_forward_task, taskname, 4096, (void*)params, 5, NULL);
}

void init_power_management() {
    // without wifi, light sleep off:
    // 10 / 10   = 11.1 mA
    // 160 / 10  = 11.2
    // 160 / 40  = 14.7
    // 160 / 160 = 26.2
    // 240 / 160 = 28.0
    // 240 / 240 = 31.3

    // with wifi
    // 10 / 80   = 35
    // 10 / 160  = 35
    // 80 / 160  = 34.7

    esp_pm_config_t config{
        .max_freq_mhz = 40,
        .min_freq_mhz = 160,
        // .light_sleep_enable = false,
        .light_sleep_enable = true,
    };

    esp_pm_configure(&config);
    esp_pm_get_configuration(&config);
    ESP_LOGI("main", "cpu freq min: %i, max: %i, light sleep: %i", config.min_freq_mhz, config.max_freq_mhz, config.light_sleep_enable);
}

extern "C" {
    void app_main(void)
    {
        // while(1) {
        //     esp_deep_sleep(10000000);
        // }
        init_power_management();

        //Initialize NVS
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);

        //Create ring buffers
        usb_serial_rx = xRingbufferCreate(1028, RINGBUF_TYPE_BYTEBUF);
        usb_serial_tx = xRingbufferCreate(1028, RINGBUF_TYPE_BYTEBUF);
        stm_serial_rx = xRingbufferCreate(1028, RINGBUF_TYPE_BYTEBUF);
        stm_serial_tx = xRingbufferCreate(1028, RINGBUF_TYPE_BYTEBUF);
        tcp_rx = xRingbufferCreate(1028, RINGBUF_TYPE_BYTEBUF);
        tcp_tx = xRingbufferCreate(1028, RINGBUF_TYPE_BYTEBUF);
        assert(usb_serial_rx);
        assert(usb_serial_tx);
        assert(stm_serial_rx);
        assert(stm_serial_tx);
        assert(tcp_rx);
        assert(tcp_tx);


        create_usb_serial_task(usb_serial_rx, usb_serial_tx);
        create_stm32_serial_task(stm_serial_rx, stm_serial_tx);
        // wifi_init_sta();
        create_tcp_server_task(tcp_rx, tcp_tx);

        forward("fw usb->stm", usb_serial_rx, stm_serial_tx, nullptr);
        forward("fw stm->usb + tcp", stm_serial_rx, usb_serial_tx, tcp_tx);
        forward("fw tcp->stm", tcp_rx, stm_serial_tx, nullptr);
    }
}



