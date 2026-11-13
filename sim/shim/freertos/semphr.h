#pragma once
#include "FreeRTOS.h"

// sin tareas concurrentes no hay a quien esperar: tomar el semaforo nunca bloquea
typedef struct SimSemaphore *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateBinary();
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
void vSemaphoreDelete(SemaphoreHandle_t s);
