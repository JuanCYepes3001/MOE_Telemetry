#ifndef SLEEP_UTILS_H
#define SLEEP_UTILS_H

#include <Arduino.h>
#include "driver/rtc_io.h"
#include <time.h>

// Actualiza el tiempo restante de deep sleep basándose en el tiempo de wakeup
void update_deep_sleep_time(time_t wakeup_time);

// Configurar EXT0 wakeup basado en el estado del pin de la puerta
void set_wakeup_EXT0();

// Configuración general del deep sleep (pines, timer, domains)
void configure_deep_sleep();

// Entra en deep sleep (apaga periféricos, display y ejecuta esp_deep_sleep_start)
void enter_deep_sleep();

// Configura pines no usados como INPUT_PULLUP para reducir consumo
void configure_unused_pins();

#endif