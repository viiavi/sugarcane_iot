#include "dht11.h"

/* -------------------------------------------------------------------------- */
/* Private microsecond delay using DWT                                        */
/* -------------------------------------------------------------------------- */

static void DHT11_DelayUs_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void DHT11_DelayUs(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = (HAL_RCC_GetHCLKFreq() / 1000000U) * us;

    while ((DWT->CYCCNT - start) < ticks)
    {
        /* wait */
    }
}

/* -------------------------------------------------------------------------- */
/* GPIO mode switching                                                        */
/* -------------------------------------------------------------------------- */

static void DHT11_SetPinOutput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin   = DHT11_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
}

static void DHT11_SetPinInput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin   = DHT11_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;   /* external pull-up recommended */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
}

/* -------------------------------------------------------------------------- */
/* Wait for pin state with real microsecond timeout                           */
/* -------------------------------------------------------------------------- */

static HAL_StatusTypeDef DHT11_WaitForState(GPIO_PinState state, uint32_t timeout_us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = (HAL_RCC_GetHCLKFreq() / 1000000U) * timeout_us;

    while ((DWT->CYCCNT - start) < ticks)
    {
        if (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == state)
        {
            return HAL_OK;
        }
    }

    return HAL_TIMEOUT;
}

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

void DHT11_Init(void)
{
    DHT11_DelayUs_Init();
}

HAL_StatusTypeDef DHT11_ReadData(DHT11_Data_t *data)
{
    uint8_t raw[5] = {0};

    if (data == NULL)
    {
        return HAL_ERROR;
    }

    /* 1. Host start signal: pull line LOW for at least 18 ms */
    DHT11_SetPinOutput();
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
    HAL_Delay(18);

    /* 2. Release line and wait 20-40 us */
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
    DHT11_DelayUs(30);

    /* 3. Switch to input so sensor can respond */
    DHT11_SetPinInput();

    /* 4. Sensor response sequence:
          - ~80 us LOW
          - ~80 us HIGH
          - then LOW before first data bit
    */
    if (DHT11_WaitForState(GPIO_PIN_RESET, 100) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (DHT11_WaitForState(GPIO_PIN_SET, 100) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (DHT11_WaitForState(GPIO_PIN_RESET, 100) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* 5. Read 40 bits */
    for (int bit = 0; bit < 40; bit++)
    {
        /* Wait for line to go HIGH = bit transmission starts */
        if (DHT11_WaitForState(GPIO_PIN_SET, 70) != HAL_OK)
        {
            return HAL_ERROR;
        }

        /* After ~40 us:
           - still HIGH => bit is 1
           - LOW already => bit is 0
        */
        DHT11_DelayUs(40);

        if (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
        {
            raw[bit / 8] |= (1U << (7 - (bit % 8)));

            /* Wait for this HIGH pulse to end */
            if (DHT11_WaitForState(GPIO_PIN_RESET, 70) != HAL_OK)
            {
                return HAL_ERROR;
            }
        }
    }

    /* 6. Check checksum */
    if ((((uint8_t)(raw[0] + raw[1] + raw[2] + raw[3])) != raw[4]))
    {
        return HAL_ERROR;
    }

    /* 7. DHT11 data format
          raw[0] = humidity integer
          raw[1] = humidity decimal (usually 0)
          raw[2] = temperature integer
          raw[3] = temperature decimal (usually 0)
          raw[4] = checksum
    */
    data->Humidity    = (float)raw[0];
    data->Temperature = (float)raw[2];

    return HAL_OK;
}
