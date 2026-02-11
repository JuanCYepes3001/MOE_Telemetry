#ifndef WIFI_UTILS_H
#define WIFI_UTILS_H

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

// Funciones de conexión WiFi
void set_wifi_connection();

// Configuración y persistencia de credenciales WiFi
bool load_wifi_credentials(String &out_ssid, String &out_password);
void save_wifi_credentials(const char* ssid, const char* password);
void erase_wifi_credentials();

// Inicia un Access Point con portal de configuración (bloqueante hasta guardar o reiniciar)
void start_config_ap();

// Funciones de sincronización de tiempo
time_t get_time_NTP();

// Funciones de desconexión para ahorro energético
void disconnect_wifi();

#endif