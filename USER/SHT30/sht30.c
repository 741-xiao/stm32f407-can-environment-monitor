#include "sht30.h"

#include "app_config.h"
#include "FreeRTOS.h"
#include "semphr.h"

I2C_HandleTypeDef hi2c1;
static StaticSemaphore_t i2c_mutex_buffer;
static SemaphoreHandle_t i2c_mutex;
static uint8_t i2c_ready;

static uint8_t SHT30_Crc8(const uint8_t *data, uint8_t length)
{
    uint8_t crc;
    uint8_t bit;

    crc = 0xFFU;
    while (length-- != 0U)
    {
        crc ^= *data++;
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x80U) != 0U)
            {
                crc = (uint8_t)((crc << 1) ^ 0x31U);
            }
            else
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

HAL_StatusTypeDef SHT30_Init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);

    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000U;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0U;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0U;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) return HAL_ERROR;
    if (i2c_mutex == NULL) i2c_mutex = xSemaphoreCreateMutexStatic(&i2c_mutex_buffer);
    i2c_ready = i2c_mutex != NULL;
    return i2c_ready ? HAL_OK : HAL_ERROR;
}

int SHT30_Lock(uint32_t timeout_ms)
{
    if (!i2c_ready || i2c_mutex == NULL) return 0;
    return xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

void SHT30_Unlock(void)
{
    if (i2c_ready && i2c_mutex != NULL) (void)xSemaphoreGive(i2c_mutex);
}

HAL_StatusTypeDef SHT30_ReadRaw(uint16_t *temperature, uint16_t *humidity)
{
    uint8_t command[2];
    uint8_t data[6];
    HAL_StatusTypeDef status;

    if (temperature == NULL || humidity == NULL) return HAL_ERROR;
    command[0] = 0x24U; /* high repeatability, no clock stretching */
    command[1] = 0x00U;
    if (!SHT30_Lock(APP_I2C_LOCK_TIMEOUT_MS)) return HAL_BUSY;
    status = HAL_I2C_Master_Transmit(&hi2c1, SHT30_I2C_ADDRESS,
                                     command, 2U, 50U);
    if (status != HAL_OK)
    {
        SHT30_Unlock();
        return status;
    }

    HAL_Delay(16U); /* maximum conversion time is 15 ms */
    status = HAL_I2C_Master_Receive(&hi2c1, SHT30_I2C_ADDRESS, data, 6U, 50U);
    SHT30_Unlock();
    if (status != HAL_OK)
    {
        return status;
    }

    if ((SHT30_Crc8(data, 2U) != data[2]) ||
        (SHT30_Crc8(&data[3], 2U) != data[5]))
    {
        return HAL_ERROR;
    }

    *temperature = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    *humidity = (uint16_t)(((uint16_t)data[3] << 8) | data[4]);
    return HAL_OK;
}
