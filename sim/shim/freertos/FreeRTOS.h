// Simulador: lo justo de FreeRTOS para trainingcapture.cpp (1.75C). No hay planificador:
// las tareas se crean pero no corren, y el bucle hace los sondeos del tactil el solo
// (TrainingCapture::poll() desde handleTouch), como cuando no se esta dibujando.
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xFFFFFFFFu
