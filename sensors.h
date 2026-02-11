#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <DHT.h>

// Declaración del objeto DHT
extern DHT dht;

// Funciones de inicialización de sensores
void init_sensors();

// Funciones de lectura de sensores
void get_temperature_humidity();
void get_battery_status();

#endif