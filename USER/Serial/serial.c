#include "serial.h"
#include "app_config.h"
#include "stm32f4xx_hal.h"

#define RX_CAPACITY 512U
typedef struct {
    volatile uint16_t head, tail;
    volatile uint8_t data[RX_CAPACITY];
    volatile uint8_t error;
} SerialRx;
static SerialRx rx[2];

void Serial_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();
    gpio.Pin = WIFI_ENABLE_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_WritePin(WIFI_ENABLE_GPIO, WIFI_ENABLE_PIN, GPIO_PIN_SET);
    HAL_GPIO_Init(WIFI_ENABLE_GPIO, &gpio);
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Alternate = GPIO_AF8_UART4;
#if WIFI_UART4_USE_PC10_PC11
    __HAL_RCC_GPIOC_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    HAL_GPIO_Init(GPIOC, &gpio);
#else
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    HAL_GPIO_Init(GPIOA, &gpio);
#endif
    USART1->CR1 = 0U;
    UART4->CR1 = 0U;
    USART1->CR2 = UART4->CR2 = 0U;
    USART1->CR3 = UART4->CR3 = 0U;
    USART1->BRR = (HAL_RCC_GetPCLK2Freq() + CONSOLE_BAUD_RATE / 2U) / CONSOLE_BAUD_RATE;
    UART4->BRR = (HAL_RCC_GetPCLK1Freq() + WIFI_BAUD_RATE / 2U) / WIFI_BAUD_RATE;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    UART4->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    HAL_NVIC_SetPriority(USART1_IRQn, 3U, 0U);
    HAL_NVIC_SetPriority(UART4_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    HAL_NVIC_EnableIRQ(UART4_IRQn);
}

static void Receive(USART_TypeDef *uart, SerialRx *buffer)
{
    uint32_t status = uart->SR;
    uint8_t byte;
    uint16_t next;
    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
        byte = (uint8_t)uart->DR; /* SR then DR clears RXNE and error flags. */
        if (status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
            buffer->error = 1U;
            return;
        }
        next = (uint16_t)((buffer->head + 1U) % RX_CAPACITY);
        if (next == buffer->tail) buffer->error = 1U;
        else {
            buffer->data[buffer->head] = byte;
            __DMB();
            buffer->head = next;
        }
    }
}
void USART1_IRQHandler(void) { Receive(USART1, &rx[SERIAL_CONSOLE]); }
void UART4_IRQHandler(void) { Receive(UART4, &rx[SERIAL_WIFI]); }

int Serial_Read(uint8_t port, uint8_t *byte)
{
    SerialRx *buffer = &rx[port];
    if (buffer->head == buffer->tail) return 0;
    *byte = buffer->data[buffer->tail];
    __DMB();
    buffer->tail = (uint16_t)((buffer->tail + 1U) % RX_CAPACITY);
    return 1;
}

int Serial_TakeError(uint8_t port)
{
    uint32_t primask = __get_PRIMASK();
    int error;
    __disable_irq();
    error = rx[port].error;
    rx[port].error = 0U;
    /* Keep successfully received bytes. Consumers discard an interrupted line;
       during ESP reset a valid ready line may follow bad ROM baud-rate bytes. */
    __set_PRIMASK(primask);
    return error;
}

int Serial_Write(uint8_t port, const char *text)
{
    USART_TypeDef *uart = port == SERIAL_CONSOLE ? USART1 : UART4;
    uint32_t started = HAL_GetTick();
    while (*text) {
        while (!(uart->SR & USART_SR_TXE)) {
            if ((uint32_t)(HAL_GetTick() - started) >= 50U) return 0;
        }
        uart->DR = (uint8_t)*text++;
    }
    return 1;
}
