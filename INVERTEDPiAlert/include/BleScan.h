#pragma once

// BleScan.h — Fetch BLE device list from ble_scan_daemon.py.
// Depends on paPost() and pa_last_error from PiAlert.h (included before this).

#include <ArduinoJson.h>

// ── Limits ────────────────────────────────────────────────────────────────────
#define BLE_MAX_DEVICES  12

// ── Structs ───────────────────────────────────────────────────────────────────
struct BleDevice {
  char mac[18];
  char name[24];
  int  rssi;
  bool suspicious;
  char flags[32];    // comma-joined flag names
};

struct BleScanData {
  int       device_count;
  int       suspicious_count;
  char      status[10];         // "clean" | "threat"
  BleDevice devices[BLE_MAX_DEVICES];
  char      scan_time[10];
  bool      valid;
};

static BleScanData ble_scan_data;

// ── Fetch: BLE device list ────────────────────────────────────────────────────
static bool paFetchBleDevices() {
  String payload;
  int code = paPost("ble-devices", payload);
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

  BleScanData &d = ble_scan_data;
  d.device_count     = doc["device_count"]     | 0;
  d.suspicious_count = doc["suspicious_count"] | 0;
  strncpy(d.status,    doc["status"]    | "clean", sizeof(d.status)    - 1);
  strncpy(d.scan_time, doc["scan_time"] | "",      sizeof(d.scan_time) - 1);

  JsonArray devs = doc["devices"];
  int i = 0;
  for (JsonObject dev : devs) {
    if (i >= BLE_MAX_DEVICES) break;
    BleDevice &e = d.devices[i++];
    strncpy(e.mac,  dev["mac"]  | "", sizeof(e.mac)  - 1);
    strncpy(e.name, dev["name"] | "", sizeof(e.name) - 1);
    e.rssi       = dev["rssi"]       | -100;
    e.suspicious = dev["suspicious"] | false;
    e.flags[0] = '\0';
    JsonArray flags = dev["flags"];
    for (JsonVariant f : flags) {
      if (e.flags[0]) strncat(e.flags, ",", sizeof(e.flags) - strlen(e.flags) - 1);
      strncat(e.flags, f.as<const char *>(), sizeof(e.flags) - strlen(e.flags) - 1);
    }
  }
  d.valid = true;
  return true;
}
