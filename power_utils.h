#ifndef POWER_UTILS_H
#define POWER_UTILS_H

#include <Arduino.h>

// Funciones de optimización energética
void disable_bluetooth();
void configure_cpu_frequency();
void disable_unused_peripherals();
void configure_power_domains();

// Función de inicialización completa de ahorro energético
void init_power_optimization();

#endif