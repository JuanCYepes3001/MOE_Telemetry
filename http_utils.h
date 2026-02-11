#ifndef HTTP_UTILS_H
#define HTTP_UTILS_H

#include <Arduino.h>

// Funciones de envío HTTP
void send_POST_temperature_humidity_battery(float temp, float hum, int volt_batt, int porc_batt);
void send_POST_door_status_battery(int door_status, int battery_vol, int battery_lvl);

#endif