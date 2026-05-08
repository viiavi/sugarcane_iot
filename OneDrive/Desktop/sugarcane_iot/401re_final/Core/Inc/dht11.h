#ifndef __DHT11_H
#define __DHT11_H

#include "main.h"

#define DHT11_PORT GPIOB
#define DHT11_PIN  GPIO_PIN_5

typedef struct
{
    float Temperature;
    float Humidity;
} DHT11_Data_t;

void DHT11_Init(void);
HAL_StatusTypeDef DHT11_ReadData(DHT11_Data_t *data);

#endif
