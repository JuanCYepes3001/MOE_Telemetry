#include "wifi_utils.h"
#include "config.h"
#include "display_utils.h"
#include "ota_utils.h"
#include "esp_wifi.h"
#include <Preferences.h>
#include <WebServer.h>
#include "images.h"
#include <DNSServer.h>
#include <DNSServer.h>

// Namespace de NVS para credenciales
static const char* PREF_NS = "moe_wifi";

// Portal de configuración: nombre y password del AP
static const char* CONFIG_AP_SSID_PREFIX = "MOE_Telemetry_";
static const char* CONFIG_AP_PASS = ""; // abierto por defecto

//  Función que permite configurar y realizar la conexión a la red Wi-Fi
void set_wifi_connection() 
{
  // Use the non-blocking try_connect_wifi_no_ap which will only start AP if explicitly needed
  if (try_connect_wifi_no_ap()) return;

  // Intentar cargar credenciales guardadas en Preferences
  String stored_ssid, stored_pass;
  if (!load_wifi_credentials(stored_ssid, stored_pass)) {
    // No hay credenciales guardadas: iniciar portal de configuración
    Serial.println("[WIFI] No se encontraron credenciales: iniciando AP de configuración...");
    start_config_ap();
    // start_config_ap() reinicia el dispositivo o guarda credenciales
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(stored_ssid.c_str(), stored_pass.c_str());

  unsigned long startAttemptTime = millis();
  const unsigned long timeout = WIFI_TIMEOUT_MS;

  display_oled_message_3_line(
    "Conectando a",
    "la red Wi-Fi",
    stored_ssid
  );

  while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime) < timeout)
  {
    delay(100);
  }

  // Configurar WiFi en modo de bajo consumo
  esp_wifi_set_ps(WIFI_PS_MAX_MODEM); // Máximo ahorro energético en WiFi
  // esp_wifi_set_ps(WIFI_PS_MIN_MODEM); // Mínimo ahorro energético en WiFi

  if (WiFi.status() == WL_CONNECTED) 
  {
    display_oled_message_3_line(
      "Conexión", 
      "Wi-Fi", 
      "establecida"
    );
    delay(500);
  } 
  else 
  {
    display_oled_message_3_line(
      "Error en", 
      "la conexión", 
      "Wi-Fi"
    );
    delay(500);
  }
}

// Cargar credenciales desde Preferences. Retorna true si existen y no están vacías.
// Cargar credenciales desde Preferences. Retorna true si existen y no están vacías.
// Además soporta la bandera "force_ap" que puede establecer OTA para forzar
// entrar en modo AP en el siguiente reinicio (por ejemplo después de un flash).
bool load_wifi_credentials(String &out_ssid, String &out_password)
{
  Preferences prefs;
  prefs.begin(PREF_NS, true); // read-only para leer ssid/pass y flag
  String s = prefs.getString("ssid", "");
  String p = prefs.getString("pass", "");
  bool force_ap = prefs.getBool("force_ap", false);
  prefs.end();

  if (force_ap) {
    // Clear flag and force AP by returning false
    Preferences prefs2;
    prefs2.begin(PREF_NS, false);
    prefs2.remove("force_ap");
    prefs2.end();
    Serial.println("[WIFI] force_ap flag detected -> starting AP and clearing flag");
    return false;
  }

  if (s.length() == 0) return false;
  out_ssid = s;
  out_password = p;
  return true;
}

// Guardar credenciales en NVS
void save_wifi_credentials(const char* ssid, const char* password)
{
  Preferences prefs;
  prefs.begin(PREF_NS, false);
  prefs.putString("ssid", String(ssid));
  prefs.putString("pass", String(password));
  // Ensure we don't keep force_ap after saving credentials
  prefs.remove("force_ap");
  prefs.end();
}

// Borrar credenciales (factory reset WiFi)
void erase_wifi_credentials()
{
  Preferences prefs;
  prefs.begin(PREF_NS, false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
}

// Portal de configuración simple usando WebServer. Bloqueante: espera POST /save o /factory_reset.
void start_config_ap()
{
  Serial.println("[WIFI] Config AP: escaneando redes cercanas...");

  // Cambiar a modo STA temporal para escanear
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(200);

  int n = WiFi.scanNetworks();
  String networksJson = "[";
  if (n > 0) {
    for (int i = 0; i < n; ++i) {
      String ss = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
      // Escape double quotes in SSID
      ss.replace("\"", "\\\"");
      networksJson += "{\"ssid\":\"" + ss + "\",\"rssi\":" + String(rssi) + ",\"open\":" + String(open ? 1 : 0) + "}";
      if (i < n - 1) networksJson += ",";
    }
  }
  networksJson += "]";

  Serial.println("[WIFI] Scan completo. Redes encontradas: " + String(n));

  // Iniciar AP para portal de configuración
  // Stop OTA server if running to avoid port conflicts so AP portal on :80 is reachable
  if (is_ota_active()) {
    Serial.println("[WIFI] Deteniendo servidor OTA antes de iniciar AP para evitar conflicto de puertos...");
    stop_ota_background();
    delay(200);
  }
  WiFi.mode(WIFI_AP);
  // Build AP SSID: prefix + last 4 hex digits of MAC
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  String last4 = mac;
  if (last4.length() >= 4) last4 = last4.substring(last4.length() - 4);
  String apSSID = String(CONFIG_AP_SSID_PREFIX) + last4;
  bool apStarted = WiFi.softAP(apSSID.c_str(), CONFIG_AP_PASS);
  if (!apStarted) {
    Serial.println("[WIFI] ERROR: no se pudo iniciar AP");
    return;
  }

  IPAddress apIP = WiFi.softAPIP();
  Serial.print("[WIFI] AP iniciado. SSID: "); Serial.println(apSSID);
  Serial.print("[WIFI] IP AP: "); Serial.println(apIP.toString());
  // Mostrar únicamente SSID, IP y MAC en pantalla durante modo AP
  String macAddr = WiFi.macAddress();
  // Use specialized AP display for better readability
  display_oled_ap_info(apSSID, apIP.toString(), macAddr);

  // Iniciar servidor DNS para captive portal: redirigir todas las consultas al AP IP
  static DNSServer dnsServer;
  const byte DNS_PORT = 53;
  // Responder a cualquier dominio con la IP del AP
  dnsServer.start(DNS_PORT, "*", apIP);

  WebServer apServer(80);

  // Serve logo as binary PNG to avoid embedding large base64 into HTML
  apServer.on("/logo.png", HTTP_GET, [&]() {
    const char* b64 = get_image_base64("logo");
    if (!b64 || b64[0] == '\0') {
      Serial.println("[WIFI] /logo.png: no logo data");
      apServer.send(404, "text/plain", "no logo"); return; }
    size_t b64len = strlen(b64);
    Serial.printf("[WIFI] /logo.png: b64len=%u\n", (unsigned)b64len);
    size_t maxBin = (b64len / 4) * 3 + 16;
    uint8_t* buf = (uint8_t*)malloc(maxBin);
    if (!buf) { apServer.send(500, "text/plain", "OOM"); return; }
    // simple base64 decode
    const char* b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char dtable[256]; memset(dtable, 0x80, 256);
    for (int i = 0; i < 64; ++i) dtable[(unsigned char)b64chars[i]] = i;
    dtable['='] = 0;
    size_t out_len = 0; unsigned int val = 0; int valb = -8;
    for (size_t i = 0; i < b64len; ++i) {
      unsigned char c = b64[i]; if (dtable[c] & 0x80) continue;
      val = (val << 6) + dtable[c]; valb += 6;
      if (valb >= 0) { buf[out_len++] = (unsigned char)((val >> valb) & 0xFF); valb -= 8; }
    }
    Serial.printf("[WIFI] /logo.png: decoded=%u bytes\n", (unsigned)out_len);
    if (out_len >= 8) {
      Serial.print("[WIFI] /logo.png: header=");
      for (int i = 0; i < 8; ++i) {
        if (buf[i] < 16) Serial.print('0');
        Serial.print(buf[i], HEX);
        Serial.print(' ');
      }
      Serial.println();
    }
    WiFiClient client = apServer.client();
    String hdr = "HTTP/1.1 200 OK\r\n";
    hdr += "Content-Type: image/png\r\n";
    hdr += "Content-Length: "; hdr += String((int)out_len); hdr += "\r\n";
    hdr += "Connection: close\r\n\r\n";
    client.print(hdr);
    size_t wrote = client.write(buf, out_len);
    Serial.printf("[WIFI] /logo.png: wrote=%u bytes to client\n", (unsigned)wrote);
    client.flush();
    free(buf);
  });

  // Endpoint que devuelve redes escaneadas como JSON
  apServer.on("/scan", HTTP_GET, [&]() {
    apServer.send(200, "application/json", networksJson);
  });

  // Página principal (UI estilo OTA)
  apServer.on("/", HTTP_GET, [&]() {
    String page = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>MOE Telemetry - Configuración WiFi</title>
  <style>
    :root{--bg:rgb(0,0,0);--card-bg:rgb(20,20,20);--accent:rgb(0,140,226);--accent-dark:#007bb5;--info-bg:rgba(0,140,226,0.04);--muted:#6c757d;--text:#ffffff;--update-green:#28a745;--reset-red:#dc3545}
    body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial;background:var(--bg);color:var(--text);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
    .container{background:var(--card-bg);border-radius:12px;max-width:640px;width:100%;box-shadow:0 20px 60px rgba(0,0,0,.7);overflow:hidden;border:1px solid rgba(255,255,255,0.03);position:relative}
    .header{background:linear-gradient(135deg,#222222 0%,#4a4a4a 100%);color:var(--text);padding:30px;text-align:center}
    .header h1{font-size:24px;margin:0;color:var(--accent)}
    #logo{height:64px;display:block;margin:6px auto;background:rgba(255,255,255,0.02);padding:6px;border-radius:8px}
    .content{padding:28px}
    label{display:block;margin-top:12px;color:rgba(255,255,255,0.8);font-weight:600}
    select,input{width:100%;padding:12px;margin-top:6px;border-radius:8px;border:1px solid rgba(255,255,255,0.06);background:transparent;color:var(--text)}
    select{background:rgb(30,30,30)}
    .row{display:flex;gap:12px}
    .row .col{flex:1}
    .buttons{display:flex;gap:10px;margin-top:18px}
    button{flex:1;padding:12px;border-radius:10px;border:none;font-weight:700;cursor:pointer}
    .primary{background:linear-gradient(90deg,var(--accent) 0%,var(--accent-dark) 100%);color:#fff}
    .danger{background:linear-gradient(90deg,var(--muted) 0%,#5a636b 100%);color:#fff}
    .status{margin-top:14px;padding:12px;border-radius:8px;background:rgba(0,0,0,0.06);color:var(--text);border-left:4px solid var(--accent)}
    .signature{position:absolute;left:0;right:0;bottom:6px;text-align:center;font-size:10px;color:#fff;opacity:0.04;pointer-events:none;user-select:none}
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>Configuración WiFi</h1>
      <div class="version"><img id="logo" src="__LOGO__" alt="MOE" style="display:block;margin:0 auto;"></div>
      <div class="signature">juan camilo yepes</div>
    </div>
    <div class="content">
      <label for="ssidSelect">Seleccionar red disponible</label>
      <select id="ssidSelect"><option value="">-- redes cercanas --</option></select>

      <label for="ssidInput">SSID (puedes escribir o seleccionar)</label>
      <input id="ssidInput" placeholder="Nombre de la red">

      <label for="passInput">Contraseña</label>
      <input id="passInput" type="password" placeholder="Contraseña WiFi (si aplica)">

      <div class="buttons">
        <button id="saveBtn" class="primary">Guardar y Conectar</button>
        <button id="resetBtn" class="danger">Restablecer de fábrica</button>
      </div>

      <div id="status" class="status" style="display:none">Estado...</div>
    </div>
  </div>

  <script>
    const ssidSelect = document.getElementById('ssidSelect');
    const ssidInput = document.getElementById('ssidInput');
    const passInput = document.getElementById('passInput');
    const saveBtn = document.getElementById('saveBtn');
    const resetBtn = document.getElementById('resetBtn');
    const statusDiv = document.getElementById('status');

    function showStatus(msg, ok=true){ statusDiv.style.display='block'; statusDiv.textContent = msg; statusDiv.style.background = ok ? 'rgba(0,140,226,0.12)' : 'rgba(220,40,40,0.12)'; statusDiv.style.color = '#ffffff'; }

    // Cargar redes desde /scan
    fetch('/scan').then(r=>r.json()).then(list=>{
      list.sort((a,b)=>b.rssi-a.rssi);
      list.forEach(n=>{
        const opt = document.createElement('option');
        opt.value = n.ssid;
        opt.textContent = `${n.ssid} (${n.rssi}dBm)`;
        ssidSelect.appendChild(opt);
      });
    }).catch(()=>{ /* ignore */ });

    ssidSelect.addEventListener('change', ()=>{ if (ssidSelect.value) ssidInput.value = ssidSelect.value; });

    saveBtn.addEventListener('click', ()=>{
      const ss = ssidInput.value.trim();
      const pw = passInput.value;
      if (!ss){ showStatus('SSID vacío', false); return; }
      showStatus('Guardando credenciales...');
      const form = new FormData(); form.append('ssid', ss); form.append('pass', pw);
      fetch('/save', { method:'POST', body: form }).then(r=>r.text()).then(t=>{
        showStatus('Guardado. Reiniciando...', true);
        setTimeout(()=>location.reload(),3000);
      }).catch(e=>{ showStatus('Error al guardar', false); });
    });

    resetBtn.addEventListener('click', ()=>{
      if (!confirm('¿Borrar credenciales y reiniciar el dispositivo?')) return;
      fetch('/factory_reset', { method:'POST' }).then(r=>r.text()).then(t=>{ showStatus('Restableciendo...'); setTimeout(()=>location.reload(),3000); }).catch(()=>{ showStatus('Error al resetear', false); });
    });
  </script>
</body>
</html>
)rawliteral";

    // Inyectar la IP del AP en la página para que también se muestre en el navegador
    page.replace("MOE Telemetry", "MOE Telemetry - " + apIP.toString());
    // Reemplazar placeholder de logo y añadir IP al título
    String pageStr = page;
    // Embed the logo as a data: URI so it renders even when the client is connected to the AP
    pageStr.replace("__LOGO__", String("data:image/png;base64,") + String(get_image_base64("logo")));
    pageStr.replace("MOE Telemetry", String("MOE Telemetry - ") + apIP.toString());
    apServer.send(200, "text/html", pageStr);
  });

  // Captive portal behavior: servir una página simple para cualquier request
  // Esto ayuda a clientes que no siguen 302/Location o usan HTTPS para comprobaciones.
  apServer.onNotFound([&]() {
    String fallback = "<html><head><meta http-equiv=\"refresh\" content=\"0;url=http://" + apIP.toString() + "\"></head><body>Red de configuracion MOE - <a href=\"http://" + apIP.toString() + "\">Abrir configuracion</a></body></html>";
    apServer.send(200, "text/html", fallback);
  });

  // Save handler
  apServer.on("/save", HTTP_POST, [&]() {
    String ss = apServer.arg("ssid");
    String pw = apServer.arg("pass");
    if (ss.length() == 0) {
      apServer.send(400, "text/plain", "SSID vacío");
      return;
    }
    save_wifi_credentials(ss.c_str(), pw.c_str());
    apServer.send(200, "text/plain", "Guardado");
    delay(200);
    ESP.restart();
  });

  // Factory reset handler
  apServer.on("/factory_reset", HTTP_POST, [&]() {
    erase_wifi_credentials();
    apServer.send(200, "text/plain", "Borrado");
    delay(200);
    ESP.restart();
  });

  apServer.begin();
  Serial.println("[WIFI] Portal de configuración activo en "+apIP.toString());

  while (true) {
    dnsServer.processNextRequest();
    apServer.handleClient();
    delay(10);
  }
}

//  Función que permite establecer la hora y fecha actual en que ocurre el evento
time_t get_time_NTP() 
{
  //  display_oled_message_3_line("Sincronizando", "hora con", "servidor NTP");
  configTime(-5 * 3600, 0, ntpServer);  // Zona horaria -5 (Colombia) sin DST

  struct tm timeinfo;
  unsigned long startAttemptTime = millis();
  const unsigned long timeout = 5000;  // ⏳ 5 segundos de espera máx.

  // Intentar hasta que se cumpla el timeout
  while (!getLocalTime(&timeinfo)) 
  {
    if (millis() - startAttemptTime > timeout) 
    {
      display_oled_message_3_line(
        "Error al", 
        "sincronizar con", 
        "NTP"
      );
      return 0;
    }
    delay(100); // evitar bloquear demasiado la CPU
  }

  // Si se obtuvo hora válida
  // char dateBuffer[20];
  // char timeBuffer[20];

  // strftime(dateBuffer, sizeof(dateBuffer), "%d/%m/%Y", &timeinfo);
  // strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &timeinfo);

  // display_oled_message_3_line("Fecha y hora", dateBuffer, timeBuffer);

  time_t raw_time;
  time(&raw_time);

  delay(20);
  return raw_time;
}

//  Función para desconectar WiFi y ahorrar energía
void disconnect_wifi()
{
  WiFi.disconnect(true);                                                //  Se asegura que se desconecte completamente
  WiFi.mode(WIFI_OFF);                                                  //  Se garantiza que no se quede en modo STA o AP
  esp_wifi_stop();                                                      //  Se apaga el driver
  esp_wifi_deinit();                                                    //  Se deshabilita completamente el módulo WiFi
}

// Try to connect using stored credentials but DO NOT launch AP if none are present.
// Returns true if connected, false otherwise.
bool try_connect_wifi_no_ap()
{
  String stored_ssid, stored_pass;
  if (!load_wifi_credentials(stored_ssid, stored_pass)) {
    Serial.println("[WIFI] try_connect_wifi_no_ap: no credentials stored");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(stored_ssid.c_str(), stored_pass.c_str());

  unsigned long startAttemptTime = millis();
  const unsigned long timeout = WIFI_TIMEOUT_MS;

  display_oled_message_3_line(
    "Conectando a",
    "la red Wi-Fi",
    stored_ssid
  );

  while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime) < timeout)
  {
    delay(100);
  }

  // Configurar WiFi en modo de bajo consumo
  esp_wifi_set_ps(WIFI_PS_MAX_MODEM);

  if (WiFi.status() == WL_CONNECTED)
  {
    display_oled_message_3_line(
      "Conexión",
      "Wi-Fi",
      "establecida"
    );
    delay(200);
    return true;
  }
  else
  {
    display_oled_message_3_line(
      "Error en",
      "la conexión",
      "Wi-Fi"
    );
    delay(200);
    return false;
  }
}