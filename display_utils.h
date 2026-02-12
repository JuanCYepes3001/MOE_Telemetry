#ifndef DISPLAY_UTILS_H
#define DISPLAY_UTILS_H

#include <Arduino.h>
#include "HT_SSD1306Wire.h"

// Declaración del objeto display
extern SSD1306Wire oled_display;

// Funciones de inicialización
void VextON();
void VextOFF();
void init_display();

// Funciones de visualización
void display_oled_message_3_line(String line_1, String line_2, String line_3);
void display_oled_message_2_line(String line_1, String line_2);
void display_oled_ap_info(const String &ssid, const String &ip, const String &mac);

#endif