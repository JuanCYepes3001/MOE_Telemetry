#include "http_utils.h"
#include "config.h"
#include "display_utils.h"
#include "HTTPClient.h"
#include "WiFi.h"
#include <ArduinoJson.h>


//  Función que permite enviar la temperatura, humedad y estado de la bateria al servidor mediante una solicitud HTTP
void send_POST_temperature_humidity_battery(float temp, float hum, int volt_batt, int porc_batt) 
{
  HTTPClient http;
  String mac = WiFi.macAddress();

  int temp_int = (int)(temp * 10);
  int hum_int  = (int)hum;

  //  Se crea el body con la información para enviar en la solicitud HTTP
  StaticJsonDocument<256> doc;

  doc["mac"] = mac;

  JsonObject values = doc.createNestedObject("values");
  values["temperature"] = temp_int;
  values["humidity"] = hum_int;

  JsonObject battery = doc.createNestedObject("battery");
  battery["voltage"] = volt_batt;
  battery["level"] = porc_batt;

  // Serializar a cadena
  String jsonPayload;
  serializeJson(doc, jsonPayload);

  http.begin(endpoint_telemetry);
  http.addHeader("Content-Type", "application/json");

  // ⏳ Timeout de 5 segundos
  http.setTimeout(5000);

  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode > 0) 
  {
    // Mostrar sólo confirmación (sin el mensaje "Apagando...")
    display_oled_message_2_line(
      "Información registrada.", 
      ""
    );
  } 
  else 
  {
    display_oled_message_2_line(
      "Información no registrada.", 
      ""
    );
  }

  http.end();
  delay(500);
}


//  Función que permite enviar el estado de apertura de la puerta y de la bateria al servidor mediante una solicitud HTTP
void send_POST_door_status_battery(int door_status, int battery_vol, int battery_lvl) 
{
  HTTPClient http;
  String mac = WiFi.macAddress();

  //  Se crea el body con la información para enviar en la solicitud HTTP
  StaticJsonDocument<256> doc;

  // Construir estructura JSON
  doc["mac"] = mac;
  doc["door_status"] = door_status;

  JsonObject battery = doc.createNestedObject("battery");
  battery["voltage"] = battery_vol;
  battery["level"] = battery_lvl;

  // Serializar a String
  String jsonPayload;
  serializeJson(doc, jsonPayload);

  // Enviar POST
  http.begin(endpoint_door_sensor);
  http.addHeader("Content-Type", "application/json");

  // ⏳ Timeout de 5 segundos
  http.setTimeout(5000);

  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode > 0) 
  {
    // Mostrar sólo confirmación (sin el mensaje "Apagando...")
    display_oled_message_2_line(
      "Información registrada.", 
      ""
    );
  } 
  else 
  {
    display_oled_message_2_line(
      "Información NO registrada.", 
      ""
    );
  }

  http.end();
  delay(500);
}