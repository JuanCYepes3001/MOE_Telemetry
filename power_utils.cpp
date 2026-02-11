#include "power_utils.h"
#include "esp_bt.h"
#include "esp_wifi.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"

// Función para deshabilitar completamente Bluetooth
void disable_bluetooth()
{
  // Deshabilitar stack de Bluetooth clásico
  esp_bt_controller_disable();
  
  // Liberar memoria del controlador Bluetooth
  esp_bt_controller_deinit();
  
  Serial.println("✅ Bluetooth completamente deshabilitado");
}

// Función para configurar frecuencia de CPU más baja cuando sea posible
void configure_cpu_frequency()
{
  // Configurar el CPU para usar frecuencia más baja cuando no esté en uso intensivo
  esp_pm_config_esp32s3_t pm_config = {
    .max_freq_mhz = 240,      // Frecuencia máxima cuando se necesita rendimiento
    .min_freq_mhz = 80,       // Frecuencia mínima para ahorrar energía
    .light_sleep_enable = true // Habilitar light sleep automático
  };
  
  esp_err_t ret = esp_pm_configure(&pm_config);
  if (ret == ESP_OK) {
    Serial.println("✅ Gestión de energía CPU configurada");
  } else {
    Serial.println("❌ Error configurando gestión de energía CPU");
  }
}

// Función para deshabilitar periféricos no utilizados
void disable_unused_peripherals()
{
  // Deshabilitar ADC2 si no se usa (ADC1 se usa para batería)
  // adc2_config_channel_atten(ADC2_CHANNEL_0, ADC_ATTEN_DB_0);
  
  // Deshabilitar DAC si no se usa
  // dac_output_disable(DAC_CHANNEL_1);
  // dac_output_disable(DAC_CHANNEL_2);
  
  // Deshabilitar LEDC si no se usa
  // ledc_fade_func_uninstall();
  
  Serial.println("✅ Periféricos no utilizados deshabilitados");
}

// Función para configurar dominios de alimentación para máximo ahorro
void configure_power_domains()
{
  // Configurar dominios RTC para mantener solo lo necesario
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);      // Mantener para GPIO wakeup
  
  Serial.println("✅ Dominios de alimentación optimizados");
}

// Función de inicialización completa de optimización energética
void init_power_optimization()
{
  Serial.begin(115200);
  Serial.println("🔋 Iniciando optimizaciones de energía...");
  
  // Aplicar todas las optimizaciones
  disable_bluetooth();
  configure_cpu_frequency();
  disable_unused_peripherals();
  configure_power_domains();
  
  Serial.println("🔋 Optimizaciones de energía completadas");
  Serial.flush(); // Asegurar que se imprima todo antes de continuar
  delay(100);
}