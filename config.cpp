#include "config.h"

// Definición de constantes de tiempo
const uint16_t Deep_Sleep_Time_S = 600;                                                 //  Tiempo de duración del DeepSleep en segundos
uint64_t Deep_Sleep_time_uS = Deep_Sleep_Time_S * 1000000ULL;                           //  Tiempo de duración del DeepSleep en microsegundos

// Reducir tiempo de display para ahorrar energía
const uint16_t WIFI_TIMEOUT_MS = 8000;                                                  //  Timeout WiFi reducido (de 8000ms)

// Constantes del sistema
const float volt_div_factor = 5.0;                                                      //  Constante del divisor resistivo

// Credenciales WiFi
const char* ssid = "Ecosistema-FCV";                                                    //  Constante que contiene el SSID o nombre de la red WIFI
const char* password = "3mpLoyed_*FcV";                                                 //  Constante que contiene el password de la red WIFI
//const char* ssid = "MiTelefono";                                                      //  Constante que contiene el SSID o nombre de la red WIFI
//const char* password = "12345670";                                                    //  Constante que contiene el password de la red WIFI

// Configuración servidor NTP
const char* ntpServer = "172.30.19.65";                                                 //  Dirección IP del servidor NTP interno

// Endpoints del servidor
const String base_url = "https://172.30.19.97:5678/webhook";                            //  URL base del servidor
const String endpoint_telemetry = base_url + "/moe_telemetry/temperature_humidity";     //  Endpoint del servidor para registro de temperatura y humedad
const String endpoint_door_sensor = base_url + "/moe_telemetry/door_status";            //  Endpoint del servidor para registro de apertura de puertas

// Variables globales de sensores
float temperature = 0.0;                                                                //  Inicialización de la variable que contiene la temperatura actual
float humidity = 0.0;                                                                   //  Inicialización de la variable que contiene la humedad actual
int battery_voltage = 0;                                                                //  Inicialización de la variable que contiene el voltaje de la bateria
int battery_level = 0;                                                                  //  Inicialización de la variable que contiene el porcentaje de la bateria

// Variables de display
String display_temperature = "Temp: --°C";
String display_humidity = "Hum: --%";
String display_battery_voltage = "Bat: --mV";
String display_battery_level = "Bat: --%";
String display_door_status = "Puerta: --";

// Modos de funcionamiento
#define MODE_NORMAL 0
#define MODE_CONTINUOUS 1

// RTC memory variables
RTC_DATA_ATTR time_t deep_sleep_start_time = 0;
// Por defecto iniciar en modo continuo. Persistente a través de deep sleep
RTC_DATA_ATTR uint8_t current_mode = MODE_CONTINUOUS;