/*
  Script que contiene el código principal para la tarjeta Heltec ESP32 Lora V3 basada en el modulo ESP32S3.
  Este código contiene las siguientes funcionalidades:
  1. Lectura de temperatura y humedad basada en el sensor DHT22 según un tiempo estimado
  2. Lectura del estado de apertura de una puerta basada en el sensor GPS-23L
  3. La tarjeta se conecta a la Red Ecosistema-FCV para el envío de información a los servidores locales
  4. El envío de la información la hace mediante una solicitud POST a los endpoints de los flujos creados en n8n
  5. Configuración de la tarjeta en el modo DeepSleep para garantizar un ahorro de energía en la bateria
  6. Optimización de energía: Bluetooth deshabilitado, gestion de CPU y dominio de alimentación
*/

//  Includes de módulos personalizados
#include "config.h"
#include "display_utils.h"
#include "sensors.h"
#include "wifi_utils.h"
#include "http_utils.h"
#include "sleep_utils.h"
#include "power_utils.h"
#include "button_utils.h"
#include "ota_utils.h"

// Safety prototype: si por alguna razón el encabezado no se encuentra
// en la copia que compilas desde el IDE de Arduino, esta declaración
// evita el error "not declared in this scope".
// door_state: -1 = desconocido, 0 = cerrada, 1 = abierta
void ota_set_device_metrics(float temp_c, float humidity_pct, int battery_pct, int door_state);

// Ensure RESET_BUTTON_PIN is defined (some toolchains may not include config.h early)
#ifndef RESET_BUTTON_PIN
#define RESET_BUTTON_PIN PRG_BUTTON_PIN
#endif

//  Librerias necesarias para el funcionamiento del módulo
#include <Wire.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include "driver/rtc_io.h"
#include <time.h>
#include <Preferences.h>

// ---- Helpers: gestión de pulsos en botón PRG ----
// Nota: la función anterior era bloqueante y provocaba latencias y re-lecturas no deseadas.
// Implementamos un detector no bloqueante para doble-click y funciones de persistencia en NVS.

// Variables para detector no bloqueante (ver button_utils.cpp)
// Ahora se declaran e inicializan en button_utils.cpp

// Persistencia de modo usando NVS (Preferences)
void saveModeToNVS(uint8_t mode)
{
  Preferences prefs;
  prefs.begin("moe_cfg", false);
  prefs.putUChar("mode", mode);
  prefs.end();
}

uint8_t loadModeFromNVS(uint8_t defaultMode)
{
  Preferences prefs;
  prefs.begin("moe_cfg", true);
  uint8_t m = prefs.getUChar("mode", defaultMode);
  prefs.end();
  return m;
}

// Detección robusta de long-press: detecta estado opuesto al inicial sostenido durante ms
bool isButtonLongPressed(int pin, unsigned long ms)
{
  int initial = digitalRead(pin);
  unsigned long start = millis();
  // esperar hasta que el pin cambie de estado
  while (digitalRead(pin) == initial && (millis() - start) < 2000) {
    delay(10);
  }
  // Si no cambió en 2s, no hay pulsación sostenida
  if (digitalRead(pin) == initial) return false;
  // Ahora esperar que el estado opuesto se mantenga durante ms
  unsigned long t0 = millis();
  while (digitalRead(pin) != initial && (millis() - t0) < ms) {
    delay(10);
  }
  return (digitalRead(pin) != initial) ? false : ((millis() - t0) >= ms);
}

//  Esta función contiene toda la lógica de funcionamiento del modulo y los diferentes sensores utilizados
void setup()
{
  // Inicializar Serial para debugging
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n\n=================================");
  Serial.println("MOE Telemetry v" + String(FIRMWARE_VERSION));
  Serial.println("=================================");
  
  // Inicializar optimizaciones de energía antes de ejecutar el funcionamiento del módulo
  init_power_optimization();

  //  Se inicializan los pines definidos
  pinMode(PRG_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DOOR_SENSOR_PIN, INPUT_PULLUP);

  //  Se inicializan los sensores y periféricos
  init_display();
  // Detectar long-press del botón de RESET (configurable) para factory reset (5 segundos)
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    unsigned long startPress = millis();
    // Esperar mientras se mantiene presionado y comprobar duración
    while (digitalRead(RESET_BUTTON_PIN) == LOW) {
      if (millis() - startPress >= 5000) {
        Serial.println("[SETUP] Long-press detectado: realizando factory reset de WiFi...");
        display_oled_message_3_line("Factory Reset","Borrando credenciales","Reiniciando...");
        erase_wifi_credentials();
        delay(200);
        ESP.restart();
      }
      delay(10);
    }
    // Si se pulsó brevemente (no llegó a 5s), forzar inicio en modo CONTINUO
    unsigned long pressDuration = millis() - startPress;
    if (pressDuration > 0 && pressDuration < 5000) {
      Serial.println("[SETUP] RESET breve detectado: forzando MODO_CONTINUO persistente");
      saveModeToNVS(MODE_CONTINUOUS);
      current_mode = MODE_CONTINUOUS;
      display_oled_message_3_line("Modo", "Forzado:", "Continuo");
      delay(600);
    }
  }
  init_sensors();
  init_button_detector(); // Inicializar detector de botones

  // Detectar 6 pulsaciones seguidas del botón PRG para entrar en modo AP/OTA
  // (reemplaza la detección de long-press que no funcionaba de forma fiable)
  int presses_for_ap = countButtonPressesWithinWindow(6000); // 6s ventana para 6 pulsos
    if (presses_for_ap >= 6) { 
    Serial.println("[SETUP] PRG 6x press detected: entrando en modo AP/OTA...");
    display_oled_message_3_line("Entrando en", "modo AP/OTA", "Espere...");
    // Start AP configuration portal (blocking)
    start_config_ap();
    // start_config_ap loops indefinitely until restart, so nothing after will run
  }

  // Nota: la conexión WiFi e inicialización OTA se realizan más adelante
  // sólo si el dispositivo permanece en modo CONTINUOUS (ver abajo).

  //  Se determina el motivo por el cual el módulo despertó del DeepSleep
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  // Nota: no forzar modo CONTINUO aquí para no romper el ciclo de DeepSleep.
  // Sólo en arranque en frío (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED)
  // se deberá forzar el modo CONTINUO si se desea reinicio físico.


  if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) 
  {
    // Al arrancar en frio: forzar MODO_CONTINUO persistente (reinicio físico)
    current_mode = MODE_CONTINUOUS;
    saveModeToNVS(MODE_CONTINUOUS);

    if (current_mode == MODE_CONTINUOUS) 
    {
      // Conectar WiFi y preparar OTA sólo en modo continuo
      Serial.println("[SETUP] Conectando WiFi (modo Continuo)...");
      display_oled_message_3_line("Conectando","a WiFi...","");
      delay(500);
      set_wifi_connection();

      if (WiFi.status() == WL_CONNECTED)
      {
        String ip_str = WiFi.localIP().toString();
        Serial.println("[SETUP] ✓ WiFi conectado!");
        Serial.println("[SETUP] IP: " + ip_str);
        Serial.println("[SETUP] RSSI: " + String(WiFi.RSSI()) + " dBm");
        Serial.println("[SETUP] OTA disponible en: http://" + ip_str);
        display_oled_message_3_line("v" + String(FIRMWARE_VERSION), "IP: " + ip_str, "OTA disponible");
        delay(3000);
        Serial.println("[SETUP] Iniciando OTA en background...");
        Serial.flush();
        init_ota_background();
        delay(500);
        if (is_ota_active()) Serial.println("[SETUP] ✓ OTA iniciado correctamente"); else Serial.println("[SETUP] ✗ ERROR: OTA no se inició");
      }
      else
      {
        Serial.println("[SETUP] ✗ Error: WiFi no disponible (status: " + String(WiFi.status()) + ")");
        display_oled_message_3_line("v" + String(FIRMWARE_VERSION), "Modo sin", "conexión");
        delay(1500);
      }

      display_oled_message_3_line(
        "Inicio de modo",
        "funcionamiento",
        "continuo"
      );
      delay(1000);

      // Bucle continuo: mostrar Temp, Hum y Estado de Puerta con respuesta inmediata a cambios
      // Variables para muestreo no bloqueante
      unsigned long last_sensor_update = 0;

      // Lectura inicial
      get_temperature_humidity();
      get_battery_status();
      int initial_door = (digitalRead(DOOR_SENSOR_PIN) == true) ? 1 : 0;
      last_door_state = initial_door; // Guardar estado inicial en RTC
      ota_set_device_metrics(temperature, humidity, battery_level, initial_door);
      display_door_status = last_door_state ? "Puerta:Abierta" : "Puerta:Cerrada";
      display_oled_message_3_line(display_temperature, display_humidity, display_door_status);
      
      Serial.printf("[CONTINUOUS] Estado inicial de puerta: %d\n", initial_door);

      while (true)
      {
        unsigned long now = millis();

        // 1) Revisar cambios en el sensor de puerta (respuesta inmediata)
        int door_state_now = (digitalRead(DOOR_SENSOR_PIN) == true) ? 1 : 0;
        if (door_state_now != last_door_state)
        {
          // Guardar el nuevo estado en memoria RTC
          int previous_state = last_door_state;
          last_door_state = door_state_now;
          
          display_door_status = door_state_now ? "Puerta:Abierta" : "Puerta:Cerrada";
          display_oled_message_3_line(display_temperature, display_humidity, display_door_status);

          Serial.printf("[CONTINUOUS] Cambio detectado: puerta %d -> %d\n", previous_state, door_state_now);

          // En modo continuo: registrar interacción de puerta mediante POST
          // Actualizar estado de batería antes de enviar
          get_battery_status();

          // Actualizar métricas OTA inmediatamente (incluye estado de puerta)
          ota_set_device_metrics(NAN, NAN, battery_level, door_state_now);

          // Garantizar conexión WiFi antes de intentar el POST
          if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[CONTINUOUS] Reconectando WiFi...");
            set_wifi_connection();
            delay(200); // breve espera a que la conexión se estabilice
          }

          if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[CONTINUOUS] Enviando POST: puerta=%d, batería=%dmV (%d%%)\n", 
                          door_state_now, battery_voltage, battery_level);
            send_POST_door_status_battery(door_state_now, battery_voltage, battery_level);
          } else {
            // Si no hay conexión, mostrar en serial
            Serial.println("[CONTINUOUS] No hay WiFi: no se pudo enviar POST de puerta");
          }
        }

        // 2) Revisar doble click: en lugar de cambiar a modo bateria, entrar en AP de configuración
        if (nonBlockingDoubleClickDetected(800))
        {
          Serial.println("[CONTINUOUS] Doble click detectado: borrando credenciales y entrando en AP de configuración...");
          display_oled_message_3_line("Formateando", "y entrando", "modo AP...");
          // Borrar credenciales para dejar el dispositivo 'formateado'
          erase_wifi_credentials();
          delay(200);
          // Iniciar portal de configuración (bloqueante)
          start_config_ap();
        }

        // 3) Actualizar temperatura y humedad cada 5s
        if (now - last_sensor_update >= 5000)
        {
          get_temperature_humidity();
          get_battery_status();
          int door_now = (digitalRead(DOOR_SENSOR_PIN) == true) ? 1 : 0;
          ota_set_device_metrics(temperature, humidity, battery_level, door_now);
          last_sensor_update = now;
          display_oled_message_3_line(display_temperature, display_humidity, display_door_status);
        }

        delay(100);
      }
    }
    else 
    {
      display_oled_message_3_line(
        "Inicio de modo", 
        "funcionamiento", 
        "normal"
      );
      delay(1000);

      // Permitir al usuario entrar en modo AP con doble click (formatear)
      int initialPresses = countButtonPressesWithinWindow(3000);
      if (initialPresses >= 2)
      {
        Serial.println("[NORMAL] Doble click detectado en modo NORMAL: borrando credenciales y entrando en AP...");
        display_oled_message_3_line("Formateando", "y entrando", "modo AP...");
        erase_wifi_credentials();
        delay(200);
        start_config_ap();
      }
    }
  }
  else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) 
  {
    //  Intentar conexión a la red Wi-Fi usando credenciales guardadas (no lanzar AP)
    bool wifi_ok = try_connect_wifi_no_ap();
    
    //  Se obtienen los valores de Temperatura, Humedad y Bateria.
    get_temperature_humidity();
    get_battery_status();
    display_oled_message_2_line(
      isnan(temperature) ? "--.-°C" : String(temperature, 1) + "°C",
      isnan(humidity) ? "--.-%" : String(humidity, 1) + "%"
    );

    // Permitir interacción vía botón PRG: una pulsación para "despertar" y doble para entrar en AP (formatear)
    int presses = countButtonPressesWithinWindow(3000); // 3s ventana para detectar interacción
    if (presses >= 2)
    {
      Serial.println("[TIMER_WAKE] Doble click detectado: borrando credenciales y entrando en AP de configuración...");
      display_oled_message_3_line("Formateando", "y entrando", "modo AP...");
      erase_wifi_credentials();
      delay(200);
      start_config_ap();
    }

    if (wifi_ok && WiFi.status() == WL_CONNECTED)
    {
      //  Se extrae la hora actual antes de entrar en el modo DeepSleep
      time_t current_time = get_time_NTP();
      deep_sleep_start_time = current_time;

      //  Se envian los valores de Temperatura, Humedad y Bateria.
      ota_set_device_metrics(temperature, humidity, battery_level, -1);
      send_POST_temperature_humidity_battery(temperature, humidity, battery_voltage, battery_level);
    }
    else
    {
      // Si no hay conexión, mostrar mensaje de error
      display_oled_message_3_line(
        "Sin conexión", 
        "a la red", 
        "Wi-Fi"
      );
      delay(500);
    }
  }
  else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) 
  {
    // Leer el estado actual de la puerta
    int door_state = digitalRead(DOOR_SENSOR_PIN) == true ? 1 : 0;
    String door_state_tag = door_state == 1 ? "ABIERTA" : "CERRADA";
    
    // Verificar si hubo un cambio real de estado
    bool state_changed = (last_door_state != door_state);
    
    Serial.println("[EXT0_WAKE] Despertar por cambio en sensor de puerta");
    Serial.printf("[EXT0_WAKE] Estado anterior: %d, Estado actual: %d\n", last_door_state, door_state);
    
    // Enviar POST independientemente de si es apertura o cierre
    // Solo si hubo un cambio real de estado (evitar duplicados)
    if (state_changed || last_door_state == -1)
    {
      //  Intentar conexión a la red Wi-Fi usando credenciales guardadas (no lanzar AP)
      bool wifi_ok = try_connect_wifi_no_ap();

      get_battery_status();
      ota_set_device_metrics(NAN, NAN, battery_level, door_state);
      display_oled_message_2_line(
        "Puerta:",
        door_state_tag
      );

      if (wifi_ok && WiFi.status() == WL_CONNECTED)
      {
        //  Se extrae la hora actual antes de entrar en el modo DeepSleep
        time_t wakeup_time = get_time_NTP();
        update_deep_sleep_time(wakeup_time);
        
        //  Se envian los valores de estado de puerta (0=cerrada, 1=abierta) y Bateria
        Serial.printf("[EXT0_WAKE] Enviando POST: puerta=%d, batería=%dmV (%d%%)\n", 
                      door_state, battery_voltage, battery_level);
        send_POST_door_status_battery(door_state, battery_voltage, battery_level);
        
        // Guardar el estado actual para la próxima comparación
        last_door_state = door_state;
      }
      else
      {
        // Si no hay conexión, mostrar mensaje de error
        Serial.println("[EXT0_WAKE] Sin conexión WiFi - no se pudo enviar POST");
        display_oled_message_3_line(
          "Sin conexión", 
          "a la red", 
          "Wi-Fi"
        );
        delay(500);
      }
    }
    else
    {
      Serial.println("[EXT0_WAKE] Sin cambio de estado real, omitiendo envío");
      display_oled_message_2_line(
        "Puerta:",
        door_state_tag
      );
      delay(500);
    }
  }
  else 
  {
    display_oled_message_3_line(
      "Sistema activo.", 
      "Esperando", 
      "reinicio"
    );
    delay(2000);
  }

  //  El sensor entra en modo DeepSleep
  enter_deep_sleep();
}

void loop() 
{
  // Este ciclo no se ejecutará
}