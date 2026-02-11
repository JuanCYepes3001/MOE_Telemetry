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
  }
  init_sensors();
  init_button_detector(); // Inicializar detector de botones

  // Conectar WiFi para OTA y sincronización
  Serial.println("[SETUP] Conectando WiFi...");
  display_oled_message_3_line(
    "Conectando",
    "a WiFi...",
    ""
  );
  delay(500);

  set_wifi_connection();

  // Mostrar IP si la conexión fue exitosa
  if (WiFi.status() == WL_CONNECTED)
  {
    String ip_str = WiFi.localIP().toString();
    Serial.println("[SETUP] ✓ WiFi conectado!");
    Serial.println("[SETUP] IP: " + ip_str);
    Serial.println("[SETUP] RSSI: " + String(WiFi.RSSI()) + " dBm");
    Serial.println("[SETUP] OTA disponible en: http://" + ip_str);
    
    display_oled_message_3_line(
      "v" + String(FIRMWARE_VERSION),
      "IP: " + ip_str,
      "OTA disponible"
    );
    delay(3000); // Mostrar IP durante 3 segundos

    // Iniciar servidor OTA en background
    Serial.println("[SETUP] Iniciando OTA en background...");
    Serial.flush(); // Asegurar que se envíe antes de crear la tarea
    
    init_ota_background();
    
    // Esperar a que OTA se haya inicializado
    delay(500);
    if (is_ota_active()) {
      Serial.println("[SETUP] ✓ OTA iniciado correctamente");
    } else {
      Serial.println("[SETUP] ✗ ERROR: OTA no se inició");
    }
  }
  else
  {
    Serial.println("[SETUP] ✗ Error: WiFi no disponible (status: " + String(WiFi.status()) + ")");
    display_oled_message_3_line(
      "v" + String(FIRMWARE_VERSION),
      "WiFi no",
      "disponible"
    );
    delay(1500);
  }

  //  Se determina el motivo por el cual el módulo despertó del DeepSleep
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  // Cargar modo persistente desde NVS (primera acción)
  current_mode = loadModeFromNVS(current_mode);


  if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) 
  {
    // Al arrancar en frio, usar modo persistente (por defecto: CONTINUO)
    if (current_mode == MODE_CONTINUOUS) 
    {
      display_oled_message_3_line(
        "Inicio de modo",
        "funcionamiento",
        "continuo"
      );
      delay(1000);

      // Bucle continuo: mostrar Temp, Hum y Estado de Puerta con respuesta inmediata a cambios
      // Variables para muestreo no bloqueante
      unsigned long last_sensor_update = 0;
      bool last_door_state = (digitalRead(DOOR_SENSOR_PIN) == true);

      // Lectura inicial
      get_temperature_humidity();
      get_battery_status();
      int initial_door = (digitalRead(DOOR_SENSOR_PIN) == true) ? 1 : 0;
      ota_set_device_metrics(temperature, humidity, battery_level, initial_door);
      display_door_status = last_door_state ? "Puerta:Abierta" : "Puerta:Cerrada";
      display_oled_message_3_line(display_temperature, display_humidity, display_door_status);

      while (true)
      {
        unsigned long now = millis();

        // 1) Revisar cambios en el sensor de puerta (respuesta inmediata)
        bool door_state_now = (digitalRead(DOOR_SENSOR_PIN) == true);
        if (door_state_now != last_door_state)
        {
          last_door_state = door_state_now;
          display_door_status = door_state_now ? "Puerta:Abierta" : "Puerta:Cerrada";
          display_oled_message_3_line(display_temperature, display_humidity, display_door_status);

          // En modo continuo: registrar interacción de puerta mediante POST
          // Actualizar estado de batería antes de enviar
          get_battery_status();

          // Actualizar métricas OTA inmediatamente (incluye estado de puerta)
          int door_now = door_state_now ? 1 : 0;
          ota_set_device_metrics(NAN, NAN, battery_level, door_now);

          // Garantizar conexión WiFi antes de intentar el POST
          if (WiFi.status() != WL_CONNECTED) {
            set_wifi_connection();
            delay(200); // breve espera a que la conexión se estabilice
          }

          if (WiFi.status() == WL_CONNECTED) {
            send_POST_door_status_battery(int(door_state_now), battery_voltage, battery_level);
          } else {
            // Si no hay conexión, mostrar en serial
            Serial.println("[CONTINUOUS] No hay WiFi: no se pudo enviar POST de puerta");
          }
        }

        // 2) Revisar doble click para cambiar a Normal
        if (nonBlockingDoubleClickDetected(800))
        {
          // Guardar modo en NVS para persistencia
          saveModeToNVS(MODE_NORMAL);
          current_mode = MODE_NORMAL;

          // Evitar re-lecturas por rebotes: esperar a que suelte el botón
          while (digitalRead(PRG_BUTTON_PIN) == LOW) delay(10);
          delay(300);

          display_oled_message_3_line("Cambiando a", "modo", "Normal");
          delay(700);
          esp_restart();
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

      // Permitir al usuario cambiar a modo continuo desde arranque con doble click
      int initialPresses = countButtonPressesWithinWindow(3000);
      if (initialPresses >= 2)
      {
        saveModeToNVS(MODE_CONTINUOUS);
        current_mode = MODE_CONTINUOUS;

        // Esperar a que se suelte el botón y evitar rebotes
        while (digitalRead(PRG_BUTTON_PIN) == LOW) delay(10);
        delay(300);

        display_oled_message_3_line("Cambiando a", "modo", "Continuo");
        delay(700);
        esp_restart();
      }
      else if (initialPresses == 1)
      {
        // Esperar si viene un segundo click para confirmar doble
        if (countButtonPressesWithinWindow(1000) >= 1)
        {
          saveModeToNVS(MODE_CONTINUOUS);
          current_mode = MODE_CONTINUOUS;

          while (digitalRead(PRG_BUTTON_PIN) == LOW) delay(10);
          delay(300);

          display_oled_message_3_line("Cambiando a", "modo", "Continuo");
          delay(700);
          esp_restart();
        }
      }
    }
  }
  else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) 
  {
    //  Se configura la conexión a la red Wi-Fi.
    set_wifi_connection();
    
    //  Se obtienen los valores de Temperatura, Humedad y Bateria.
    get_temperature_humidity();
    get_battery_status();
    display_oled_message_2_line(
      isnan(temperature) ? "--.-°C" : String(temperature, 1) + "°C",
      isnan(humidity) ? "--.-%" : String(humidity, 1) + "%"
    );

    // Permitir interacción vía botón PRG: una pulsación para "despertar" y doble para cambiar a modo continuo
    int presses = countButtonPressesWithinWindow(3000); // 3s ventana para detectar interacción
    if (presses >= 2)
    {
      // Doble click inmediato -> cambiar a modo continuo (persistente en NVS)
      saveModeToNVS(MODE_CONTINUOUS);
      current_mode = MODE_CONTINUOUS;

      while (digitalRead(PRG_BUTTON_PIN) == LOW) delay(10);
      delay(300);

      display_oled_message_3_line("Cambiando a", "modo", "Continuo");
      delay(700);
      esp_restart();
    }
    else if (presses == 1)
    {
      // Pulsación simple: esperar una segunda pulsación corta para confirmar doble click
      if (countButtonPressesWithinWindow(1000) >= 1)
      {
        saveModeToNVS(MODE_CONTINUOUS);
        current_mode = MODE_CONTINUOUS;

        while (digitalRead(PRG_BUTTON_PIN) == LOW) delay(10);
        delay(300);

        display_oled_message_3_line("Cambiando a", "modo", "Continuo");
        delay(700);
        esp_restart();
      }
      // Si no hay doble click, continuar con flujo normal
    }

    if (WiFi.status() == WL_CONNECTED)
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
    //El sensor envia LOW si los 2 imanes estan separados
    int door_state = digitalRead(DOOR_SENSOR_PIN) == true;
    String door_state_tag = door_state == true ? "ABIERTA" : "CERRADA";
    
    if (door_state == true)
    {
      //  Se configura la conexión a la red Wi-Fi
      set_wifi_connection();

      get_battery_status();
      int door_state_val = (digitalRead(DOOR_SENSOR_PIN) == true) ? 1 : 0;
      ota_set_device_metrics(NAN, NAN, battery_level, door_state_val);
      display_oled_message_2_line(
        "Puerta:",
        door_state_tag
      );

      if (WiFi.status() == WL_CONNECTED)
      {
        //  Se extrae la hora actual antes de entrar en el modo DeepSleep
        time_t wakeup_time = get_time_NTP();
        update_deep_sleep_time(wakeup_time);
        
        //  Se envian los valores de Apertura Puerta y Bateria.
        send_POST_door_status_battery(int(door_state), battery_voltage, battery_level);
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