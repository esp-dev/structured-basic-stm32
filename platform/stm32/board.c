#include "board.h"
#include "stm32f4xx_hal.h"

static Console console;
static uint8_t rx[2048];
static volatile uint32_t rx_write, rx_read, cancelled;
static ADC_HandleTypeDef adc;
static uint8_t gpio_output[3];

void SysTick_Handler(void) { HAL_IncTick(); }
void USART3_IRQHandler(void) {
    uint32_t status = USART3->SR;
    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) {
        uint8_t ch = (uint8_t)USART3->DR;
        if (!(status & USART_SR_RXNE)) return;
        if (ch == 3) { cancelled = 1; return; }
        uint32_t write = rx_write;
        uint32_t next = (write + 1u) % sizeof(rx);
        if (next != rx_read) { rx[write] = ch; __DMB(); rx_write = next; }
    }
}
static int take_break(void *context) {
    (void)context;
    uint32_t restore = __get_PRIMASK();
    __disable_irq();
    uint32_t result = cancelled;
    cancelled = 0;
    if (result) rx_read = rx_write;
    __set_PRIMASK(restore);
    return result != 0;
}
static int receive(void *context, uint32_t timeout) {
    uint32_t start = HAL_GetTick();
    do {
        if (take_break(context)) return 3;
        uint32_t read = rx_read;
        if (read != rx_write) {
            __DMB();
            int ch = rx[read];
            rx_read = (read + 1u) % sizeof(rx);
            return ch;
        }
    } while (timeout == UINT32_MAX || (HAL_GetTick() - start) < timeout);
    return -1;
}
void board_send(void *context, const void *data, size_t n) {
    const uint8_t *p = data;
    (void)context;
    for (size_t i = 0; i < n; ++i) {
        uint32_t start = HAL_GetTick();
        while (!(USART3->SR & USART_SR_TXE)) if (HAL_GetTick() - start >= 100u) return;
        USART3->DR = p[i];
    }
}
static int clock_init(void) {
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clock = {0};
    RCC_PeriphCLKInitTypeDef periph = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM = 16; osc.PLL.PLLN = 192; osc.PLL.PLLP = RCC_PLLP_DIV2; osc.PLL.PLLQ = 4;
    osc.PLL.PLLR = 2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) return -1;
    clock.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV2;
    clock.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_3) != HAL_OK) return -1;
    periph.PeriphClockSelection = RCC_PERIPHCLK_SDIO | RCC_PERIPHCLK_CLK48;
    periph.Clk48ClockSelection = RCC_CLK48CLKSOURCE_PLLQ;
    periph.SdioClockSelection = RCC_SDIOCLKSOURCE_CLK48;
    return HAL_RCCEx_PeriphCLKConfig(&periph) == HAL_OK ? 0 : -1;
}
static void serial_init(void) {
    GPIO_InitTypeDef pins = {0};
    __HAL_RCC_GPIOD_CLK_ENABLE(); __HAL_RCC_USART3_CLK_ENABLE();
    pins.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    pins.Mode = GPIO_MODE_AF_PP; pins.Pull = GPIO_PULLUP;
    pins.Speed = GPIO_SPEED_FREQ_HIGH; pins.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOD, &pins);
    USART3->BRR = (HAL_RCC_GetPCLK1Freq() + 57600u) / 115200u;
    USART3->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    HAL_NVIC_SetPriority(USART3_IRQn, 5, 0); HAL_NVIC_EnableIRQ(USART3_IRQn);
}
static int device(void *context, uint8_t op, int32_t a, int32_t b, int32_t *result) {
    static const uint16_t led[] = {GPIO_PIN_0, GPIO_PIN_7, GPIO_PIN_14};
    static const uint32_t analog[] = {ADC_CHANNEL_3, ADC_CHANNEL_10, ADC_CHANNEL_13, ADC_CHANNEL_11};
    static GPIO_TypeDef *const analog_port[] = {GPIOA, GPIOC, GPIOC, GPIOC};
    static const uint16_t analog_pin[] = {GPIO_PIN_3, GPIO_PIN_0, GPIO_PIN_3, GPIO_PIN_1};
    GPIO_InitTypeDef pin = {0};
    (void)context;
    *result = 0;
    if (op == SB_MILLIS) { *result = (int32_t)(HAL_GetTick() & 0x7fffffffu); return 0; }
    if (op == SB_DELAY && a >= 0 && a <= 10) { HAL_Delay((uint32_t)a); return 0; }
    if (op == SB_ADC && a >= 0 && a < 4) {
        ADC_ChannelConfTypeDef channel = {0};
        __HAL_RCC_ADC1_CLK_ENABLE(); __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE();
        pin.Pin = analog_pin[a]; pin.Mode = GPIO_MODE_ANALOG; HAL_GPIO_Init(analog_port[a], &pin);
        adc.Instance = ADC1; adc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
        adc.Init.Resolution = ADC_RESOLUTION_12B; adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
        adc.Init.NbrOfConversion = 1; adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
        adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
        channel.Channel = analog[a]; channel.Rank = 1; channel.SamplingTime = ADC_SAMPLETIME_84CYCLES;
        if (HAL_ADC_Init(&adc) != HAL_OK || HAL_ADC_ConfigChannel(&adc, &channel) != HAL_OK ||
            HAL_ADC_Start(&adc) != HAL_OK) return -1;
        HAL_StatusTypeDef status = HAL_ADC_PollForConversion(&adc, 20);
        if (status == HAL_OK) *result = (int32_t)HAL_ADC_GetValue(&adc);
        HAL_ADC_Stop(&adc);
        return status == HAL_OK ? 0 : -1;
    }
    if (a < 0 || a >= 3) return -1;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    if (op == SB_PINMODE && (b == 0 || b == 1)) {
        pin.Pin = led[a]; pin.Mode = b ? GPIO_MODE_OUTPUT_PP : GPIO_MODE_INPUT;
        HAL_GPIO_Init(GPIOB, &pin); gpio_output[a] = (uint8_t)b; return 0;
    }
    if (op == SB_PINREAD) { *result = HAL_GPIO_ReadPin(GPIOB, led[a]) == GPIO_PIN_SET; return 0; }
    if (op == SB_PINWRITE && gpio_output[a] && (b == 0 || b == 1)) {
        HAL_GPIO_WritePin(GPIOB, led[a], b ? GPIO_PIN_SET : GPIO_PIN_RESET); return 0;
    }
    return -1;
}
int main(void) {
    int clock_ok;
    const ConsoleIO io = {
        .receive = receive, .break_requested = take_break, .send = board_send,
        .command = card_command, .load = card_load, .save = card_save, .resolve = card_resolve,
        .device = device
    };
    HAL_Init();
    clock_ok = clock_init();
    serial_init();
    if (clock_ok) {
        static const char warning[] = "\r\nClock setup failed. SDIO may be unavailable.\r\n";
        board_send(NULL, warning, sizeof(warning) - 1);
    }
    console_run(&console, &io);
    for (;;) {}
}
