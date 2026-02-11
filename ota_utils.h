#ifndef OTA_UTILS_H
#define OTA_UTILS_H

#include <Arduino.h>

// Inicializa el servidor OTA en una tarea FreeRTOS (no bloqueante)
// Se ejecuta en paralelo con el resto del código
void init_ota_background();

// Detiene el servidor OTA (si es necesario)
void stop_ota_background();

// Verifica si OTA está activo
bool is_ota_active();

// Llamar desde otros módulos para actualizar métricas mostradas en la UI OTA
// door_state: -1 = desconocido, 0 = cerrada, 1 = abierta
void ota_set_device_metrics(float temp_c, float humidity_pct, int battery_pct, int door_state);

// Persistent operation mode: continuous (true) or normal (false)
bool ota_is_continuous_mode();
void ota_set_continuous_mode(bool continuous);

#endif
