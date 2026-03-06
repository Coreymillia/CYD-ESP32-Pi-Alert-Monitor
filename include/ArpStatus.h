#pragma once

// ArpStatus.h — Fetch the full ARP health snapshot from the Pi.Alert
//               arpwatch_daemon.py v2 endpoint.
// Depends on paPost() and pa_last_error from PiAlert.h (included before this).

#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------

struct ArpStatusData {
  char iface[8];              // "eth0"
  char gateway_ip[16];        // "192.168.0.1"
  char gateway_mac_cur[18];   // "aa:bb:cc:dd:ee:ff"  (current)
  char gateway_mac_exp[18];   // "aa:bb:cc:dd:ee:ff"  (expected / truth anchor)
  char status[10];            // "ok" | "warning" | "anomaly"
  char status_reason[40];     // human-readable reason, e.g. "ARP flood 1588/min"
  char last_arp_ts[20];       // "YYYY-MM-DD HH:MM:SS"
  float arp_rate;             // packets per minute (rolling 60 s)
  int  duplicate_count;       // lifetime duplicate-ARP / MAC-collision count
  int  gw_mac_changes;        // lifetime gateway MAC change count
  char last_anomaly[24];      // "none" | "gateway_mac_changed" | "arp_spoof" | …
  char last_anomaly_ts[20];   // "YYYY-MM-DD HH:MM:SS"
  bool valid;
};

#define ARP_MAX_TALKERS 5
struct ArpTalker {
  char ip[16];
  int  count;
};

#define ARP_MAX_EVENTS 5
struct ArpLastEvent {
  char ip[16];
  char mac[18];
  char type[8];   // "request" | "reply"
  char ts[9];     // "HH:MM:SS"
};

static ArpStatusData arp_status_data;
static ArpTalker     arp_talkers[ARP_MAX_TALKERS];
static int           arp_talker_count = 0;
static ArpLastEvent  arp_last_events[ARP_MAX_EVENTS];
static int           arp_last_event_count = 0;

// ---------------------------------------------------------------------------
// Fetch
// ---------------------------------------------------------------------------

static bool paFetchArpStatus() {
  String payload;
  int code = paPost("arp-status", payload);
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

  // Check daemon is actually running (unavailable sentinel)
  const char *st = doc["status"] | "ok";
  if (strcmp(st, "unavailable") == 0) {
    snprintf(pa_last_error, sizeof(pa_last_error), "Daemon not running");
    return false;
  }

  // --- Core fields ---
  strncpy(arp_status_data.iface,           doc["iface"]               | "eth0",    sizeof(arp_status_data.iface)           - 1);
  strncpy(arp_status_data.gateway_ip,      doc["gateway_ip"]          | "",         sizeof(arp_status_data.gateway_ip)      - 1);
  strncpy(arp_status_data.gateway_mac_cur, doc["gateway_mac_current"] | "",         sizeof(arp_status_data.gateway_mac_cur) - 1);
  strncpy(arp_status_data.gateway_mac_exp, doc["gateway_mac_expected"]| "",         sizeof(arp_status_data.gateway_mac_exp) - 1);
  strncpy(arp_status_data.status,          st,                                       sizeof(arp_status_data.status)          - 1);
  strncpy(arp_status_data.status_reason,   doc["status_reason"]       | "",         sizeof(arp_status_data.status_reason)   - 1);
  strncpy(arp_status_data.last_arp_ts,     doc["last_arp_ts"]         | "",         sizeof(arp_status_data.last_arp_ts)     - 1);
  strncpy(arp_status_data.last_anomaly,    doc["last_anomaly"]        | "none",     sizeof(arp_status_data.last_anomaly)    - 1);
  strncpy(arp_status_data.last_anomaly_ts, doc["last_anomaly_ts"]     | "",         sizeof(arp_status_data.last_anomaly_ts) - 1);

  arp_status_data.iface[sizeof(arp_status_data.iface)-1]                     = '\0';
  arp_status_data.gateway_ip[sizeof(arp_status_data.gateway_ip)-1]           = '\0';
  arp_status_data.gateway_mac_cur[sizeof(arp_status_data.gateway_mac_cur)-1] = '\0';
  arp_status_data.gateway_mac_exp[sizeof(arp_status_data.gateway_mac_exp)-1] = '\0';
  arp_status_data.status[sizeof(arp_status_data.status)-1]                   = '\0';
  arp_status_data.status_reason[sizeof(arp_status_data.status_reason)-1]     = '\0';
  arp_status_data.last_arp_ts[sizeof(arp_status_data.last_arp_ts)-1]         = '\0';
  arp_status_data.last_anomaly[sizeof(arp_status_data.last_anomaly)-1]       = '\0';
  arp_status_data.last_anomaly_ts[sizeof(arp_status_data.last_anomaly_ts)-1] = '\0';

  arp_status_data.arp_rate        = doc["arp_rate"]             | 0.0f;
  arp_status_data.duplicate_count = doc["duplicate_arp_count"]  | 0;
  arp_status_data.gw_mac_changes  = doc["gateway_mac_changes"]  | 0;
  arp_status_data.valid           = true;

  // --- Top talkers ---
  arp_talker_count = 0;
  JsonArray talkers = doc["top_talkers"].as<JsonArray>();
  if (!talkers.isNull()) {
    for (JsonObject t : talkers) {
      if (arp_talker_count >= ARP_MAX_TALKERS) break;
      strncpy(arp_talkers[arp_talker_count].ip, t["ip"] | "", sizeof(ArpTalker::ip) - 1);
      arp_talkers[arp_talker_count].ip[sizeof(ArpTalker::ip)-1] = '\0';
      arp_talkers[arp_talker_count].count = t["count"] | 0;
      arp_talker_count++;
    }
  }

  // --- Last events ---
  arp_last_event_count = 0;
  JsonArray evts = doc["last_events"].as<JsonArray>();
  if (!evts.isNull()) {
    for (JsonObject ev : evts) {
      if (arp_last_event_count >= ARP_MAX_EVENTS) break;
      ArpLastEvent &e = arp_last_events[arp_last_event_count++];
      strncpy(e.ip,   ev["ip"]   | "",        sizeof(e.ip)   - 1);
      strncpy(e.mac,  ev["mac"]  | "",        sizeof(e.mac)  - 1);
      strncpy(e.type, ev["type"] | "request", sizeof(e.type) - 1);
      strncpy(e.ts,   ev["ts"]   | "",        sizeof(e.ts)   - 1);
      e.ip[sizeof(e.ip)-1]     = '\0';
      e.mac[sizeof(e.mac)-1]   = '\0';
      e.type[sizeof(e.type)-1] = '\0';
      e.ts[sizeof(e.ts)-1]     = '\0';
    }
  }

  Serial.printf("[ARP] status=%s  rate=%.1f/m  dupes=%d  gw_chg=%d  talkers=%d  events=%d\n",
                arp_status_data.status, arp_status_data.arp_rate,
                arp_status_data.duplicate_count, arp_status_data.gw_mac_changes,
                arp_talker_count, arp_last_event_count);
  return true;
}
