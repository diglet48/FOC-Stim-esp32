#include "driver/gpio.h"


#define FOC_LED_GPIO (GPIO_NUM_11)

#if defined(BOARD_FOCSTIM_V4_0)
#define FOC_I2C_SCL_GPIO (GPIO_NUM_7)
#define FOC_I2C_SDA_GPIO (GPIO_NUM_1)
#define FOC_UART_RX_GPIO (GPIO_NUM_33)
#define FOC_UART_TX_GPIO (GPIO_NUM_34)


#elif defined(BOARD_FOCSTIM_V4_1)
#define FOC_I2C_SCL_GPIO (GPIO_NUM_38)
#define FOC_I2C_SDA_GPIO (GPIO_NUM_37)
#define FOC_UART_RX_GPIO (GPIO_NUM_17)
#define FOC_UART_TX_GPIO (GPIO_NUM_18)  // changed in V4.1 due to presence of startup glitches on this pin


#else
#error "unknown FOC-Stim board variant."
#endif