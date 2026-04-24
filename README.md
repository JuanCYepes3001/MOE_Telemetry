# MOE Telemetry

Firmware IoT para monitoreo ambiental y estado de puertas sobre **Heltec WiFi LoRa 32 V3 (ESP32-S3)**, diseñado para operar en modo de bajo consumo o en modo continuo con interfaz web OTA. Este proyecto permite medir **temperatura, humedad, batería y estado de puerta**, enviar eventos a un backend vía WiFi y facilitar la configuración remota sin recompilar el dispositivo. [file:1][file:2]

## Descripción

MOE Telemetry está pensado para espacios como salas, laboratorios o instalaciones donde se necesita registrar variables ambientales y detectar aperturas o cierres de puerta. El firmware soporta dos modos principales: un modo normal con **deep sleep periódico** para maximizar autonomía, y un modo continuo para tareas de configuración, monitoreo en tiempo real y actualizaciones OTA. [file:1]

Además de la lógica de telemetría, el sistema incluye un **portal AP de configuración WiFi**, una **interfaz web OTA**, almacenamiento persistente en NVS y wakeup por evento mediante sensor magnético. Esto lo hace útil tanto para despliegues productivos con batería como para operación conectada por USB-C. [file:1][file:2]

## Funcionalidades

- Monitoreo de **temperatura y humedad** con sensor DHT22. [file:1]
- Detección de **apertura/cierre de puerta** con reed switch. [file:1]
- Lectura de **voltaje y nivel de batería**. [file:1]
- Envío de datos por **HTTP POST / HTTPS** a endpoints de backend. [file:1]
- **Modo normal** con deep sleep por intervalo configurable. [file:1]
- **Wakeup inmediato por evento** cuando cambia el estado de la puerta. [file:1]
- **Modo continuo** para monitoreo en tiempo real y mantenimiento. [file:1][file:2]
- **Portal de configuración WiFi** en modo Access Point. [file:1][file:2]
- **Actualización OTA** desde interfaz web. [file:1][file:2]
- Persistencia de credenciales, intervalo y modo usando **NVS**. [file:1]

## Hardware

### Plataforma principal

- **Board:** Heltec WiFi LoRa 32 V3. [file:1]
- **MCU:** ESP32-S3. [file:1]
- **Display:** OLED SSD1306 integrado. [file:1]
- **Alimentación:** USB-C o batería LiPo. [file:1][file:2]

### Sensores y conexiones

- **DHT22 / AM2302:** lectura de temperatura y humedad en GPIO46. [file:1]
- **Reed switch:** detección de estado de puerta en GPIO19 con wakeup EXT0. [file:1]
- **Lectura de batería:** ADC en GPIO1 con control de divisor en GPIO37. [file:1]

## Arquitectura

La arquitectura del firmware está organizada en capas para separar la lógica de aplicación, servicios, HAL y configuración central. La documentación técnica describe módulos especializados para WiFi, HTTP, OTA, sensores, display, energía, sueño profundo y manejo de botones. [file:1]

### Módulos principales

| Módulo | Responsabilidad |
|---|---|
| `config` | Pines GPIO, constantes, URLs, variables compartidas. [file:1] |
| `displayutils` | Control de display OLED, mensajes y gestión de Vext. [file:1] |
| `sensors` | Lectura de DHT22 y batería. [file:1] |
| `buttonutils` | Detección no bloqueante de doble/triple click. [file:1] |
| `wifiutils` | Conexión WiFi, NVS, portal AP, sincronización NTP. [file:1] |
| `httputils` | Construcción y envío de payloads HTTP POST. [file:1] |
| `sleeputils` | Configuración de deep sleep y wakeup sources. [file:1] |
| `powerutils` | Optimización de consumo energético. [file:1] |
| `otautils` | Servidor web OTA y panel de monitoreo/configuración. [file:1] |

## Flujo de operación

1. El dispositivo inicia periféricos, display, sensores y optimizaciones de energía. [file:1]
2. Si no hay credenciales WiFi o se fuerza el modo AP, levanta un portal de configuración. [file:1][file:2]
3. En modo normal, despierta por temporizador o por cambio del reed switch. [file:1]
4. Lee sensores, construye el payload y envía datos al backend. [file:1]
5. Si está en modo normal, apaga periféricos y vuelve a deep sleep. [file:1]
6. Si está en modo continuo, mantiene la interfaz OTA activa y sigue actualizando métricas. [file:1][file:2]

## Modos de operación

| Modo | Uso recomendado | Comportamiento |
|---|---|---|
| Normal | Operación con batería, máxima autonomía. [file:2] | El dispositivo duerme por intervalos y despierta por timer o por evento de puerta. [file:1] |
| Continuo | Configuración, depuración, acceso web frecuente, OTA. [file:2] | Permanece activo, con WiFi e interfaz OTA disponibles. [file:1][file:2] |

## Configuración WiFi

Cuando el dispositivo no tiene credenciales guardadas, activa automáticamente un **Access Point** para configuración inicial. También puede forzarse mediante interacción con el botón PRG y luego conectarse a una red tipo `MOETelemetryXXXX` para abrir el portal en `http://192.168.4.1`. [file:1][file:2]

Desde ese portal se pueden escanear redes, guardar SSID y contraseña, reiniciar el equipo y, si aplica, ejecutar acciones de reseteo relacionadas con la configuración WiFi. El dispositivo solo soporta redes **2.4 GHz**. [file:2]

## Interfaz web y OTA

Una vez conectado a la red local, el dispositivo expone una interfaz web accesible desde su dirección IP. Esta interfaz muestra métricas en tiempo real, permite cambiar entre modo normal y continuo, ajustar el intervalo de medición y subir un nuevo firmware `.bin`. [file:1][file:2]

Para actualizar firmware OTA, se recomienda activar modo continuo, verificar suficiente batería o conectar USB-C, cargar el binario desde el navegador y esperar el reinicio automático del dispositivo. [file:2]

## Dependencias

### Framework y core

- Arduino IDE 2.x. [file:1]
- ESP32 Arduino Core 2.0.0. [file:1]

### Librerías principales

- `WiFi.h` [file:1]
- `HTTPClient.h` [file:1]
- `WebServer.h` [file:1]
- `DNSServer.h` [file:1]
- `Preferences.h` [file:1]
- `Update.h` [file:1]
- `ArduinoJson` 6.19.0. [file:1]
- `DHT sensor library` 1.4.0. [file:1]
- `Heltec ESP32 Dev-Boards` 1.1.0. [file:1]

## Configuración de compilación

La documentación técnica recomienda usar la board **Heltec WiFi LoRa 32 V3**, velocidad de carga **921600**, CPU a **240 MHz**, flash mode **QIO** y esquema de partición **Default 4MB with spiffs**. [file:1]

### Pasos básicos

1. Abrir el archivo principal del proyecto en Arduino IDE. [file:1]
2. Seleccionar la board correcta en **Tools > Board**. [file:1]
3. Elegir el puerto correspondiente. [file:1]
4. Compilar con **Verify/Compile**. [file:1]
5. Subir por USB o exportar el binario para actualización OTA. [file:1][file:2]

## Endpoints y payloads

El firmware utiliza una URL base configurada en `config` y separa al menos dos endpoints: uno para telemetría ambiental y otro para estado de puerta. Los payloads incluyen la MAC del dispositivo y datos como temperatura, humedad, voltaje, nivel de batería o estado de puerta según el evento detectado. [file:1]

## Gestión de energía

El proyecto está optimizado para bajo consumo con apagado de WiFi, desactivación de Bluetooth, control de dominios de energía y uso de deep sleep con wakeup por timer y EXT0. La documentación reporta perfiles aproximados de consumo de 80–120 mA con WiFi activo, 30–50 mA con WiFi apagado en activo y alrededor de 10 µA en deep sleep. [file:1]

## Estructura sugerida del repo

```text
.
├── README.md
├── main.ino
├── config.h
├── config.cpp
├── displayutils.*
├── sensors.*
├── buttonutils.*
├── wifiutils.*
├── httputils.*
├── sleeputils.*
├── powerutils.*
├── otautils.*
└── images.*
```

La documentación técnica muestra una organización modular basada en archivos de utilidades por dominio funcional. Si tu repositorio tiene nombres o carpetas distintos, esta sección puede adaptarse para reflejar la estructura real. [file:1]

## Casos de uso

- Monitoreo ambiental de salas o laboratorios. [file:1]
- Registro periódico de temperatura y humedad. [file:1]
- Detección de apertura y cierre de puertas. [file:1]
- Despliegues autónomos con batería LiPo. [file:1]
- Mantenimiento remoto mediante OTA y configuración web. [file:1][file:2]

## Seguridad y recomendaciones

- No sumergir el dispositivo ni exponerlo a condiciones fuera del rango permitido por el hardware. [file:2][file:1]
- No abrir la carcasa si integra batería LiPo. [file:2]
- No desconectar sensores con el dispositivo energizado. [file:2]
- Para OTA, evitar apagar o desconectar el equipo durante la carga del firmware. [file:2]
- Para máxima autonomía, volver a modo normal después de configurar o actualizar. [file:2]

## Estado del proyecto

La documentación adjunta identifica el firmware como **versión 3.1.0** y el manual de usuario como **versión 1.2.0**, con documentación fechada en febrero de 2026. [file:1][file:2]

## Autor

**Juan Camilo Yepes**. [file:1]
