#include "sleep_utils.h"
#include "config.h"
#include "wifi_utils.h"
#include "display_utils.h"
#include "driver/rtc_io.h"
#include "ota_utils.h"
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <Preferences.h>
#ifdef CONFIG_BT_ENABLED
#include "esp_bt.h"
#endif


//  Función que permite calcular el tiempo restante para que el módulo salga del DeepSleep
void update_deep_sleep_time(time_t wakeup_time) 
{
  char sleep_time_str[20];
  char wake_time_str[20];
  char diff_line[25];
  struct tm tm_info;

  // Formatear hora de inicio de DeepSleep
  localtime_r(&deep_sleep_start_time, &tm_info);
  strftime(sleep_time_str, sizeof(sleep_time_str), "%H:%M:%S", &tm_info);

  // Formatear hora de despertar
  localtime_r(&wakeup_time, &tm_info);
  strftime(wake_time_str, sizeof(wake_time_str), "%H:%M:%S", &tm_info);

  // Calcular diferencia en segundos
  int durmio_segundos = difftime(wakeup_time, deep_sleep_start_time);

  // Calcular diferencia restante
  int diferencia_restante = Deep_Sleep_Time_S - durmio_segundos;
  if (diferencia_restante < 0) 
  {
    diferencia_restante = 0;  // No permitir valores negativos
  }

  // Actualizar tiempo en microsegundos
  Deep_Sleep_time_uS = (uint64_t)diferencia_restante * 1000000ULL;
  delay(20);
}


//  Función que permite configurar el GPIO como EXT0 y el nivel lógico por el cual va a despertar
void set_wakeup_EXT0() 
{
  if (digitalRead(DOOR_SENSOR_PIN) == HIGH) 
  {
    esp_sleep_enable_ext0_wakeup(DOOR_SENSOR_PIN, 0);   //  Despierta si el pin detecta un cambio de HIGH a LOW
  } 
  else 
  {
    esp_sleep_enable_ext0_wakeup(DOOR_SENSOR_PIN, 1);   //  Despierta si el pin detecta un cambio de LOW a HIGH
  }
  delay(20);
}


// Función que configura todos los parámetros de deep sleep
void configure_deep_sleep()
{
  //  Se configuran las resistencias PULLUP y PULLDOWN en el pin RTC
  rtc_gpio_pullup_en(DOOR_SENSOR_PIN);
  rtc_gpio_pulldown_dis(DOOR_SENSOR_PIN);

  //  Se configura el PIN que servirá como interrupción externa
  esp_sleep_enable_ext0_wakeup(DOOR_SENSOR_PIN, 0);     //0 para despertar por estado LOW y 1 para despertar por estado HIGH

  //  Se configura el tiempo que el sensor va a dormir en el modo DeepSleep
  // Leer intervalo persistente (en minutos) si está disponible en NVS
  {
    Preferences prefs;
    prefs.begin("moe_cfg", true);
    uint8_t minutes = prefs.getUChar("interval_minutes", 10);
    prefs.end();
    Deep_Sleep_Time_S = (uint16_t)minutes * 60;
    Deep_Sleep_time_uS = (uint64_t)Deep_Sleep_Time_S * 1000000ULL;
  }
  esp_sleep_enable_timer_wakeup(Deep_Sleep_time_uS);

  // Se configura los dominios de alimentación para máximo ahorro
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
}


// Función para entrar en deep sleep
void enter_deep_sleep()
{
  // If device is in continuous mode, skip deep-sleep here
  if (current_mode == MODE_CONTINUOUS) {
    Serial.println("[SLEEP] Current mode is CONTINUOUS - skipping deep sleep");
    return;
  }

  // Apagar la pantalla OLED completamente antes de dormir (corta Vext)
  VextOFF();

  // Detener WiFi y driver para máximo ahorro
  if (WiFi.isConnected()) {
    WiFi.disconnect(true);
  }
  WiFi.mode(WIFI_OFF);
  esp_wifi_stop();
  esp_wifi_deinit();

  // Deshabilitar Bluetooth controlador si está presente (silencioso si no está)
#ifdef CONFIG_BT_ENABLED
  esp_bt_controller_disable();
#endif

  // Configurar wakeup sources and timer
  configure_deep_sleep();

  Serial.println("[SLEEP] Entering DEEP SLEEP (WiFi/Bluetooth off, display off)");
  esp_deep_sleep_start();
}

// Strong implementation of ota_on_mode_changed to react when OTA UI changes mode
void ota_on_mode_changed(bool continuous)
{
  Serial.printf("[SLEEP] ota_on_mode_changed -> continuous=%s\n", continuous ? "true" : "false");
  if (continuous) {
    // Ensure WiFi active and no sleeping
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    Serial.println("[SLEEP] Continuous mode: staying awake");
  } else {
    // Set runtime mode to NORMAL (do NOT persist across reboots) and enter deep-sleep cycle
    current_mode = MODE_NORMAL;
    Serial.println("[SLEEP] Switching to MODE_NORMAL (runtime only). Entering deep sleep cycle now.");
    enter_deep_sleep();
  }
}


// Función para configurar pines no utilizados
void configure_unused_pins()
{
  // Configurar pines no utilizados como INPUT con PULLUP para reducir consumo
  // Ajustar según los pines que realmente uses en tu placa
  
  // Ejemplo de pines que podrían no usarse (verificar con tu diseño):
  pinMode(GPIO_NUM_2, INPUT_PULLUP);
  pinMode(GPIO_NUM_3, INPUT_PULLUP);
  pinMode(GPIO_NUM_5, INPUT_PULLUP);
  pinMode(GPIO_NUM_6, INPUT_PULLUP);
  pinMode(GPIO_NUM_7, INPUT_PULLUP);
  // Agregar más pines según sea necesario
}

//