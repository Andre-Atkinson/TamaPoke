#pragma once
#include "FreeRTOS.h"

typedef struct SimTask *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
// la tarea se registra pero no se ejecuta nunca (no hay planificador en el host)
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                       UBaseType_t prio, TaskHandle_t *out);
static inline void xTaskNotifyGive(TaskHandle_t) {}
static inline uint32_t ulTaskNotifyTake(BaseType_t, TickType_t) { return 1; }
void vTaskDelay(TickType_t ticks);
static inline UBaseType_t uxTaskGetNumberOfTasks() { return 1; }
static inline void vTaskGetRunTimeStats(char *buf) { if (buf) buf[0] = 0; }
