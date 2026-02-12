#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Definicion de pines utilizados
#define PRG_BUTTON_PIN      GPIO_NUM_0                      //  Pin digital asignado al botón P de la tarjeta
#define DOOR_SENSOR_PIN     GPIO_NUM_19                      //  Pin digital asignado al sensor de apertura de puertas

// Pin configurable para detección de long-press de "reset".
// Por defecto apuntamos al mismo pin PRG_BUTTON_PIN para compatibilidad.
#define RESET_BUTTON_PIN PRG_BUTTON_PIN

#define DHT22_SENSOR_PIN    GPIO_NUM_46                     //  Pin digital asignado al sensor de temperatura y humedad
#define DHTTYPE             DHT22                           //  Referencia del sensor de temperatura y humedad utilizado

#define VBAT_READ_PIN	    GPIO_NUM_1                      //  Pin analógico asignado a la medicion del voltaje de la bateria 
#define ADC_CTRL_PIN 	    GPIO_NUM_37                     //  Pin digital asignado a la activación del divisor resistivo para lectura de voltaje de bateria

// Pines OLED (Heltec ESP32 Lora V3)
#define SDA_OLED            GPIO_NUM_17                     //  Pin SDA para comunicación I2C con pantalla OLED
#define SCL_OLED            GPIO_NUM_18                     //  Pin SCL para comunicación I2C con pantalla OLED
#define RST_OLED            GPIO_NUM_21                     //  Pin de reset para pantalla OLED
#define Vext                GPIO_NUM_36                     //  Pin de control de alimentación para pantalla OLED

// Definición de versión del firmware
#define FIRMWARE_VERSION "1.2.0"                            //  Versión del firmware

// Definición de constantes de tiempo
extern uint16_t Deep_Sleep_Time_S;                    //  Tiempo de duración del DeepSleep en segundos
extern uint64_t Deep_Sleep_time_uS;                         //  Tiempo de duración del DeepSleep en microsegundos

// Nuevas constantes para optimización de energía
extern const uint16_t WIFI_TIMEOUT_MS;                      //  Timeout para conexión WiFi

// Constantes del sistema
extern const float volt_div_factor;                         //  Constante del divisor resistivo

// Credenciales WiFi
extern const char* ssid;                                    //  Constante que contiene el SSID o nombre de la red WIFI
extern const char* password;                                //  Constante que contiene el password de la red WIFI

// Configuración servidor NTP
extern const char* ntpServer;                               //  Dirección IP del servidor NTP interno

// Endpoints del servidor
extern const String base_url;                               // URL base del servidor
extern const String endpoint_telemetry;                     // Endpoint del servidor para registro de temperatura y humedad
extern const String endpoint_door_sensor;                   // Endpoint del servidor para registro de apertura de puertas

// Variables globales de sensores
extern float temperature;                                   // Variable que contiene la temperatura actual
extern float humidity;                                      // Variable que contiene la humedad actual
extern int battery_voltage;                                 // Variable que contiene el voltaje de la bateria
extern int battery_level;                                   // Variable que contiene el porcentaje de la bateria

// Variables de display
extern String display_temperature;
extern String display_humidity;
extern String display_battery_voltage;
extern String display_battery_level;

// Variables de display
extern String display_door_status;

// Modos de funcionamiento
#define MODE_NORMAL 0
#define MODE_CONTINUOUS 1

// RTC memory variables
extern RTC_DATA_ATTR time_t deep_sleep_start_time;
extern RTC_DATA_ATTR uint8_t current_mode; // Persistente: MODE_NORMAL o MODE_CONTINUOUS

#endif
