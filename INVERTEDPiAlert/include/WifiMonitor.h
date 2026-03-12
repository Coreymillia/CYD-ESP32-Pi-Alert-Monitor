#pragma once

// WifiMonitor.h — Fetch Wi-Fi RF monitoring data from wifi_monitor_daemon.py.
// Depends on paPost() and pa_last_error from PiAlert.h (included before this).

#include <ArduinoJson.h>

// ── Limits ───────────────────────────────────────────────────────────────────
#define WIFI_MAX_ROGUE  6
#define WIFI_MAX_DEAUTH 5
#define WIFI_MAX_BSSID  8

// ── Structs ───────────────────────────────────────────────────────────────────
struct WifiRogueAP {
  char bssid[18];
  char ssid[24];
  int  channel;
  int  rssi;
  bool hidden;
};

struct WifiDeauth {
  char ts[10];    // "HH:MM:SS"
  char src[18];
  char dst[18];
  int  reason;
};

struct WifiBssid {
  char bssid[18];
  int  count;
};

struct WifiStatusData {
  char status[10];         // "ok" | "warning" | "anomaly"
  char status_reason[32];
  int  deauth_count;
  int  deauth_5min;
  char last_deauth_ts[12];
  int  rogue_ap_count;
  int  probe_count;
  int  probe_rate;
  bool probe_spike;
  int  known_ap_count;
  int  hidden_ssid_count;
  bool learning;
  int  learn_ends_in;
  bool valid;
};

struct WifiDetailData {
  char        status[10];
  WifiRogueAP rogue_aps[WIFI_MAX_ROGUE];
  int         rogue_ap_count;
  WifiDeauth  recent_deauths[WIFI_MAX_DEAUTH];
  int         deauth_count;
  WifiBssid   top_bssids[WIFI_MAX_BSSID];
  int         bssid_count;
  int         channel_map[14];  // index 1–13 (index 0 unused)
  bool        probe_spike;
  int         probe_rate;
  bool        valid;
};

static WifiStatusData wifi_status_data;
static WifiDetailData wifi_detail_data;

// ── Fetch: status summary ─────────────────────────────────────────────────────
static bool paFetchWifiStatus() {
  String payload;
  int code = paPost("wifi-status", payload);
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

  WifiStatusData &d = wifi_status_data;
  strncpy(d.status,         doc["status"]         | "ok", sizeof(d.status) - 1);
  strncpy(d.status_reason,  doc["status_reason"]  | "",   sizeof(d.status_reason) - 1);
  strncpy(d.last_deauth_ts, doc["last_deauth_ts"] | "",   sizeof(d.last_deauth_ts) - 1);
  d.deauth_count     = doc["deauth_count"]      | 0;
  d.deauth_5min      = doc["deauth_5min"]        | 0;
  d.rogue_ap_count   = doc["rogue_ap_count"]     | 0;
  d.probe_count      = doc["probe_count"]         | 0;
  d.probe_rate       = doc["probe_rate"]          | 0;
  d.probe_spike      = doc["probe_spike"]         | false;
  d.known_ap_count   = doc["known_ap_count"]      | 0;
  d.hidden_ssid_count= doc["hidden_ssid_count"]   | 0;
  d.learning         = doc["learning"]            | false;
  d.learn_ends_in    = doc["learn_ends_in"]       | 0;
  d.valid            = true;
  return true;
}

// ── Fetch: full detail ────────────────────────────────────────────────────────
static bool paFetchWifiDetail() {
  String payload;
  int code = paPost("wifi-detail", payload);
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

  WifiDetailData &d = wifi_detail_data;
  strncpy(d.status, doc["status"] | "ok", sizeof(d.status) - 1);

  // Rogue APs
  JsonArray rogues = doc["rogue_aps"];
  d.rogue_ap_count = 0;
  for (JsonObject ap : rogues) {
    if (d.rogue_ap_count >= WIFI_MAX_ROGUE) break;
    WifiRogueAP &r = d.rogue_aps[d.rogue_ap_count++];
    strncpy(r.bssid, ap["bssid"] | "", sizeof(r.bssid) - 1);
    strncpy(r.ssid,  ap["ssid"]  | "", sizeof(r.ssid)  - 1);
    r.channel = ap["channel"] | 0;
    r.rssi    = ap["rssi"]    | -100;
    r.hidden  = ap["hidden"]  | false;
  }

  // Recent deauths
  JsonArray deauths = doc["recent_deauths"];
  d.deauth_count = 0;
  for (JsonObject de : deauths) {
    if (d.deauth_count >= WIFI_MAX_DEAUTH) break;
    WifiDeauth &dv = d.recent_deauths[d.deauth_count++];
    strncpy(dv.ts,  de["ts"]  | "", sizeof(dv.ts)  - 1);
    strncpy(dv.src, de["src"] | "", sizeof(dv.src) - 1);
    strncpy(dv.dst, de["dst"] | "", sizeof(dv.dst) - 1);
    dv.reason = de["reason"] | 0;
  }

  // Top BSSIDs
  JsonArray bssids = doc["top_bssids"];
  d.bssid_count = 0;
  for (JsonObject b : bssids) {
    if (d.bssid_count >= WIFI_MAX_BSSID) break;
    WifiBssid &bv = d.top_bssids[d.bssid_count++];
    strncpy(bv.bssid, b["bssid"] | "", sizeof(bv.bssid) - 1);
    bv.count = b["count"] | 0;
  }

  // Channel map (keys "1"–"13")
  memset(d.channel_map, 0, sizeof(d.channel_map));
  JsonObject chmap = doc["channel_map"];
  for (int i = 1; i <= 13; i++) {
    char key[3];
    snprintf(key, sizeof(key), "%d", i);
    d.channel_map[i] = chmap[key] | 0;
  }

  d.probe_spike = doc["probe_spike"] | false;
  d.probe_rate  = doc["probe_rate"]  | 0;
  d.valid       = true;
  return true;
}
