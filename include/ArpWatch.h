#pragma once

// ArpWatch.h — Fetch ARP anomaly alerts from the Pi.Alert arpwatch endpoint.
// Depends on paPost() and pa_last_error from PiAlert.h (included before this).

#include <ArduinoJson.h>

#define ARP_MAX_ALERTS 10

struct ArpAlert {
  char type[16];     // "GATEWAY_MAC", "MAC_CHANGE", "ARP_SPOOF"
  char ip[16];
  char old_mac[18];  // "aa:bb:cc:dd:ee:ff"
  char new_mac[18];
  char time[20];     // "YYYY-MM-DD HH:MM:SS"
  bool valid;
};

static ArpAlert arp_alerts[ARP_MAX_ALERTS];
static int      arp_alert_count = 0;

static bool paFetchArpAlerts() {
  String payload;
  int code = paPost("arp-alerts", payload);
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
    snprintf(pa_last_error, sizeof(pa_last_error), "No alerts array");
    return false;
  }

  arp_alert_count = 0;
  for (JsonObject a : arr) {
    if (arp_alert_count >= ARP_MAX_ALERTS) break;
    ArpAlert &e = arp_alerts[arp_alert_count++];
    strncpy(e.type,    a["type"]    | "UNKNOWN", sizeof(e.type)    - 1);
    strncpy(e.ip,      a["ip"]      | "",         sizeof(e.ip)      - 1);
    strncpy(e.old_mac, a["old_mac"] | "",         sizeof(e.old_mac) - 1);
    strncpy(e.new_mac, a["new_mac"] | "",         sizeof(e.new_mac) - 1);
    strncpy(e.time,    a["time"]    | "",         sizeof(e.time)    - 1);
    e.type[sizeof(e.type)-1]       = '\0';
    e.ip[sizeof(e.ip)-1]           = '\0';
    e.old_mac[sizeof(e.old_mac)-1] = '\0';
    e.new_mac[sizeof(e.new_mac)-1] = '\0';
    e.time[sizeof(e.time)-1]       = '\0';
    e.valid = true;
  }

  Serial.printf("[ARP] Alerts: %d\n", arp_alert_count);
  return true;
}
