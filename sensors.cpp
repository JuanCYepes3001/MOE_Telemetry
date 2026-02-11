#include "sensors.h"
#include "config.h"

// Definición del objeto DHT
DHT dht(DHT22_SENSOR_PIN, DHTTYPE);

// Inicializa los sensores
void init_sensors()
{
  dht.begin();
  pinMode(ADC_CTRL_PIN, OUTPUT);
}

//  Función que permite obtener la Temperatura y Humedad actual del sensor DHT22
void get_temperature_humidity()
{
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();

  display_temperature = isnan(temperature) ? "Temp: Error" : "Temp: " + String(temperature, 1) + " °C";
  display_humidity = isnan(humidity) ? "Hum: Error"  : "Hum: " + String(humidity, 1) + " %";

  delay(20);
}

// Función que me permite obtener el voltaje y el porcentaje actual de la bateria
void get_battery_status()
{ 
  analogReadResolution(12);
  digitalWrite(ADC_CTRL_PIN, HIGH);
  delay(10);

  int battery_adc = analogReadMilliVolts(VBAT_READ_PIN);
  digitalWrite(ADC_CTRL_PIN, LOW);

  battery_voltage = (int)(battery_adc * volt_div_factor);
  battery_level = map(battery_voltage, 3300, 4200, 0, 100);
  battery_level = constrain(battery_level, 0, 100);

  display_battery_voltage = "Bat: " + String(battery_voltage) + " mv";
  display_battery_level = "Bat: " + String(battery_level) + " %";
  delay(20);
}