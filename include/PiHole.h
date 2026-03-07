#pragma once

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// Pi-hole stats (Mode 15) — from /api/stats/summary
// ---------------------------------------------------------------------------
struct PhStats {
  int   total;
  int   blocked;
  float percent_blocked;
  int   cached;
  int   unique_domains;
  float frequency;       // queries per minute
  bool  valid;
};

static PhStats ph_stats = {0, 0, 0.0f, 0, 0, 0.0f, false};

// ---------------------------------------------------------------------------
// Pi-hole top blocked domain (shown on stats screen)
// ---------------------------------------------------------------------------
#define PH_MAX_TOP_DOMAINS 3

struct PhTopDomain {
  char  domain[64];
  int   count;
};

static PhTopDomain ph_top_domains[PH_MAX_TOP_DOMAINS];
static int         ph_top_domain_count = 0;

// ---------------------------------------------------------------------------
// Pi-hole top DNS clients (Mode 16) — from /api/stats/top_clients
// ---------------------------------------------------------------------------
#define PH_MAX_CLIENTS 12

struct PhClient {
  char  ip[16];
  int   count;
};

static PhClient ph_top_clients[PH_MAX_CLIENTS];
static int      ph_client_count  = 0;
static int      ph_total_queries = 0;

// ---------------------------------------------------------------------------
// HTTP GET helper — no API key required for Pi-hole v6 public endpoints
// ---------------------------------------------------------------------------
static int phGet(const char *path, String &out) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  if (ph_host[0] == '\0') return -2;

  char url[128];
  snprintf(url, sizeof(url), "http://%s%s", ph_host, path);

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) return -1;

  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    out = http.getString();
    http.end();
    return code;
  }
  http.end();
  return code;
}

// ---------------------------------------------------------------------------
// Fetch Pi-hole summary stats + top 3 blocked domains
// Called for Mode 15
// ---------------------------------------------------------------------------
static bool phFetchStats() {
  if (ph_host[0] == '\0') return false;

  // ── Summary ───────────────────────────────────────────────────────────────
  String payload;
  int code = phGet("/api/stats/summary", payload);
  if (code != HTTP_CODE_OK) {
    Serial.printf("[PiHole] summary error %d\n", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    Serial.println("[PiHole] JSON error (summary)");
    return false;
  }

  JsonObject q = doc["queries"];
  if (q.isNull()) {
    Serial.println("[PiHole] No 'queries' key");
    return false;
  }

  ph_stats.total          = q["total"]           | 0;
  ph_stats.blocked        = q["blocked"]         | 0;
  ph_stats.percent_blocked = q["percent_blocked"] | 0.0f;
  ph_stats.cached         = q["cached"]          | 0;
  ph_stats.unique_domains = q["unique_domains"]  | 0;
  ph_stats.frequency      = q["frequency"]       | 0.0f;
  ph_stats.valid          = true;

  // ── Top blocked domains ────────────────────────────────────────────────────
  String dPayload;
  char path[64];
  snprintf(path, sizeof(path), "/api/stats/top_domains?blocked=true&count=%d",
           PH_MAX_TOP_DOMAINS);
  if (phGet(path, dPayload) == HTTP_CODE_OK) {
    JsonDocument dDoc;
    if (!deserializeJson(dDoc, dPayload)) {
      JsonArray arr = dDoc["domains"].as<JsonArray>();
      ph_top_domain_count = 0;
      for (JsonObject d : arr) {
        if (ph_top_domain_count >= PH_MAX_TOP_DOMAINS) break;
        PhTopDomain &td = ph_top_domains[ph_top_domain_count++];
        const char *dom = d["domain"] | "";
        strncpy(td.domain, dom, sizeof(td.domain) - 1);
        td.domain[sizeof(td.domain) - 1] = '\0';
        td.count = d["count"] | 0;
      }
    }
  }

  Serial.printf("[PiHole] Stats: %d total, %d blocked (%.1f%%), %.2f q/min\n",
                ph_stats.total, ph_stats.blocked,
                ph_stats.percent_blocked, ph_stats.frequency);
  return true;
}

// ---------------------------------------------------------------------------
// Fetch top DNS clients by query count
// Called for Mode 16
// ---------------------------------------------------------------------------
static bool phFetchTopClients() {
  if (ph_host[0] == '\0') return false;

  String payload;
  char path[48];
  snprintf(path, sizeof(path), "/api/stats/top_clients?count=%d", PH_MAX_CLIENTS);
  int code = phGet(path, payload);
  if (code != HTTP_CODE_OK) {
    Serial.printf("[PiHole] top_clients error %d\n", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    Serial.println("[PiHole] JSON error (top_clients)");
    return false;
  }

  ph_total_queries = doc["total_queries"] | 0;
  JsonArray arr    = doc["clients"].as<JsonArray>();
  if (arr.isNull()) return false;

  ph_client_count = 0;
  for (JsonObject c : arr) {
    if (ph_client_count >= PH_MAX_CLIENTS) break;
    PhClient &cl = ph_top_clients[ph_client_count++];
    const char *ip = c["ip"] | "";
    strncpy(cl.ip, ip, sizeof(cl.ip) - 1);
    cl.ip[sizeof(cl.ip) - 1] = '\0';
    cl.count = c["count"] | 0;
  }

  Serial.printf("[PiHole] Top clients: %d entries, %d total queries\n",
                ph_client_count, ph_total_queries);
  return true;
}
