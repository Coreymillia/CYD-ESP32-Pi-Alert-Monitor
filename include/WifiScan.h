#pragma once

// WifiScan.h — Fetch Wi-Fi AP scan + shady-network data from wifi_scan_daemon.py.
// Depends on paPost() and pa_last_error from PiAlert.h (included before this).

#include <ArduinoJson.h>

// ── Limits ────────────────────────────────────────────────────────────────────
#define SCAN_MAX_APS    14   // max APs kept in display list
#define SHADY_MAX_APS    6   // max shady APs kept

// ── Structs ───────────────────────────────────────────────────────────────────
struct WifiApEntry {
  char ssid[24];
  char bssid[18];
  int  channel;
  int  rssi;
  char security[6];   // "Open" | "WEP" | "WPA" | "WPA2" | "WPA3"
  bool hidden;
};

struct WifiShadyEntry {
  char ssid[24];
  char bssid[18];
  int  channel;
  int  rssi;
  char security[6];
  int  score;
  char flags[48];     // comma-joined flag names
};

struct WifiScanData {
  int         ap_count;
  WifiApEntry aps[SCAN_MAX_APS];
  char        scan_time[10];
  int         channel_counts[15];   // index 1–14; index 0 unused
  bool        valid;
};

struct WifiShadyData {
  int            shady_count;
  int            max_score;
  char           status[10];        // "clean" | "warning" | "threat"
  WifiShadyEntry shady_aps[SHADY_MAX_APS];
  char           scan_time[10];
  bool           valid;
};

static WifiScanData  wifi_scan_data;
static WifiShadyData wifi_shady_data;

// ── Fetch: AP list ────────────────────────────────────────────────────────────
static bool paFetchWifiScan() {
  String payload;
  int code = paPost("wifi-scan", payload);
  if (code != HTTP_CODE_OK) {
    snprintf(pa_last_error, sizeof(pa_last_error),
             code == -1 ? "No connection" : "HTTP %d", code);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    snprintf(pa_last_error, sizeof(pa_last_error), "JSON error");
    return false;
  }
  if (doc["error"].is<const char *>()) {
    snprintf(pa_last_error, sizeof(pa_last_error), "Daemon not running");
    return false;
  }

  WifiScanData &d = wifi_scan_data;
  d.ap_count = doc["ap_count"] | 0;
  strncpy(d.scan_time, doc["scan_time"] | "", sizeof(d.scan_time) - 1);

  JsonArray aps = doc["aps"];
  int i = 0;
  for (JsonObject ap : aps) {
    if (i >= SCAN_MAX_APS) break;
    WifiApEntry &e = d.aps[i++];
    strncpy(e.ssid,     ap["ssid"]     | "", sizeof(e.ssid)     - 1);
    strncpy(e.bssid,    ap["bssid"]    | "", sizeof(e.bssid)    - 1);
    strncpy(e.security, ap["security"] | "", sizeof(e.security) - 1);
    e.channel = ap["channel"] | 0;
    e.rssi    = ap["rssi"]    | -100;
    e.hidden  = ap["hidden"]  | false;
  }

  memset(d.channel_counts, 0, sizeof(d.channel_counts));
  JsonObject cmap = doc["channel_counts"];
  for (int ch = 1; ch <= 14; ch++) {
    char key[3]; snprintf(key, sizeof(key), "%d", ch);
    d.channel_counts[ch] = cmap[key] | 0;
  }
  d.valid = true;
  return true;
}

// ── Fetch: shady networks ─────────────────────────────────────────────────────
static bool paFetchWifiShady() {
  String payload;
  int code = paPost("wifi-shady", payload);
  if (code != HTTP_CODE_OK) {
    snprintf(pa_last_error, sizeof(pa_last_error),
             code == -1 ? "No connection" : "HTTP %d", code);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    snprintf(pa_last_error, sizeof(pa_last_error), "JSON error");
    return false;
  }
  if (doc["error"].is<const char *>()) {
    snprintf(pa_last_error, sizeof(pa_last_error), "Daemon not running");
    return false;
  }

  WifiShadyData &d = wifi_shady_data;
  d.shady_count = doc["shady_count"] | 0;
  d.max_score   = doc["max_score"]   | 0;
  strncpy(d.status,    doc["status"]    | "clean", sizeof(d.status)    - 1);
  strncpy(d.scan_time, doc["scan_time"] | "",      sizeof(d.scan_time) - 1);

  JsonArray saps = doc["shady_aps"];
  int i = 0;
  for (JsonObject ap : saps) {
    if (i >= SHADY_MAX_APS) break;
    WifiShadyEntry &e = d.shady_aps[i++];
    strncpy(e.ssid,     ap["ssid"]     | "", sizeof(e.ssid)     - 1);
    strncpy(e.bssid,    ap["bssid"]    | "", sizeof(e.bssid)    - 1);
    strncpy(e.security, ap["security"] | "", sizeof(e.security) - 1);
    e.channel = ap["channel"] | 0;
    e.rssi    = ap["rssi"]    | -100;
    e.score   = ap["score"]   | 0;
    e.flags[0] = '\0';
    JsonArray flags = ap["flags"];
    for (JsonVariant f : flags) {
      if (e.flags[0]) strncat(e.flags, ",", sizeof(e.flags) - strlen(e.flags) - 1);
      strncat(e.flags, f.as<const char *>(), sizeof(e.flags) - strlen(e.flags) - 1);
    }
  }
  d.valid = true;
  return true;
}
