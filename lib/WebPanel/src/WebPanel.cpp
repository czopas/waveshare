#include "WebPanel.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "Interlock.h"

static WebServer server(80);

static bool CheckAuth() {
  if (!server.authenticate(WEB_LOGIN_USER, WEB_LOGIN_PASSWORD)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

/*******************************************************  Strona glowna  *******************************************************/

static const char kIndexHtml[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Blokada krzyzowa - panel</title>
<style>
  body { font-family: Arial, sans-serif; background:#f0f0f0; margin:0; padding:0; }
  .header { text-align:center; padding:16px 0; background:#333; color:#fff; margin-bottom:16px; }
  .container { max-width:520px; margin:0 auto 20px; padding:16px 20px; background:#fff; border-radius:6px; box-shadow:0 0 5px rgba(0,0,0,.3); }
  h2 { margin-top:0; font-size:16px; color:#333; border-bottom:1px solid #eee; padding-bottom:6px; }
  table { width:100%; border-collapse:collapse; font-size:14px; }
  td { padding:4px 2px; }
  td.label { color:#555; }
  td.val { text-align:right; font-weight:bold; }
  .on { color:#1a7d1a; }
  .off { color:#999; }
  .phase { font-size:18px; text-align:center; padding:8px; border-radius:4px; background:#eee; margin-bottom:10px; font-weight:bold; }
  .form-row { display:flex; align-items:center; margin-bottom:10px; }
  .form-row label { flex:1; font-size:14px; color:#333; }
  .form-row input { width:100px; padding:5px; border:1px solid #ccc; border-radius:3px; text-align:right; }
  button, input[type=submit] { padding:8px 16px; background:#333; color:#fff; border:none; border-radius:4px; cursor:pointer; font-size:14px; }
  button:hover, input[type=submit]:hover { background:#555; }
  .hint { font-size:12px; color:#888; margin-top:-6px; margin-bottom:10px; }
</style>
</head>
<body>
<div class="header"><h1>Blokada krzyzowa - podglad</h1></div>

<div class="container">
  <h2>Stan biezacy</h2>
  <div class="phase" id="phase">-</div>
  <table>
    <tr><td class="label">Aktywne okno</td><td class="val" id="active">-</td></tr>
    <tr><td class="label">Czas w biezacej fazie</td><td class="val" id="elapsed">-</td></tr>
    <tr><td class="label">DI1 - przycisk okno 1</td><td class="val" id="btn1">-</td></tr>
    <tr><td class="label">DI2 - okno 1 zamkniete</td><td class="val" id="closed1">-</td></tr>
    <tr><td class="label">DI3 - przycisk okno 2</td><td class="val" id="btn2">-</td></tr>
    <tr><td class="label">DI4 - okno 2 zamkniete</td><td class="val" id="closed2">-</td></tr>
    <tr><td class="label">CH1 - zielona okno 1</td><td class="val" id="ch1">-</td></tr>
    <tr><td class="label">CH2 - czerwona okno 1</td><td class="val" id="ch2">-</td></tr>
    <tr><td class="label">CH3 - zaczep okno 1</td><td class="val" id="ch3">-</td></tr>
    <tr><td class="label">CH4 - zielona okno 2</td><td class="val" id="ch4">-</td></tr>
    <tr><td class="label">CH5 - czerwona okno 2</td><td class="val" id="ch5">-</td></tr>
    <tr><td class="label">CH6 - zaczep okno 2</td><td class="val" id="ch6">-</td></tr>
  </table>
</div>

<div class="container">
  <h2>Parametry czasowe</h2>
  <form id="cfgForm" action="/api/config" method="POST">
    <div class="form-row"><label>Czas zwolnienia zaczepu [ms]</label><input type="number" min="200" max="60000" step="100" name="lockReleaseMs" id="lockReleaseMs"></div>
    <div class="form-row"><label>Czas regeneracji [ms]</label><input type="number" min="0" max="300000" step="100" name="regenMs" id="regenMs"></div>
    <div class="form-row"><label>Czas debounce wejsc [ms]</label><input type="number" min="5" max="1000" step="5" name="debounceMs" id="debounceMs"></div>
    <div class="hint">Zmiana zapisywana jest w pamieci trwalej (przetrwa restart).</div>
    <input type="submit" value="Zapisz parametry">
  </form>
</div>

<div class="container">
  <h2>Aktualizacja firmware (OTA)</h2>
  <p class="hint">Wgraj plik .bin wygenerowany przez PlatformIO (firmware.bin). Sterownik zrestartuje sie po zakonczeniu.</p>
  <a href="/update"><button type="button">Przejdz do wgrywania firmware</button></a>
</div>

<script>
const phaseNames = {
  READY: 'GOTOWY (obie zielone)',
  UNLOCKED: 'ZADANIE (zaczep zwolniony)',
  OPEN: 'OTWARTE',
  REGEN: 'REGENERACJA',
  ALARM: 'ALARM - NIEAUTORYZOWANE OTWARCIE OKNA'
};
function fmtBool(v, onTxt, offTxt) {
  const span = document.createElement('span');
  span.textContent = v ? onTxt : offTxt;
  span.className = v ? 'on' : 'off';
  return span.outerHTML;
}
async function refresh() {
  try {
    const r = await fetch('/api/status');
    const d = await r.json();
    document.getElementById('phase').textContent = phaseNames[d.phase] || d.phase;
    document.getElementById('active').textContent = d.active;
    document.getElementById('elapsed').textContent = (d.phaseElapsedMs/1000).toFixed(1) + ' s';
    document.getElementById('btn1').innerHTML = fmtBool(d.btn1, 'wcisniety', '-');
    document.getElementById('closed1').innerHTML = fmtBool(d.closed1, 'zamkniete', 'OTWARTE');
    document.getElementById('btn2').innerHTML = fmtBool(d.btn2, 'wcisniety', '-');
    document.getElementById('closed2').innerHTML = fmtBool(d.closed2, 'zamkniete', 'OTWARTE');
    for (let i = 0; i < 6; i++) {
      document.getElementById('ch' + (i+1)).innerHTML = fmtBool(d.out[i], 'ZALACZONE', 'wylaczone');
    }
  } catch (e) { /* chwilowy brak polaczenia - probujemy ponownie przy kolejnym tiku */ }
}
async function loadConfig() {
  try {
    const r = await fetch('/api/status');
    const d = await r.json();
    document.getElementById('lockReleaseMs').value = d.lockReleaseMs;
    document.getElementById('regenMs').value = d.regenMs;
    document.getElementById('debounceMs').value = d.debounceMs;
  } catch (e) {}
}
loadConfig();
refresh();
setInterval(refresh, 500);
</script>
</body>
</html>
)HTML";

static void HandleRoot() {
  if (!CheckAuth()) return;
  server.send_P(200, "text/html", kIndexHtml);
}

static void HandleApiStatus() {
  if (!CheckAuth()) return;
  InterlockStatus st = Interlock_GetStatus();

  JsonDocument doc;
  doc["phase"] = Interlock_PhaseName(st.phase);
  doc["active"] = (st.activeWindow == 1) ? "okno 1" : (st.activeWindow == 2) ? "okno 2" : "-";
  doc["btn1"] = st.btn1Active;
  doc["btn2"] = st.btn2Active;
  doc["closed1"] = st.win1Closed;
  doc["closed2"] = st.win2Closed;
  JsonArray out = doc["out"].to<JsonArray>();
  for (int i = 0; i < 6; i++) out.add(st.out[i]);
  doc["lockReleaseMs"] = st.lockReleaseMs;
  doc["regenMs"] = st.regenMs;
  doc["debounceMs"] = st.debounceMs;
  doc["phaseElapsedMs"] = st.phaseElapsedMs;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

static void HandleApiConfig() {
  if (!CheckAuth()) return;
  if (!server.hasArg("lockReleaseMs") || !server.hasArg("regenMs") || !server.hasArg("debounceMs")) {
    server.send(400, "text/plain", "Brak wymaganych parametrow");
    return;
  }
  long lockReleaseMs = server.arg("lockReleaseMs").toInt();
  long regenMs = server.arg("regenMs").toInt();
  long debounceMs = server.arg("debounceMs").toInt();

  if (lockReleaseMs < 0 || regenMs < 0 || debounceMs < 0 ||
      !Interlock_SetTimings((unsigned long)lockReleaseMs, (unsigned long)regenMs, (unsigned long)debounceMs)) {
    server.send(400, "text/plain", "Nieprawidlowe wartosci parametrow (poza dozwolonym zakresem)");
    return;
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

/*******************************************************  OTA  *******************************************************/

static const char kUpdateHtml[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Aktualizacja firmware</title>
<style>
  body { font-family: Arial, sans-serif; background:#f0f0f0; margin:0; padding:0; }
  .header { text-align:center; padding:16px 0; background:#333; color:#fff; margin-bottom:16px; }
  .container { max-width:420px; margin:0 auto; padding:16px 20px; background:#fff; border-radius:6px; box-shadow:0 0 5px rgba(0,0,0,.3); }
  input[type=submit] { padding:8px 16px; background:#333; color:#fff; border:none; border-radius:4px; cursor:pointer; }
  .hint { font-size:12px; color:#888; }
</style>
</head>
<body>
<div class="header"><h1>Aktualizacja firmware</h1></div>
<div class="container">
  <p class="hint">Wybierz plik firmware.bin (PlatformIO: .pio/build/waveshare_32108/firmware.bin) i wgraj. Sterownik zrestartuje sie automatycznie po udanej aktualizacji - blokada bedzie przez chwile niedostepna.</p>
  <form method="POST" action="/update" enctype="multipart/form-data">
    <input type="file" name="update" accept=".bin">
    <br><br>
    <input type="submit" value="Wgraj firmware">
  </form>
  <p><a href="/">&larr; powrot do panelu</a></p>
</div>
</body>
</html>
)HTML";

static void HandleUpdatePage() {
  if (!CheckAuth()) return;
  server.send_P(200, "text/html", kUpdateHtml);
}

static void HandleUpdateResult() {
  server.sendHeader("Connection", "close");
  if (Update.hasError()) {
    server.send(200, "text/plain", "Blad aktualizacji firmware - sprawdz logi na porcie szeregowym.");
  } else {
    server.send(200, "text/plain", "Aktualizacja zakonczona powodzeniem. Sterownik restartuje sie...");
    delay(1000);
    ESP.restart();
  }
}

static void HandleUpdateUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    if (!CheckAuth()) {
      printf("[WebPanel] OTA odrzucone - brak autoryzacji\r\n");
      return;
    }
    printf("[WebPanel] OTA start: %s\r\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      printf("[WebPanel] OTA OK, rozmiar: %u bajtow\r\n", (unsigned)upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end();
    printf("[WebPanel] OTA przerwane\r\n");
  }
}

/*******************************************************  Zadanie FreeRTOS  *******************************************************/

static void WebPanelTask(void *parameter) {
  (void)parameter;
  for (;;) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void WebPanel_Init(void) {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress ip = WiFi.softAPIP();
  printf("[WebPanel] Punkt dostepu '%s' uruchomiony, panel dostepny pod http://%s/\r\n",
         AP_SSID, ip.toString().c_str());

  server.on("/", HTTP_GET, HandleRoot);
  server.on("/api/status", HTTP_GET, HandleApiStatus);
  server.on("/api/config", HTTP_POST, HandleApiConfig);
  server.on("/update", HTTP_GET, HandleUpdatePage);
  server.on("/update", HTTP_POST, HandleUpdateResult, HandleUpdateUpload);
  server.begin();

  xTaskCreatePinnedToCore(
    WebPanelTask,
    "WebPanelTask",
    8192,
    NULL,
    2,
    NULL,
    1
  );
}
