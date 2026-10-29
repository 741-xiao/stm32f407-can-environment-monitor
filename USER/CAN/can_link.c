#include "can_link.h"
#include "app_config.h"
#include "stm32f4xx_hal.h"

CAN_HandleTypeDef hcan1;
static volatile uint32_t rx_count;
static volatile uint32_t error_count;

static int AddFilter(uint32_t bank, uint32_t id)
{
    CAN_FilterTypeDef filter = {0};
    filter.FilterBank = bank;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = (uint16_t)(id << 5);
    filter.FilterIdLow = 0U;
    filter.FilterMaskIdHigh = (uint16_t)(0x7FFU << 5);
    filter.FilterMaskIdLow = 0U;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = CAN_FILTER_ENABLE;
    filter.SlaveStartFilterBank = 14U;
    return HAL_CAN_ConfigFilter(&hcan1, &filter) == HAL_OK;
}

int CanLink_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_CAN1_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOA, &gpio);
    hcan1.Instance = CAN1;
    hcan1.Init.Prescaler = APP_CAN_PRESCALER;
    hcan1.Init.Mode = CAN_MODE_NORMAL;
    hcan1.Init.SyncJumpWidth = APP_CAN_SJW;
    hcan1.Init.TimeSeg1 = APP_CAN_TIME_SEG1;
    hcan1.Init.TimeSeg2 = APP_CAN_TIME_SEG2;
    hcan1.Init.TimeTriggeredMode = DISABLE;
    hcan1.Init.AutoBusOff = ENABLE;
    hcan1.Init.AutoWakeUp = DISABLE;
    hcan1.Init.AutoRetransmission = ENABLE;
    hcan1.Init.ReceiveFifoLocked = DISABLE;
    hcan1.Init.TransmitFifoPriority = DISABLE;
    if (HAL_CAN_Init(&hcan1) != HAL_OK) return 0;
    if (!AddFilter(0U, APP_CAN_DATA_ID) || !AddFilter(1U, APP_CAN_META_ID)) return 0;
    if (HAL_CAN_Start(&hcan1) != HAL_OK) return 0;
    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING |
            CAN_IT_ERROR | CAN_IT_BUSOFF | CAN_IT_LAST_ERROR_CODE) != HAL_OK) return 0;
    HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
    return 1;
}

int CanLink_Send(uint32_t id, const uint8_t *data, uint8_t length)
{
    CAN_TxHeaderTypeDef header = {0};
    uint32_t mailbox;
    uint32_t started = HAL_GetTick();
    if (data == NULL || length == 0U || length > 8U) return 0;
    header.StdId = id;
    header.ExtId = 0U;
    header.IDE = CAN_ID_STD;
    header.RTR = CAN_RTR_DATA;
    header.DLC = length;
    header.TransmitGlobalTime = DISABLE;
    while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0U) {
        if ((uint32_t)(HAL_GetTick() - started) >= 20U) return 0;
    }
    return HAL_CAN_AddTxMessage(&hcan1, &header, (uint8_t *)data, &mailbox) == HAL_OK;
}

int CanLink_Read(uint32_t *id, uint8_t *data, uint8_t *length)
{
    CAN_RxHeaderTypeDef header;
    if (id == NULL || data == NULL || length == NULL ||
        HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0) == 0U) return 0;
    if (HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &header, data) != HAL_OK) return 0;
    if (header.IDE != CAN_ID_STD || header.RTR != CAN_RTR_DATA || header.DLC > 8U) return 0;
    *id = header.StdId;
    *length = (uint8_t)header.DLC;
    return 1;
}

uint32_t CanLink_RxCount(void) { return rx_count; }
uint32_t CanLink_ErrorCount(void) { return error_count; }

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *can)
{
    if (can == &hcan1) rx_count++;
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *can)
{
    if (can == &hcan1) error_count++;
}
