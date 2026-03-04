#pragma once

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define PA_MAX_DEVICES 20

// ---------------------------------------------------------------------------
// System status (Mode 0)
// ---------------------------------------------------------------------------
struct PaStatus {
  int  all_devices;
  int  online;
  int  offline;
  int  new_devices;
  int  down;
  char last_scan[24];  // "YYYY-MM-DD HH:MM:SS"
  bool valid;
};

static PaStatus pa_status = {0, 0, 0, 0, 0, "", false};

// ---------------------------------------------------------------------------
// Device list (Modes 1 & 2)
// ---------------------------------------------------------------------------
struct PaDevice {
  char name[32];
  char ip[16];
  char vendor[32];
  bool valid;
};

static PaDevice pa_online[PA_MAX_DEVICES];
static int      pa_online_count  = 0;
static PaDevice pa_offline[PA_MAX_DEVICES];
static int      pa_offline_count = 0;

// ---------------------------------------------------------------------------
// New/unknown devices (Mode 3)
// ---------------------------------------------------------------------------
#define PA_MAX_NEW 40

struct PaNewDevice {
  char name[32];
  char ip[16];
  char vendor[32];
  char mac[18];
  char first_seen[20];  // "YYYY-MM-DD HH:MM:SS"
  bool valid;
};

static PaNewDevice pa_new_devices[PA_MAX_NEW];
static int         pa_new_count = 0;

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
static char pa_last_error[48] = "";

// ---------------------------------------------------------------------------
// Generic POST helper — sends api-key + get param to /pialert/api/
// Returns HTTP code, populates out on 200.
// ---------------------------------------------------------------------------
static int paPost(const char *get_param, String &out) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  if (pa_host[0] == '\0' || pa_apikey[0] == '\0') {
    snprintf(pa_last_error, sizeof(pa_last_error), "No host/key set");
    return -1;
  }

  char url[80];
  snprintf(url, sizeof(url), "http://%s/pialert/api/", pa_host);
  char body[160];
  snprintf(body, sizeof(body), "api-key=%s&get=%s", pa_apikey, get_param);

  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt > 0) {
      Serial.printf("[PiAlert] begin() failed, retry %d...\n", attempt);
      delay(attempt * 800);
    }
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(8000);
    if (!http.begin(client, url)) continue;
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int code = http.POST(body);
    if (code == HTTP_CODE_OK) {
      out = http.getString();
      http.end();
      if (out.startsWith("Wrong API-Key")) {
        snprintf(pa_last_error, sizeof(pa_last_error), "Wrong API key");
        return 403;
      }
      return code;
    }
    http.end();
    return code;
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Down devices (Mode 4) — devices marked to alert when down, currently offline
// ---------------------------------------------------------------------------
#define PA_MAX_DOWN 20

struct PaDownDevice {
  char name[32];
  char ip[16];
  char vendor[32];
  bool valid;
};

static PaDownDevice pa_down[PA_MAX_DOWN];
static int          pa_down_count = 0;

static bool paFetchDown() {
  String payload;
  int code = paPost("all-down", payload);
  if (code != HTTP_CODE_OK) {
    if (code != 403) snprintf(pa_last_error, sizeof(pa_last_error),
                              code == -1 ? "No connection" : "HTTP %d", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    snprintf(pa_last_error, sizeof(pa_last_error), "JSON error");
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) {
    snprintf(pa_last_error, sizeof(pa_last_error), "No device array");
    return false;
  }

  pa_down_count = 0;
  for (JsonObject dev : arr) {
    if (pa_down_count >= PA_MAX_DOWN) break;
    PaDownDevice &d = pa_down[pa_down_count++];
    strncpy(d.name,   dev["dev_Name"]   | "Unknown", sizeof(d.name)   - 1);
    strncpy(d.ip,     dev["dev_LastIP"] | "",         sizeof(d.ip)     - 1);
    strncpy(d.vendor, dev["dev_Vendor"] | "",         sizeof(d.vendor) - 1);
    d.name[sizeof(d.name)-1]     = '\0';
    d.ip[sizeof(d.ip)-1]         = '\0';
    d.vendor[sizeof(d.vendor)-1] = '\0';
    d.valid = true;
  }

  Serial.printf("[PiAlert] Down: %d devices\n", pa_down_count);
  return true;
}

// ---------------------------------------------------------------------------
// Recent events (Mode 5) — last 15 connect/disconnect events
// ---------------------------------------------------------------------------
#define PA_MAX_EVENTS 20

struct PaEvent {
  char name[32];
  char ip[16];
  char type[24];    // "Connected", "Disconnected", etc.
  char time[20];    // "YYYY-MM-DD HH:MM:SS"
  bool valid;
};

static PaEvent  pa_events[PA_MAX_EVENTS];
static int      pa_event_count = 0;

static bool paFetchEvents() {
  String payload;
  int code = paPost("recent-events", payload);
  if (code != HTTP_CODE_OK) {
    if (code != 403) snprintf(pa_last_error, sizeof(pa_last_error),
                              code == -1 ? "No connection" : "HTTP %d", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    snprintf(pa_last_error, sizeof(pa_last_error), "JSON error");
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) {
    snprintf(pa_last_error, sizeof(pa_last_error), "No events array");
    return false;
  }

  pa_event_count = 0;
  for (JsonObject ev : arr) {
    if (pa_event_count >= PA_MAX_EVENTS) break;
    PaEvent &e = pa_events[pa_event_count++];
    strncpy(e.name, ev["dev_Name"]      | "Unknown", sizeof(e.name) - 1);
    strncpy(e.ip,   ev["eve_IP"]        | "",         sizeof(e.ip)   - 1);
    strncpy(e.type, ev["eve_EventType"] | "",         sizeof(e.type) - 1);
    strncpy(e.time, ev["eve_DateTime"]  | "",         sizeof(e.time) - 1);
    e.name[sizeof(e.name)-1] = '\0';
    e.ip[sizeof(e.ip)-1]     = '\0';
    e.type[sizeof(e.type)-1] = '\0';
    e.time[sizeof(e.time)-1] = '\0';
    e.valid = true;
  }

  Serial.printf("[PiAlert] Events: %d\n", pa_event_count);
  return true;
}

// ---------------------------------------------------------------------------
// IP History (Mode 6) — last 20 distinct (MAC, IP) pairs seen on the network
// Useful for tracking which MAC addresses are using which IPs over time.
// ---------------------------------------------------------------------------
#define PA_MAX_MAC_HISTORY 20

struct PaMacEntry {
  char mac[18];    // "aa:bb:cc:dd:ee:ff"
  char name[32];   // device name, or "Unknown"
  char ip[16];
  char time[20];   // "YYYY-MM-DD HH:MM:SS"
  bool valid;
};

static PaMacEntry pa_mac_history[PA_MAX_MAC_HISTORY];
static int        pa_mac_count = 0;

static bool paFetchMacHistory() {
  String payload;
  int code = paPost("ip-changes", payload);
  if (code != HTTP_CODE_OK) {
    if (code != 403) snprintf(pa_last_error, sizeof(pa_last_error),
                              code == -1 ? "No connection" : "HTTP %d", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    snprintf(pa_last_error, sizeof(pa_last_error), "JSON error");
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) {
    snprintf(pa_last_error, sizeof(pa_last_error), "No history array");
    return false;
  }

  pa_mac_count = 0;
  for (JsonObject ev : arr) {
    if (pa_mac_count >= PA_MAX_MAC_HISTORY) break;
    PaMacEntry &e = pa_mac_history[pa_mac_count++];
    strncpy(e.mac,  ev["eve_MAC"]   | "",        sizeof(e.mac)  - 1);
    strncpy(e.name, ev["dev_Name"]  | "Unknown", sizeof(e.name) - 1);
    strncpy(e.ip,   ev["eve_IP"]    | "",         sizeof(e.ip)   - 1);
    strncpy(e.time, ev["last_seen"] | "",         sizeof(e.time) - 1);
    e.mac[sizeof(e.mac)-1]   = '\0';
    e.name[sizeof(e.name)-1] = '\0';
    e.ip[sizeof(e.ip)-1]     = '\0';
    e.time[sizeof(e.time)-1] = '\0';
    e.valid = true;
  }

  Serial.printf("[PiAlert] MAC history: %d entries\n", pa_mac_count);
  return true;
}

// ---------------------------------------------------------------------------
// Uptime Bars (Mode 7) — online devices with server-computed uptime in minutes
// Sorted newest-connection-first so shortest bar is at top, longest at bottom.
// ---------------------------------------------------------------------------
#define PA_MAX_UPTIME 40

struct PaUptimeEntry {
  char ip[16];
  char name[32];
  int  minutes;
  bool valid;
};

static PaUptimeEntry pa_uptime[PA_MAX_UPTIME];
static int           pa_uptime_count = 0;

static bool paFetchUptime() {
  String payload;
  int code = paPost("online-uptime", payload);
  if (code != HTTP_CODE_OK) {
    if (code != 403) snprintf(pa_last_error, sizeof(pa_last_error),
                              code == -1 ? "No connection" : "HTTP %d", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    snprintf(pa_last_error, sizeof(pa_last_error), "JSON error");
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) {
    snprintf(pa_last_error, sizeof(pa_last_error), "No uptime array");
    return false;
  }

  pa_uptime_count = 0;
  for (JsonObject dev : arr) {
    if (pa_uptime_count >= PA_MAX_UPTIME) break;
    PaUptimeEntry &e = pa_uptime[pa_uptime_count++];
    strncpy(e.ip,   dev["dev_LastIP"] | "", sizeof(e.ip)   - 1);
    strncpy(e.name, dev["dev_Name"]   | "", sizeof(e.name) - 1);
    e.minutes = dev["minutes"] | 0;
    e.ip[sizeof(e.ip)-1]     = '\0';
    e.name[sizeof(e.name)-1] = '\0';
    e.valid = true;
  }

  Serial.printf("[PiAlert] Uptime: %d devices\n", pa_uptime_count);
  return true;
}

// ---------------------------------------------------------------------------
// Device Presence (Mode 8) — days each device was seen in the last 30 days
// ---------------------------------------------------------------------------
#define PA_MAX_PRESENCE 40

struct PaPresenceEntry {
  char ip[16];
  char name[32];
  int  days;   // distinct days seen in last 30
  bool valid;
};

static PaPresenceEntry pa_presence[PA_MAX_PRESENCE];
static int             pa_presence_count = 0;

static bool paFetchPresence() {
  String payload;
  int code = paPost("device-presence", payload);
  if (code != HTTP_CODE_OK) {
    if (code != 403) snprintf(pa_last_error, sizeof(pa_last_error),
                              code == -1 ? "No connection" : "HTTP %d", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    snprintf(pa_last_error, sizeof(pa_last_error), "JSON error");
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) {
    snprintf(pa_last_error, sizeof(pa_last_error), "No presence array");
    return false;
  }

  pa_presence_count = 0;
  for (JsonObject dev : arr) {
    if (pa_presence_count >= PA_MAX_PRESENCE) break;
    PaPresenceEntry &e = pa_presence[pa_presence_count++];
    strncpy(e.ip,   dev["dev_LastIP"] | "", sizeof(e.ip)   - 1);
    strncpy(e.name, dev["dev_Name"]   | "", sizeof(e.name) - 1);
    e.days = dev["days_seen"] | 0;
    e.ip[sizeof(e.ip)-1]     = '\0';
    e.name[sizeof(e.name)-1] = '\0';
    e.valid = true;
  }

  Serial.printf("[PiAlert] Presence: %d devices\n", pa_presence_count);
  return true;
}

// ---------------------------------------------------------------------------
// CYD Device Scanner (Mode 9) — probes each online IP for GET /identify,
// auto-renames unknowns in Pi.Alert via set-device-name POST.
// ---------------------------------------------------------------------------
#define PA_MAX_CYD 20

struct PaCydDevice {
  char ip[16];
  char name[32];
  char version[16];
  int  rssi;
  unsigned long uptime_s;
  unsigned long last_fetch;
  uint32_t      errors;
  bool valid;
};

static PaCydDevice pa_cyd[PA_MAX_CYD];
static int         pa_cyd_count = 0;
static bool        pa_cyd_stale = false;  // true when last scan found 0 (showing old data)

// POST set=device-name&mac=<mac>&name=<name> to Pi.Alert API
static void paSetDeviceName(const char *mac, const char *name) {
  if (WiFi.status() != WL_CONNECTED) return;
  char url[80];
  snprintf(url, sizeof(url), "http://%s/pialert/api/", pa_host);
  char body[128];
  snprintf(body, sizeof(body), "api-key=%s&set=device-name&mac=%s&name=%s",
           pa_apikey, mac, name);
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(4000);
  if (!http.begin(client, url)) return;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.POST(body);
  http.end();
}

static bool paFetchCydDevices() {
  // Fetch all known device IPs — includes "offline" devices since ESP32s don't
  // respond to ping and Pi.Alert often marks them offline incorrectly.
  String payload;
  int code = paPost("all-device-ips", payload);
  if (code != HTTP_CODE_OK) {
    if (code != 403) snprintf(pa_last_error, sizeof(pa_last_error),
                              code == -1 ? "No connection" : "HTTP %d", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    snprintf(pa_last_error, sizeof(pa_last_error), "JSON error");
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) return false;

  // Build results into a temp buffer — only replace pa_cyd if we find something,
  // so stale results from the previous scan stay visible if the device misses a probe.
  PaCydDevice tmp[PA_MAX_CYD];
  int tmp_count = 0;

  for (JsonObject dev : arr) {
    if (WiFi.status() != WL_CONNECTED) break;
    const char *ip  = dev["dev_LastIP"] | "";
    const char *mac = dev["dev_MAC"]    | "";
    if (ip[0] == '\0') continue;

    // Use WiFiClient::connect(ip, port, timeout_ms) — this is the ONLY way to
    // control TCP connect timeout. http.setTimeout() only sets the read timeout;
    // silent-drop devices (firewalled) wait 10-20s each via lwIP default otherwise.
    WiFiClient client;
    if (!client.connect(ip, 80, 350)) { client.stop(); continue; } // 350ms connect timeout

    // Send HTTP request on the already-connected socket (no second connect = no
    // TCP pre-check race that breaks ESP32 WebServer)
    client.print(String("GET /identify HTTP/1.0\r\nHost: ") + ip + "\r\nConnection: close\r\n\r\n");

    // Wait for response header
    unsigned long t0 = millis();
    while (!client.available() && millis() - t0 < 500) delay(5);
    if (!client.available()) { client.stop(); continue; }

    // Read full response (body is after \r\n\r\n)
    String resp;
    t0 = millis();
    while (client.connected() && millis() - t0 < 800) {
      while (client.available()) { resp += (char)client.read(); t0 = millis(); }
      delay(1);
    }
    client.stop();

    int bodyStart = resp.indexOf("\r\n\r\n");
    if (bodyStart < 0) continue;
    String iPayload = resp.substring(bodyStart + 4);

    JsonDocument iDoc;
    if (deserializeJson(iDoc, iPayload)) continue;

    if (tmp_count >= PA_MAX_CYD) break;
    PaCydDevice &c = tmp[tmp_count++];
    strncpy(c.ip,      ip,                              sizeof(c.ip)      - 1);
    strncpy(c.name,    iDoc["name"]    | "Unknown",     sizeof(c.name)    - 1);
    strncpy(c.version, iDoc["version"] | "?",           sizeof(c.version) - 1);
    c.rssi      = iDoc["rssi"]      | 0;
    c.uptime_s  = iDoc["uptime_s"]  | (unsigned long)0;
    c.last_fetch= iDoc["last_fetch"]| (unsigned long)0;
    c.errors    = iDoc["errors"]    | 0;
    c.ip[sizeof(c.ip)-1]           = '\0';
    c.name[sizeof(c.name)-1]       = '\0';
    c.version[sizeof(c.version)-1] = '\0';
    c.valid = true;

    // Auto-rename in Pi.Alert if MAC known and name is meaningful
    const char *devName = dev["dev_Name"] | "";
    bool isUnknown = (devName[0] == '\0' ||
                      strcasecmp(devName, "unknown")   == 0 ||
                      strcmp(devName,    "(unknown)")  == 0);
    if (isUnknown && mac[0] != '\0' && strcmp(c.name, "Unknown") != 0) {
      paSetDeviceName(mac, c.name);
      Serial.printf("[CYD] Auto-named %s → %s\n", mac, c.name);
    }
  }

  if (tmp_count > 0) {
    // Fresh results — replace display data and clear stale flag
    memcpy(pa_cyd, tmp, tmp_count * sizeof(PaCydDevice));
    pa_cyd_count = tmp_count;
    pa_cyd_stale = false;
  } else if (pa_cyd_count > 0) {
    // Nothing found this scan but we have prior results — keep them, mark stale
    pa_cyd_stale = true;
  }
  // If count was already 0 and still 0, stale stays false (no data ever found)

  Serial.printf("[PiAlert] CYD devices found: %d (stale=%d)\n", pa_cyd_count, pa_cyd_stale);
  return true;  // scan itself succeeded even if no CYD devices responded
}

// ---------------------------------------------------------------------------
// Returns just the last octet of an IP string ("192.168.0.5" -> "5")
// ---------------------------------------------------------------------------
static void paLastOctet(const char *ip, char *buf, size_t bufLen) {
  const char *last = strrchr(ip, '.');
  if (last && *(last + 1) != '\0') {
    strncpy(buf, last + 1, bufLen - 1);
    buf[bufLen - 1] = '\0';
  } else {
    strncpy(buf, ip, bufLen - 1);
    buf[bufLen - 1] = '\0';
  }
}

// ---------------------------------------------------------------------------
// Fetch system status (Mode 0)
// ---------------------------------------------------------------------------
static bool paFetchStatus() {
  String payload;
  int code = paPost("system-status", payload);
  if (code != HTTP_CODE_OK) {
    if (code != 403) snprintf(pa_last_error, sizeof(pa_last_error),
                              code == -1 ? "No connection" : "HTTP %d", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    snprintf(pa_last_error, sizeof(pa_last_error), "JSON error");
    return false;
  }

  pa_status.all_devices = doc["All_Devices"]    | 0;
  pa_status.online      = doc["Online_Devices"] | 0;
  pa_status.offline     = doc["Offline_Devices"]| 0;
  pa_status.new_devices = doc["New_Devices"]    | 0;
  pa_status.down        = doc["Down_Devices"]   | 0;

  const char *scan = doc["Last_Scan"] | "";
  strncpy(pa_status.last_scan, scan, sizeof(pa_status.last_scan) - 1);
  pa_status.last_scan[sizeof(pa_status.last_scan) - 1] = '\0';

  pa_status.valid = true;
  Serial.printf("[PiAlert] Status: %d total, %d online, %d offline, %d new\n",
                pa_status.all_devices, pa_status.online,
                pa_status.offline, pa_status.new_devices);
  return true;
}

// ---------------------------------------------------------------------------
// Fetch online devices (Mode 1)
// ---------------------------------------------------------------------------
static bool paFetchOnline() {
  String payload;
  int code = paPost("all-online", payload);
  if (code != HTTP_CODE_OK) {
    if (code != 403) snprintf(pa_last_error, sizeof(pa_last_error),
                              code == -1 ? "No connection" : "HTTP %d", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    snprintf(pa_last_error, sizeof(pa_last_error), "JSON error");
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) {
    snprintf(pa_last_error, sizeof(pa_last_error), "No device array");
    return false;
  }

  pa_online_count = 0;
  for (JsonObject dev : arr) {
    if (pa_online_count >= PA_MAX_DEVICES) break;
    PaDevice &d = pa_online[pa_online_count++];
    strncpy(d.name,   dev["dev_Name"]   | "Unknown", sizeof(d.name)   - 1);
    strncpy(d.ip,     dev["dev_LastIP"] | "",         sizeof(d.ip)     - 1);
    strncpy(d.vendor, dev["dev_Vendor"] | "",         sizeof(d.vendor) - 1);
    d.name[sizeof(d.name)-1]     = '\0';
    d.ip[sizeof(d.ip)-1]         = '\0';
    d.vendor[sizeof(d.vendor)-1] = '\0';
    d.valid = true;
  }

  Serial.printf("[PiAlert] Online: %d devices\n", pa_online_count);
  return true;
}

// ---------------------------------------------------------------------------
// Fetch offline devices (Mode 2)
// ---------------------------------------------------------------------------
static bool paFetchOffline() {
  String payload;
  int code = paPost("all-offline", payload);
  if (code != HTTP_CODE_OK) {
    if (code != 403) snprintf(pa_last_error, sizeof(pa_last_error),
                              code == -1 ? "No connection" : "HTTP %d", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    snprintf(pa_last_error, sizeof(pa_last_error), "JSON error");
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) {
    snprintf(pa_last_error, sizeof(pa_last_error), "No device array");
    return false;
  }

  pa_offline_count = 0;
  for (JsonObject dev : arr) {
    if (pa_offline_count >= PA_MAX_DEVICES) break;
    PaDevice &d = pa_offline[pa_offline_count++];
    strncpy(d.name,   dev["dev_Name"]   | "Unknown", sizeof(d.name)   - 1);
    strncpy(d.ip,     dev["dev_LastIP"] | "",         sizeof(d.ip)     - 1);
    strncpy(d.vendor, dev["dev_Vendor"] | "",         sizeof(d.vendor) - 1);
    d.name[sizeof(d.name)-1]     = '\0';
    d.ip[sizeof(d.ip)-1]         = '\0';
    d.vendor[sizeof(d.vendor)-1] = '\0';
    d.valid = true;
  }

  Serial.printf("[PiAlert] Offline: %d devices\n", pa_offline_count);
  return true;
}

// ---------------------------------------------------------------------------
// Fetch new/unknown devices (Mode 3)
// ---------------------------------------------------------------------------
static bool paFetchNew() {
  String payload;
  int code = paPost("all-new", payload);
  if (code != HTTP_CODE_OK) {
    if (code != 403) snprintf(pa_last_error, sizeof(pa_last_error),
                              code == -1 ? "No connection" : "HTTP %d", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    snprintf(pa_last_error, sizeof(pa_last_error), "JSON error");
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) {
    snprintf(pa_last_error, sizeof(pa_last_error), "No device array");
    return false;
  }

  pa_new_count = 0;
  for (JsonObject dev : arr) {
    if (pa_new_count >= PA_MAX_NEW) break;
    PaNewDevice &d = pa_new_devices[pa_new_count++];
    strncpy(d.name,       dev["dev_Name"]            | "(unknown)", sizeof(d.name)       - 1);
    strncpy(d.ip,         dev["dev_LastIP"]           | "",          sizeof(d.ip)         - 1);
    strncpy(d.vendor,     dev["dev_Vendor"]           | "",          sizeof(d.vendor)     - 1);
    strncpy(d.mac,        dev["dev_MAC"]              | "",          sizeof(d.mac)        - 1);
    strncpy(d.first_seen, dev["dev_FirstConnection"]  | "",          sizeof(d.first_seen) - 1);
    d.name[sizeof(d.name)-1]             = '\0';
    d.ip[sizeof(d.ip)-1]                 = '\0';
    d.vendor[sizeof(d.vendor)-1]         = '\0';
    d.mac[sizeof(d.mac)-1]               = '\0';
    d.first_seen[sizeof(d.first_seen)-1] = '\0';
    d.valid = true;
  }

  Serial.printf("[PiAlert] New devices: %d\n", pa_new_count);
  return true;
}
