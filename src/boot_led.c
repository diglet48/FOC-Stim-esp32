#include "boot_led.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO  (11) // GPIO num
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_DUTY_RES   LEDC_TIMER_10_BIT
#define LEDC_DUTY       (1024)
#define LEDC_DUTY_2     (1)
#define LEDC_FREQUENCY  (4000)             // Frequency in Hertz. Set frequency at 4 kHz

#define STACK_SIZE (4096)


// task to reduce the LED brightness after boot, so it flickers brightly on boot and is dim afterwards.
static void reduce_brightness(void *pvParameters)
{
    int delay_ms = 100;
    vTaskDelay(delay_ms / portTICK_PERIOD_MS);

    boot_led_set_duty(LEDC_DUTY_2);

    vTaskDelete(NULL);
}

void init_boot_led(void)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQUENCY, // Set output frequency at 4 kHz
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure= false
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .gpio_num = LEDC_OUTPUT_IO,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = 0, // Set duty to 0%
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {}};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    boot_led_set_duty(LEDC_DUTY);

    xTaskCreate(reduce_brightness, "led brightness task", STACK_SIZE, (void*)NULL, 10, NULL);
}

void boot_led_set_duty(uint16_t duty)
{
    // set LED duty
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
}