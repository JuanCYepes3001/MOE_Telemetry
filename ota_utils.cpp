#include "ota_utils.h"
#include "config.h"
#include "display_utils.h"
#include "wifi_utils.h"
#include "button_utils.h"
#include "images.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include <Update.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <math.h> // para isnan

// Servidor web para OTA (global)
WebServer server(80);
bool ota_active = false;
TaskHandle_t ota_task_handle = NULL;

// Persistent preferences namespace and runtime flag for "continuous" mode
static Preferences ota_prefs;
static bool ota_continuous_mode = false;

// Forward declaration: application may implement ota_on_mode_changed elsewhere
void ota_on_mode_changed(bool continuous);

// Variables compartidas para métricas del dispositivo (por defecto no disponibles)
static float ota_temp_c = NAN;
static float ota_humidity_pct = NAN;
static int ota_battery_pct = -1;
// -1 = unknown, 0 = closed, 1 = open
static int ota_door_state = -1;

// Setter público (declarado en ota_utils.h)
void ota_set_device_metrics(float temp_c, float humidity_pct, int battery_pct, int door_state)
{
  ota_temp_c = temp_c;
  ota_humidity_pct = humidity_pct;
  ota_battery_pct = battery_pct;
  ota_door_state = door_state;
}

// Simple base64 decoder for PNG data (returns decoded length)
static size_t base64_decode(const char* src, uint8_t* out) {
  if (!src || !out) return 0;
  const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  unsigned char dtable[256];
  memset(dtable, 0x80, 256);
  for (int i = 0; i < 64; i++) dtable[(unsigned char)b64[i]] = i;
  dtable['='] = 0;

  size_t len = strlen(src);
  size_t out_len = 0;
  unsigned int val = 0;
  int valb = -8;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = src[i];
    if (dtable[c] & 0x80) continue; // skip non-base64
    val = (val << 6) + dtable[c];
    valb += 6;
    if (valb >= 0) {
      out[out_len++] = (unsigned char)((val >> valb) & 0xFF);
      valb -= 8;
    }
  }
  return out_len;
}

// HTML embebido para la interfaz OTA (modificado: añadida sección de Device Info y fetch a /update/device_info)
const char* ota_html = R"(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>MOE Telemetry - Gestor de Actualizaciones</title>
  <style>
    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
    }
    
    :root {
      --bg: rgb(0,0,0); /* página general detrás de la card */
      --card-bg: rgb(20,20,20); /* tarjeta central */
      --accent: rgb(0,140,226);
      --accent-dark: #007bb5;
      --info-bg: rgba(0,140,226,0.04); /* instrucciones: sutil azul acento */
      --muted: #6c757d; /* botón limpiar menos llamativo */
      --text: #ffffff;
      --update-green: #28a745;
      --reset-red: #dc3545;
    }

    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
      background: var(--bg);
      color: var(--text);
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 20px;
    }

    .container {
      background: var(--card-bg);
      position: relative;
      border-radius: 15px;
      box-shadow: 0 18px 50px rgba(0, 0, 0, 0.7);
      max-width: 600px;
      width: 100%;
      overflow: hidden;
      border: 1px solid rgba(255,255,255,0.03);
    }

    /* faint developer signature in background */
    .signature {
      position: absolute;
      left: 0;
      right: 0;
      bottom: 6px;
      text-align: center;
      font-size: 10px;
      color: #ffffff;
      opacity: 0.04;
      pointer-events: none;
      user-select: none;
    }

    /* Battery switch */
    .battery-switch { display:flex; align-items:center; gap:8px; justify-content:center; }
    .battery-switch input[type="checkbox"] { width: 40px; height: 20px; appearance: none; background: #444; border-radius: 12px; position: relative; cursor:pointer; outline:none; }
    .battery-switch input[type="checkbox"]:checked { background: var(--accent); }
    .battery-switch input[type="checkbox"]::after { content: ''; position: absolute; top: 3px; left: 3px; width: 14px; height:14px; background: #fff; border-radius:50%; transition: transform 0.15s ease; }
    .battery-switch input[type="checkbox"]:checked::after { transform: translateX(20px); }
    .battery-pct { font-weight:700; color:var(--accent); font-size:14px; }

    .header {
      /* grayscale gradient that contrasts with black and the blue accent */
      background: linear-gradient(135deg, #222222 0%, #4a4a4a 100%);
      color: var(--text);
      padding: 36px 30px;
      text-align: center;
    }
    
    .header h1 {
      font-size: 32px;
      margin-bottom: 10px;
      font-weight: 700;
      color: var(--accent);
    }
    
    .header p {
      font-size: 16px;
      opacity: 0.9;
      margin-bottom: 5px;
    }
    
    .version-badge {
      display: inline-block;
      background: rgba(255, 255, 255, 0.03);
      padding: 5px 15px;
      border-radius: 20px;
      font-size: 13px;
      margin-top: 10px;
      font-weight: 600;
      color: var(--accent);
      border: 1px solid rgba(255,255,255,0.02);
    }
    
    .content {
      padding: 40px;
    }
    
    .info-section {
      background: var(--info-bg);
      border-left: 4px solid var(--accent);
      padding: 20px;
      color: var(--text);
      border-radius: 8px;
      margin-bottom: 30px;
    }

    .info-section h3 {
      color: var(--text);
      font-size: 16px;
      margin-bottom: 10px;
      display: flex;
      align-items: center;
      gap: 10px;
    }

    /* Logo styling to increase visibility and be responsive */
    #logo {
      width: 100%;
      max-width: 260px; /* maximum visual width */
      height: auto;
      max-height: 140px; /* cap height so header doesn't grow too much */
      display: block;
      margin: 0 auto 8px;
      background: rgba(255,255,255,0.02);
      padding: 8px;
      border-radius: 8px;
      object-fit: contain;
    }

    .device-info {
      display: flex;
      gap: 12px;
      justify-content: space-between;
      flex-wrap: wrap;
      color: var(--text);
      padding: 16px;
      background: transparent;
      border: 1px solid rgba(255,255,255,0.03);
      border-radius: 10px;
      margin: 20px 0;
      align-items: center;
      box-shadow: 0 6px 18px rgba(0,0,0,0.6);
    }

    /* Toggle switch styling (visual switch built from checkbox) */
    .toggle-switch {
      position: relative;
      width: 48px;
      height: 26px;
      background: #444;
      border-radius: 16px;
      cursor: pointer;
      transition: background 0.15s ease;
      display:inline-block;
    }
    .toggle-switch input { display:none; }
    .toggle-switch .knob {
      position: absolute;
      top: 3px;
      left: 3px;
      width: 20px;
      height: 20px;
      background: white;
      border-radius: 50%;
      transition: transform 0.15s ease;
      box-shadow: 0 2px 6px rgba(0,0,0,0.3);
    }
    .toggle-switch.checked { background: var(--accent); }
    .toggle-switch.checked .knob { transform: translateX(22px); }

    .device-info .metric {
      flex: 1 1 120px;
      text-align: center;
      min-width: 120px;
      margin-bottom: 8px;
    }

    .device-info .label {
      display: block;
      font-size: 12px;
      color: #666;
      color: rgba(255,255,255,0.7);
    }

    .device-info .value {
      font-size: 16px;
      font-weight: 700;
      color: var(--accent);
    }

    /* Slight right-offset for SSID value for visual balance */
    #deviceSSID { padding-left: 10px; }
    
    .info-section ol {
      margin-left: 20px;
      color: var(--text);
      line-height: 1.8;
      font-size: 14px;
    }
    
    .info-section li {
      margin-bottom: 8px;
    }
    
    .file-upload-section {
      margin-bottom: 30px;
    }
    
    .file-input-wrapper {
      position: relative;
      display: block;
      cursor: pointer;
    }
    
    input[type="file"] {
      position: absolute;
      left: -9999px;
      opacity: 0;
    }
    
    .file-input-label {
      display: block;
      padding: 20px;
      background: linear-gradient(135deg, var(--accent) 0%, var(--accent-dark) 100%);
      color: white;
      text-align: center;
      border-radius: 10px;
      transition: all 0.3s ease;
      font-weight: 600;
      cursor: pointer;
      border: 2px dashed transparent;
    }
    
    .file-input-label:hover {
      transform: translateY(-2px);
      box-shadow: 0 10px 25px rgba(102, 126, 234, 0.4);
    }
    
    .file-input-label:active {
      transform: translateY(0);
    }
    
    .file-input-wrapper.drag-over .file-input-label {
      background: linear-gradient(135deg, #764ba2 0%, #667eea 100%);
      border-color: white;
    }
    
    #fileName {
      color: var(--accent);
      font-size: 14px;
      margin-top: 10px;
      text-align: center;
      font-weight: 600;
      display: none;
    }
    
    #fileName.active {
      display: block;
    }
    
    .button-group {
      display: flex;
      gap: 10px;
    }
    
    #uploadBtn {
      flex: 1;
      padding: 15px;
      background: linear-gradient(90deg, var(--update-green) 0%, #218838 100%);
      color: white;
      border: none;
      border-radius: 10px;
      font-size: 16px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.3s ease;
    }
    
    #uploadBtn:hover:not(:disabled) {
      transform: translateY(-2px);
      box-shadow: 0 10px 25px rgba(40, 167, 69, 0.4);
    }
    
    #uploadBtn:disabled {
      background: #ccc;
      cursor: not-allowed;
      opacity: 0.6;
    }
    
    #resetBtn {
      padding: 15px 25px;
      background: linear-gradient(90deg, var(--muted) 0%, #5a636b 100%);
      color: white;
      border: none;
      border-radius: 10px;
      font-size: 16px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.3s ease;
      box-shadow: 0 6px 16px rgba(0,0,0,0.4);
    }
    
    #resetBtn:hover { transform: translateY(-2px); }
    
    #resetBtn:active { transform: translateY(0); }
    
    #progress {
      margin-top: 30px;
      display: none;
    }
    
    #progress.active {
      display: block;
    }
    
    .progress-container {
      margin-bottom: 15px;
    }
    
    .progress-label {
      display: flex;
      justify-content: space-between;
      font-size: 14px;
      color: var(--text);
      margin-bottom: 8px;
    }
    
    .progress-bar {
      width: 100%;
      height: 30px;
      background: #e0e0e0;
      border-radius: 15px;
      overflow: hidden;
      border: 1px solid #ddd;
    }
    
    .progress-fill {
      height: 100%;
      background: linear-gradient(90deg, var(--accent) 0%, var(--accent) 100%);
      width: 0%;
      transition: width 0.3s ease;
      display: flex;
      align-items: center;
      justify-content: center;
      color: white;
      font-size: 12px;
      font-weight: 600;
    }
    
    .status-message {
      margin-top: 15px;
      padding: 15px;
      border-radius: 8px;
      font-weight: 600;
      text-align: center;
      font-size: 14px;
    }
    
    .status-message.info {
      background: rgba(255,255,255,0.03);
      color: var(--text);
      border-left: 4px solid var(--accent);
    }

    .status-message.success {
      background: rgba(0,0,0,0.15);
      color: var(--text);
      border-left: 4px solid var(--update-green);
    }

    .status-message.error {
      background: rgba(0,0,0,0.15);
      color: var(--text);
      border-left: 4px solid var(--reset-red);
    }

    .factory-button {
      background: linear-gradient(90deg, #d32f2f 0%, #b71c1c 100%);
      color: white;
      border: none;
      padding: 12px 18px;
      border-radius: 10px;
      font-weight: 700;
      cursor: pointer;
      box-shadow: 0 8px 20px rgba(179,28,28,0.18);
      transition: transform 0.12s ease;
    }

    .factory-button:hover { transform: translateY(-3px); }

    .factory-button:active { transform: translateY(0); }
    
    .warning-box {
      background: #fff3e0;
      border-left: 4px solid #f57c00;
      padding: 15px;
      border-radius: 8px;
      margin-bottom: 20px;
      color: #e65100;
      font-size: 14px;
      line-height: 1.6;
    }

    .footer {
      background: transparent;
      padding: 20px;
      text-align: center;
      color: rgba(255,255,255,0.6);
      font-size: 12px;
      border-top: 1px solid rgba(255,255,255,0.02);
    }

    @media (max-width: 480px) {
      .container {
        border-radius: 0;
      }
      
      .header {
        padding: 30px 20px;
      }
      
      .header h1 {
        font-size: 24px;
      }
      
      .content {
        padding: 20px;
      }
      
      .button-group {
        flex-direction: column;
      }
      /* Make device-info metrics wrap into two columns on narrow screens */
      .device-info { padding: 12px; }
      .device-info .metric { flex: 1 1 45%; min-width: 45%; }
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1><img id="logo" src="__LOGO__" alt="MOE"><br>MOE Telemetry</h1>
      <p>Gestor de Actualizaciones OTA</p>
      <div style="display:flex;gap:8px;align-items:center;justify-content:center;margin-top:10px;">
        <div class="version-badge" id="deviceVersion">Conectando...</div>
      </div>
    </div>
    
    <!-- Device info: temperatura, humedad, batería, IP, MAC -->
    <!-- Device info split: sensors and network separated into two containers -->
    <div style="display:flex;flex-direction:column;gap:12px;margin-top:12px;">
      <div class="device-info" id="sensorsInfo" style="width:100%;">
        <div class="metric">
          <span class="label">Temperatura</span>
          <span class="value" id="deviceTemp">--.-°C</span>
        </div>
        <div class="metric">
          <span class="label">Humedad</span>
          <span class="value" id="deviceHumidity">--.-%</span>
        </div>
        <div class="metric" style="display:flex;flex-direction:column;align-items:center;">
          <span class="label">Batería</span>
          <div style="display:flex;gap:12px;align-items:center;">
            <label class="toggle-switch" id="batterySwitchWrap">
              <input type="checkbox" id="batterySwitch">
              <span class="knob"></span>
            </label>
          </div>
        </div>
        <div class="metric">
          <span class="label">Puerta</span>
          <span class="value" id="deviceDoor">--</span>
        </div>
      </div>

      <div class="device-info" id="networkInfo" style="width:100%;">
        <div class="metric">
          <span class="label">IP</span>
          <span class="value" id="deviceIP">--</span>
        </div>
        <div class="metric">
          <span class="label">MAC</span>
          <span class="value" id="deviceMAC">--</span>
        </div>
        <div class="metric">
          <span class="label">Red</span>
          <span class="value" id="deviceSSID">--</span>
        </div>
        <div class="metric">
          <span class="label">Intensidad</span>
          <span class="value" id="deviceRSSI">-- dBm</span>
        </div>
        <!-- continuous switch moved to battery block -->
      </div>
    </div>
    <div class="signature">Programa creado por: Ing. Juan Camilo Yepes</div>
    
    <div class="content">
      <div class="warning-box">
        ⚠️ <strong>Importante:</strong> No apagues el dispositivo durante la actualización. El proceso tarda entre 30-60 segundos.
      </div>
      
      <div class="info-section">
        <h3>📋 Instrucciones</h3>
        <ol>
          <li>Selecciona el archivo <strong>.bin</strong> compilado del firmware</li>
          <li>Verifica que sea la versión correcta</li>
          <li>Haz clic en <strong>"Iniciar Actualización"</strong></li>
          <li>Espera a que el progreso llegue al 100%</li>
          <li>El dispositivo se reiniciará automáticamente</li>
        </ol>
      </div>
      
        <div class="file-upload-section">
          <label class="file-input-wrapper" id="fileWrapper">
            <input type="file" id="fileInput" accept=".bin" />
            <span class="file-input-label">
              📁 Selecciona o arrastra archivo .bin aquí
            </span>
          </label>
          <div id="fileName"></div>
        </div>

        <!-- Logo upload removed: using LittleFS /logo.png or embedded image fallback -->
      
      <div class="button-group">
        <button id="uploadBtn" disabled>Iniciar Actualización</button>
        <button id="resetBtn">Limpiar</button>
      </div>
      
      <div id="progress">
        <div class="progress-container">
          <div class="progress-label">
            <span>Progreso de carga</span>
            <span id="progressPercent">0%</span>
          </div>
          <div class="progress-bar">
            <div class="progress-fill" id="progressFill"></div>
          </div>
        </div>
        <div class="status-message info" id="status">⏳ Subiendo firmware...</div>
      </div>
    </div>
    
    <div class="footer">
      <p>MOE Telemetry OTA System | Actualización segura vía WiFi</p>
      <div style="display:flex;justify-content:center;margin-top:12px;">
        <button id="factoryBtnFooter" class="factory-button">Restablecer de fábrica</button>
      </div>
    </div>
  </div>

  <script>
    const fileInput = document.getElementById('fileInput');
    const fileWrapper = document.getElementById('fileWrapper');
    const uploadBtn = document.getElementById('uploadBtn');
    const resetBtn = document.getElementById('resetBtn');
    const fileName = document.getElementById('fileName');
    const progressDiv = document.getElementById('progress');
    const progressFill = document.getElementById('progressFill');
    const progressPercent = document.getElementById('progressPercent');
    const status = document.getElementById('status');
    const deviceVersion = document.getElementById('deviceVersion');
    const factoryBtn = document.getElementById('factoryBtnFooter');
    const deviceTemp = document.getElementById('deviceTemp');
    const deviceHumidity = document.getElementById('deviceHumidity');
    const deviceDoor = document.getElementById('deviceDoor');
    const deviceIP = document.getElementById('deviceIP');
    const deviceMAC = document.getElementById('deviceMAC');
    const deviceSSID = document.getElementById('deviceSSID');
    const deviceRSSI = document.getElementById('deviceRSSI');
    const batterySwitch = document.getElementById('batterySwitch');
    const batterySwitchWrap = document.getElementById('batterySwitchWrap');
    // continuousNote removed
    // logo upload controls removed

    // Obtener versión del dispositivo
    fetch('/update/identity')
      .then(r => r.json())
      .then(data => {
        if (data.version) {
          deviceVersion.textContent = `v${data.version}`;
        }
      })
      .catch(() => {
        deviceVersion.textContent = 'Versión desconocida';
      });

    // Obtener información del dispositivo (métricas + red)
    function fetchDeviceInfo() {
      fetch('/update/device_info')
        .then(r => r.json())
        .then(data => {
          if (data.temperature !== undefined && data.temperature !== null) {
            deviceTemp.textContent = `${data.temperature.toFixed(1)} °C`;
          } else {
            deviceTemp.textContent = '--.-°C';
          }

          if (data.humidity !== undefined && data.humidity !== null) {
            deviceHumidity.textContent = `${data.humidity.toFixed(1)} %`;
          } else {
            deviceHumidity.textContent = '--.-%';
          }

                // Battery information intentionally hidden in this UI; switch controls mode

          if (data.door !== undefined && data.door !== null) {
            deviceDoor.textContent = data.door == 1 ? 'Abierta' : 'Cerrada';
          } else {
            deviceDoor.textContent = '--';
          }

          if (data.ip) deviceIP.textContent = data.ip; else deviceIP.textContent = '--';
          if (data.mac) deviceMAC.textContent = data.mac; else deviceMAC.textContent = '--';

          if (data.ssid) deviceSSID.textContent = data.ssid; else deviceSSID.textContent = '--';
          if (data.rssi !== undefined && data.rssi !== null) deviceRSSI.textContent = `${data.rssi} dBm`; else deviceRSSI.textContent = '-- dBm';
          // After updating device info, refresh mode UI (depends on battery presence)
          fetchMode();
        })
        .catch(err => {
          console.warn('fetchDeviceInfo failed', err);
        });
    }

    // Fetch current persistent mode and update switch
    function fetchMode() {
      fetch('/update/mode')
        .then(r => r.json())
        .then(m => {
          // UI mapping: switch ON -> continuous mode
          batterySwitch.checked = !!m.continuous;
          if (batterySwitch.checked) batterySwitchWrap.classList.add('checked'); else batterySwitchWrap.classList.remove('checked');
        })
        .catch(err => { console.warn('fetchMode failed', err); });
    }

    batterySwitch.addEventListener('change', () => {
      // UI mapping: switch ON -> continuous mode
      const wantContinuous = !!batterySwitch.checked;
      // update visual wrapper immediately for snappy UI
      if (batterySwitch.checked) batterySwitchWrap.classList.add('checked'); else batterySwitchWrap.classList.remove('checked');
      fetch('/update/mode', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ continuous: wantContinuous }) })
        .then(r => {
          if (!r.ok) return r.text().then(t => Promise.reject(t));
          return r.json();
        })
        .then(j => {
          // server responded with effective mode; update UI to match
          batterySwitch.checked = !!j.continuous;
          if (batterySwitch.checked) batterySwitchWrap.classList.add('checked'); else batterySwitchWrap.classList.remove('checked');
        })
        .catch(err => {
          console.warn('set mode failed', err);
          // Revert UI
          fetchMode();
        });
    });

    // Inicializar datos y refrescar periódicamente
    fetchDeviceInfo();
    setInterval(fetchDeviceInfo, 5000);

    // Factory reset desde UI OTA (footer) con confirmación modal ligera
    factoryBtn.addEventListener('click', () => {
      // Mostrar diálogo personalizado usando confirm() para simplicidad
      if (!confirm('ATENCIÓN: Esto borrará las credenciales WiFi y reiniciará el dispositivo. ¿Continuar?')) return;
      status.textContent = 'Borrando credenciales y reiniciando...';
      status.className = 'status-message info';
      fetch('/factory_reset', { method: 'POST' })
        .then(r => {
          if (r.ok) {
            status.textContent = '✓ Reiniciando...';
            status.className = 'status-message success';
            setTimeout(() => { window.location.reload(); }, 3000);
          } else {
            status.textContent = '✗ Error al resetear';
            status.className = 'status-message error';
          }
        })
        .catch(() => {
          status.textContent = '✗ Error de conexión al resetear';
          status.className = 'status-message error';
        });
    });

    // Drag and drop
    ['dragenter', 'dragover', 'dragleave', 'drop'].forEach(eventName => {
      fileWrapper.addEventListener(eventName, preventDefaults, false);
    });

    function preventDefaults(e) {
      e.preventDefault();
      e.stopPropagation();
    }

    ['dragenter', 'dragover'].forEach(eventName => {
      fileWrapper.addEventListener(eventName, () => {
        fileWrapper.classList.add('drag-over');
      }, false);
    });

    ['dragleave', 'drop'].forEach(eventName => {
      fileWrapper.addEventListener(eventName, () => {
        fileWrapper.classList.remove('drag-over');
      }, false);
    });

    fileWrapper.addEventListener('drop', (e) => {
      const dt = e.dataTransfer;
      const files = dt.files;
      fileInput.files = files;
      handleFileSelect();
    }, false);

    fileInput.addEventListener('change', handleFileSelect);

    function handleFileSelect() {
      if (fileInput.files.length > 0) {
        const file = fileInput.files[0];
        fileName.textContent = `✓ Archivo seleccionado: ${file.name} (${formatFileSize(file.size)})`;
        fileName.classList.add('active');
        uploadBtn.disabled = false;
      }
    }

    function formatFileSize(bytes) {
      if (bytes === 0) return '0 Bytes';
      const k = 1024;
      const sizes = ['Bytes', 'KB', 'MB'];
      const i = Math.floor(Math.log(bytes) / Math.log(k));
      return Math.round((bytes / Math.pow(k, i)) * 100) / 100 + ' ' + sizes[i];
    }

    resetBtn.addEventListener('click', () => {
      fileInput.value = '';
      fileName.classList.remove('active');
      uploadBtn.disabled = true;
      progressDiv.classList.remove('active');
      status.textContent = '';
    });

    // logo upload handlers removed

    uploadBtn.addEventListener('click', () => {
      const file = fileInput.files[0];
      if (!file) return;

      // Validar tamaño máximo (aprox 1.5MB para ESP32)
      const maxSize = 1536 * 1024;
      if (file.size > maxSize) {
        status.textContent = '✗ Error: Archivo muy grande. Máximo ' + (maxSize / 1024 / 1024).toFixed(1) + 'MB';
        status.className = 'status-message error';
        return;
      }

      uploadBtn.disabled = true;
      resetBtn.disabled = true;
      progressDiv.classList.add('active');
      status.textContent = '⏳ Subiendo firmware...';
      status.className = 'status-message info';
      progressFill.style.width = '5%';
      progressPercent.textContent = '5%';

      const formData = new FormData();
      formData.append('file', file);

      const xhr = new XMLHttpRequest();
      
      xhr.upload.addEventListener('progress', (e) => {
        if (e.lengthComputable) {
          const percentComplete = Math.round((e.loaded / e.total) * 90) + 5; // 5-95%
          progressFill.style.width = percentComplete + '%';
          progressPercent.textContent = percentComplete + '%';
          console.log('Upload progress:', percentComplete + '%');
        }
      });

      xhr.addEventListener('load', () => {
        console.log('XHR load - Status:', xhr.status, 'Response:', xhr.responseText.substring(0, 100));
        if (xhr.status === 200) {
          status.textContent = '✓ ¡Actualización completada! El dispositivo se reiniciará en 5 segundos...';
          status.className = 'status-message success';
          progressFill.style.width = '100%';
          progressPercent.textContent = '100%';
          
          console.log('Actualización exitosa');
          setTimeout(() => {
            window.location.reload();
          }, 5000);
        } else {
          status.textContent = '✗ Error HTTP ' + xhr.status + ': ' + xhr.statusText;
          status.className = 'status-message error';
          uploadBtn.disabled = false;
          resetBtn.disabled = false;
        }
      });

      xhr.addEventListener('error', () => {
        console.error('XHR Error:', xhr.statusText, 'Status:', xhr.status, 'Response:', xhr.responseText);
        status.textContent = '✗ Error de conexión. Intenta de nuevo.';
        status.className = 'status-message error';
        uploadBtn.disabled = false;
        resetBtn.disabled = false;
      });

      xhr.addEventListener('abort', () => {
        console.log('XHR cancelado por usuario');
        status.textContent = '✗ Carga cancelada';
        status.className = 'status-message error';
        uploadBtn.disabled = false;
        resetBtn.disabled = false;
      });

      xhr.addEventListener('timeout', () => {
        console.error('XHR Timeout');
        status.textContent = '✗ Timeout. El dispositivo puede estar procesando la actualización.';
        status.className = 'status-message error';
        uploadBtn.disabled = false;
        resetBtn.disabled = false;
      });

      console.log('Enviando multipart/form-data a /update - Tamaño:', file.size, 'bytes');
      xhr.open('POST', '/update');  // POST a /update (manejado por el servidor OTA personalizado)
      xhr.timeout = 120000; // 120 segundos de timeout
      xhr.send(formData);
    });
  </script>
</body>
</html>
)";

// Tarea FreeRTOS para manejar OTA en paralelo (implementación propia sin ElegantOTA)
void ota_background_task(void *parameter)
{
  Serial.println("\n=== [OTA] Iniciando tarea OTA ===");
  Serial.print("[OTA] WiFi conectado: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "SÍ" : "NO");
  Serial.print("[OTA] IP: ");
  Serial.println(WiFi.localIP());

  Serial.println("[OTA] Configurando servidor (handlers OTA personalizados)...");

  // Initialize LittleFS to allow storing uploaded logo
  if (!LittleFS.begin()) {
    Serial.println("[OTA] ERROR: LittleFS.begin() failed");
  } else {
    Serial.println("[OTA] LittleFS mounted");
  }

  // GET / -> página principal
  server.on("/", HTTP_GET, []() {
    Serial.println("[OTA] GET / - Sirviendo página OTA");
    String page = String(ota_html);
    // Use the /logo.png path; the handler will serve LittleFS file if present or fallback to Base64
    page.replace("__LOGO__", String("/logo.png"));
    server.send(200, "text/html", page);
  });

  // GET /logo.png -> serve from LittleFS if available, else decode Base64 and stream
  server.on("/logo.png", HTTP_GET, []() {
    if (LittleFS.exists("/logo.png")) {
      File f = LittleFS.open("/logo.png", "r");
      if (!f) { server.send(500, "text/plain", "file open fail"); return; }
      server.sendHeader("Connection", "close");
      server.streamFile(f, "image/png");
      f.close();
      Serial.println("[OTA] /logo.png: served from LittleFS");
      return;
    }
    // Fallback: decode embedded Base64 and stream
    const char* b64 = get_image_base64("logo");
    if (!b64 || b64[0] == '\0') {
      Serial.println("[OTA] /logo.png: no logo data");
      server.send(404, "text/plain", "no logo");
      return;
    }
    size_t b64len = strlen(b64);
    Serial.printf("[OTA] /logo.png: b64len=%u\n", (unsigned)b64len);
    size_t maxBin = (b64len / 4) * 3 + 16;
    uint8_t* buf = (uint8_t*)malloc(maxBin);
    if (!buf) { server.send(500, "text/plain", "OOM"); return; }
    size_t decLen = base64_decode(b64, buf);
    Serial.printf("[OTA] /logo.png: decoded=%u bytes\n", (unsigned)decLen);
    WiFiClient client = server.client();
    String hdr = "HTTP/1.1 200 OK\r\n";
    hdr += "Content-Type: image/png\r\n";
    hdr += "Content-Length: "; hdr += String((int)decLen); hdr += "\r\n";
    hdr += "Connection: close\r\n\r\n";
    client.print(hdr);
    size_t wrote = client.write(buf, decLen);
    Serial.printf("[OTA] /logo.png: wrote=%u bytes to client\n", (unsigned)wrote);
    client.flush();
    free(buf);
  });

  // GET /update/identity -> devuelve versión
  server.on("/update/identity", HTTP_GET, []() {
    String json = "{\"version\":\"" + String(FIRMWARE_VERSION) + "\"}";
    server.send(200, "application/json", json);
  });

  // Nuevo: GET /update/device_info -> devuelve JSON completo con métricas + ip/mac
  server.on("/update/device_info", HTTP_GET, []() {
    String json = "{";
    json += "\"version\":\"" + String(FIRMWARE_VERSION) + "\"";

    // temperatura
    if (isnan(ota_temp_c)) {
      json += ",\"temperature\":null";
    } else {
      json += ",\"temperature\":" + String(ota_temp_c, 1);
    }

    // humedad
    if (isnan(ota_humidity_pct)) {
      json += ",\"humidity\":null";
    } else {
      json += ",\"humidity\":" + String(ota_humidity_pct, 1);
    }

    // bateria
    if (ota_battery_pct < 0) {
      json += ",\"battery\":null";
    } else {
      json += ",\"battery\":" + String(ota_battery_pct);
    }

    // estado de la puerta
    if (ota_door_state < 0) {
      json += ",\"door\":null";
    } else {
      json += ",\"door\":" + String(ota_door_state);
    }

    // ip y mac
    json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    json += ",\"mac\":\"" + WiFi.macAddress() + "\"";

    // Add connected SSID and RSSI
    json += ",\"ssid\":\"" + WiFi.SSID() + "\"";
    json += ",\"rssi\":" + String(WiFi.RSSI());

    json += "}";

    server.send(200, "application/json", json);
  });

  // POST /upload_logo -> recibir PNG y guardarlo en LittleFS
  server.on("/upload_logo", HTTP_POST, []() {
    // final handler: respond OK and trigger no restart
    server.send(200, "application/json", "{\"ok\":true}\n");
  }, []() {
    HTTPUpload &upload = server.upload();
    static File logoFile = File();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("[OTA] logo upload start: %s\n", upload.filename.c_str());
      if (LittleFS.exists("/logo.png")) LittleFS.remove("/logo.png");
      logoFile = LittleFS.open("/logo.png", "w");
      if (!logoFile) Serial.println("[OTA] ERROR: cannot open /logo.png for writing");
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (logoFile) logoFile.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (logoFile) {
        logoFile.close();
        Serial.printf("[OTA] logo upload complete, size=%u\n", upload.totalSize);
      }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      Serial.println("[OTA] logo upload aborted");
      if (logoFile) { logoFile.close(); LittleFS.remove("/logo.png"); }
    }
  });

  // GET/POST /update/mode -> consulta y cambia modo continuo (persistente)
  server.on("/update/mode", HTTP_GET, []() {
    String js = String("{\"continuous\":") + (ota_continuous_mode ? "true" : "false") + "}";
    server.send(200, "application/json", js);
  });

  server.on("/update/mode", HTTP_POST, []() {
    String body = server.arg("plain");
    Serial.printf("[OTA] POST /update/mode body: %s\n", body.c_str());
    bool want = false;
    bool parsed = false;
    int idx = body.indexOf("continuous");
    if (idx >= 0) {
      int colon = body.indexOf(':', idx);
      if (colon >= 0) {
        String v = body.substring(colon + 1);
        v.trim();
        if (v.startsWith("true") || v.startsWith("1")) { want = true; parsed = true; }
        else if (v.startsWith("false") || v.startsWith("0")) { want = false; parsed = true; }
      }
    }
    if (!parsed) {
      want = (body.indexOf("true") >= 0) || (body.indexOf("1") >= 0);
    }

    Serial.printf("[OTA] parsed wantContinuous=%s (battery_pct=%d)\n", want ? "true" : "false", ota_battery_pct);
    // Accept any requested mode change from UI; persist and notify
    ota_set_continuous_mode(want);
    String resp = String("{\"continuous\":") + (ota_continuous_mode ? "true" : "false") + "}";
    server.send(200, "application/json", resp);
  });

  // POST /update -> manejo de carga OTA usando Update API
  server.on("/update", HTTP_POST, []() {
    // Compleción de la petición
    if (Update.hasError()) {
      Serial.println("[OTA] Resultado: FALLÓ");
      server.send(500, "text/plain", "FAIL");
    } else {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", "OK");
      Serial.println("[OTA] Actualización completada con éxito, reiniciando...");
      delay(100);
      ESP.restart();
    }
  }, []() {
    // Handler de upload
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("[OTA] UploadStart: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != (int)upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("[OTA] Update Success: %u bytes\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      Serial.println("[OTA] Upload Aborted");
    }
  });

  // POST /factory_reset -> borrar credenciales y reiniciar (desde UI OTA)
  server.on("/factory_reset", HTTP_POST, []() {
    Serial.println("[OTA] POST /factory_reset recibido: borrando credenciales...");
    erase_wifi_credentials();
    server.send(200, "text/plain", "OK");
    delay(200);
    ESP.restart();
  });

  // Iniciar servidor
  Serial.println("[OTA] Iniciando servidor WebServer en puerto 80...");
  server.begin();
  Serial.println("[OTA] Servidor WebServer iniciado ✓");

  ota_active = true;

  Serial.println("✓ Servidor OTA listo");
  Serial.println("✓ Dirección: http://" + WiFi.localIP().toString());
  Serial.println("✓ Acceso: http://" + WiFi.localIP().toString() + "/");
  Serial.println("[OTA] Esperando conexiones...\n");

  // Bucle infinito: manejar peticiones OTA
  while (true)
  {
    server.handleClient();
    delay(1);
  }

  vTaskDelete(NULL); // Nunca se alcanza, pero por seguridad
}

// Inicializa OTA en una tarea FreeRTOS
void init_ota_background()
{
  Serial.println("[OTA_INIT] Verificando precondiciones...");
  Serial.print("[OTA_INIT] WiFi conectado: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "SÍ" : "NO");
  Serial.print("[OTA_INIT] Task handle: ");
  Serial.println(ota_task_handle == NULL ? "NULL (ok)" : "YA EXISTE");

  // Cargar el modo continuo desde Preferences (persistente entre reinicios)
  ota_prefs.begin("moe", true); // read-only here
  ota_continuous_mode = ota_prefs.getBool("continuous", false);
  ota_prefs.end();
  Serial.printf("[OTA_INIT] continuous_mode=%s\n", ota_continuous_mode ? "true" : "false");

  if (ota_task_handle == NULL && WiFi.status() == WL_CONNECTED)
  {
    Serial.println("[OTA_INIT] Creando tarea FreeRTOS...");
    
    // Crear tarea con 16KB de stack (suficiente para la carga OTA)
    BaseType_t result = xTaskCreatePinnedToCore(
      ota_background_task,     // Función de la tarea
      "OTA_Task",              // Nombre
      8192,                    // Stack size
      NULL,                     // Parámetros
      1,                        // Prioridad (0-24, 1 es bajo)
      &ota_task_handle,         // Handle
      0                         // Core (0 o 1)
    );
    
    Serial.print("[OTA_INIT] Resultado de xTaskCreatePinnedToCore: ");
    Serial.println(result == pdPASS ? "✓ ÉXITO" : "✗ FALLO");
    
    if (result != pdPASS) {
      Serial.println("[OTA_INIT] ERROR: No se pudo crear la tarea OTA");
      ota_task_handle = NULL;
    }
  }
  else
  {
    if (ota_task_handle != NULL) {
      Serial.println("[OTA_INIT] ERROR: Tarea OTA ya existe");
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[OTA_INIT] ERROR: WiFi no disponible");
    }
  }
}

// Detiene OTA
void stop_ota_background()
{
  if (ota_task_handle != NULL)
  {
    server.stop();
    vTaskDelete(ota_task_handle);
    ota_task_handle = NULL;
    ota_active = false;
  }
}

// Verifica si OTA está activo
bool is_ota_active()
{
  return ota_active;
}

// Persistente: set/get para modo continuo (utilizado por sleep_utils)
void ota_set_continuous_mode(bool enabled)
{
  ota_continuous_mode = enabled;
  ota_prefs.begin("moe", false); // RW
  ota_prefs.putBool("continuous", ota_continuous_mode);
  ota_prefs.end();
  Serial.printf("[OTA] ota_set_continuous_mode=%s\n", ota_continuous_mode ? "true" : "false");
  // Notify application of mode change immediately
  ota_on_mode_changed(ota_continuous_mode);
}

// Weak callback the application may implement to react immediately to mode changes
void ota_on_mode_changed(bool continuous) __attribute__((weak));
void ota_on_mode_changed(bool continuous) { /* default: no-op */ }


bool ota_is_continuous_mode()
{
  return ota_continuous_mode;
}

// Toggle and persist continuous mode; returns new effective state
bool ota_toggle_continuous_mode()
{
  ota_set_continuous_mode(!ota_continuous_mode);
  Serial.printf("[OTA] ota_toggle_continuous_mode -> %s\n", ota_continuous_mode ? "true" : "false");
  return ota_continuous_mode;
}
