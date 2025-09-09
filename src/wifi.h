#pragma once

#include <stdint.h>

#define WIFI_CONNECT_MAXIMUM_RETRIES  2

void wifi_init_sta(void);

void wifi_set_ssid(uint8_t ssid[32]);
void wifi_set_password(uint8_t password[64]);
void wifi_reconnect();
uint32_t wifi_get_ip();