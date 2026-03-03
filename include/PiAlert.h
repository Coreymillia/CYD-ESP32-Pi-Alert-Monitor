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
