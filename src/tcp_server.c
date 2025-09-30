#include "tcp_server.h"

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


#define PORT                        55533
#define KEEPALIVE_IDLE              5
#define KEEPALIVE_INTERVAL          5
#define KEEPALIVE_COUNT             3

static const char *TAG = "tcp_server";

static EventGroupHandle_t socket_event_group;

#define SOCKET_CONNECTED_BIT BIT0
#define SOCKET_DISCONNECTED_BIT BIT1

static int tcp_socket_fd;



static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);


        // disable wifi modem power saving for better performance
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

        tcp_socket_fd = sock;
        xEventGroupSetBits(socket_event_group, SOCKET_CONNECTED_BIT);
        EventBits_t bits = xEventGroupWaitBits(socket_event_group,
            SOCKET_DISCONNECTED_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
        xEventGroupClearBits(socket_event_group, SOCKET_DISCONNECTED_BIT);

        // enable wifi modem power saving again
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}


static void tcp_rx_task(void *pvParameters)
{
    RingbufHandle_t ringbuf = (RingbufHandle_t)pvParameters;

    int len;
    char rx_buffer[128];

    while (1) {
        // wait until socket connects
        EventBits_t bits = xEventGroupWaitBits(socket_event_group,
            SOCKET_CONNECTED_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

        do {
            // receive data from socket
            len = recv(tcp_socket_fd, rx_buffer, sizeof(rx_buffer), 0);
            if (len < 0) {
                ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
            } else if (len == 0) {
                ESP_LOGW(TAG, "Connection closed");
            } else {
                // ESP_LOGI("tcp rx", "read %d bytes to tx", len);

                UBaseType_t res = xRingbufferSend(ringbuf, rx_buffer, len, pdMS_TO_TICKS(1000));
                if (res != pdTRUE) {
                    ESP_LOGW(TAG, "Failed to send item");
                }
            }
        } while (len > 0);

        xEventGroupClearBits(socket_event_group, SOCKET_CONNECTED_BIT);
        xEventGroupSetBits(socket_event_group, SOCKET_DISCONNECTED_BIT);
    }
}

static void tcp_tx_task(void *pvParameters) {
    RingbufHandle_t ringbuf = (RingbufHandle_t)pvParameters;

    while (1) {
        //Receive data from byte buffer
        size_t item_size;
        char *data = (char *)xRingbufferReceiveUpTo(ringbuf, &item_size, pdMS_TO_TICKS(1000), 1000);

        //Check received data
        if (data != NULL) {
            // ESP_LOGI("tcp tx", "write %d bytes to tx", item_size);


            // try to write the data to the socket, if connected
            EventBits_t bits = xEventGroupGetBits(socket_event_group);
            if ((bits & SOCKET_CONNECTED_BIT) && !(bits & SOCKET_DISCONNECTED_BIT)) {

                // send() can return less bytes than supplied length.
                // Walk-around for robust implementation.
                int to_write = item_size;
                while (to_write > 0) {
                    int written = send(tcp_socket_fd, data + (item_size - to_write), to_write, 0);
                    if (written < 0) {
                        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                        // Failed to retransmit, giving up
                        break;
                    }
                    to_write -= written;
                }
            }
            // else: trash data

            //Return Item
            vRingbufferReturnItem(ringbuf, (void *)data);
        } else {
            //Failed to receive item
            // printf("Failed to receive item\n");
        }
    }
}

void create_tcp_server_task(RingbufHandle_t rx_buffer, RingbufHandle_t tx_buffer)
{
    socket_event_group = xEventGroupCreate();

    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET, 5, NULL);
    xTaskCreate(tcp_rx_task, "tcp_rx", 4096, (void*)rx_buffer, 5, NULL);
    xTaskCreate(tcp_tx_task, "tcp_tx", 4096, (void*)tx_buffer, 5, NULL);
}