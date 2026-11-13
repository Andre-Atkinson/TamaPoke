// Simulador: el light sleep solo adelanta el reloj falso lo que pida el temporizador
// (POWER_SLEEP_MS como mucho). Un toque del guion llega entre dos pasadas del bucle,
// asi que no hace falta despertar antes.
#pragma once
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t time_in_us);
esp_err_t esp_sleep_enable_gpio_wakeup(void);
esp_err_t esp_light_sleep_start(void);
uint32_t simLightSleeps();  // cuantas veces se ha dormido (para el resumen del simulador)
