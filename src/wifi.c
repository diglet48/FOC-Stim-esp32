#include "wifi.h"

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

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

static int s_retry_num = 0;
static uint32_t ip = 0;


static wifi_config_t wifi_config_defaults = {
    .sta = {
        // .ssid = "",
        // .password = "",
        /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
            * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
            * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
            * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
            */
        .threshold = {
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
        .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        .sae_h2e_identifier = "",
    },
};


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_CONNECT_MAXIMUM_RETRIES) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI("wifi_event", "retry to connect to the AP");
        } else {
            ESP_LOGI("wifi_event", "maximun retries exceeded");
        }
        ESP_LOGI("wifi_event", "connect to the AP fail");
        ip = 0;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI("wifi_event", "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        ip = event->ip_info.ip.addr;
    }
}

void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // TODO: improve power saving
    // ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    // ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));

    // TODO: disable log messages of wifi?
}


void wifi_set_ssid(uint8_t ssid[32]) {
    wifi_config_t config = wifi_config_defaults;
    esp_wifi_get_config(WIFI_IF_STA, &config);

    memcpy(config.ap.ssid, ssid, 32);
    config.ap.ssid_len = 0;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
}

void wifi_set_password(uint8_t password[64]) {
    wifi_config_t config = wifi_config_defaults;
    esp_wifi_get_config(WIFI_IF_STA, &config);

    memcpy(config.ap.password, password, 64);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
}

void wifi_reconnect()
{
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    ESP_ERROR_CHECK(esp_wifi_connect());
    s_retry_num = 0;
}

uint32_t wifi_get_ip()
{
    return ip;
}
