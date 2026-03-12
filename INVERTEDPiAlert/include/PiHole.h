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
// Auth state — SID acquired once per session, reused for all requests
// ---------------------------------------------------------------------------
static char ph_sid[65]    = "";
static bool ph_auth_done  = false;
static int  ph_last_http  = 0;
static char ph_last_err[48] = "";

static bool phAuthenticate() {
  WiFiClient ac;
  HTTPClient ah;
  ah.setTimeout(8000);
  char authUrl[96];
  snprintf(authUrl, sizeof(authUrl), "http://%s:%u/api/auth", ph_host, ph_port);
  if (!ah.begin(ac, authUrl)) return false;
  ah.addHeader("Content-Type", "application/json");
  char body[80];
  snprintf(body, sizeof(body), "{\"password\":\"%s\"}", ph_pass);
  int code = ah.POST(body);
  ph_last_http = code;
  if (code != HTTP_CODE_OK) { ah.end(); return false; }
  String payload = ah.getString();
  ah.end();
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;
  const char *sid = doc["session"]["sid"] | "";
  strncpy(ph_sid, sid, sizeof(ph_sid) - 1);
  ph_sid[sizeof(ph_sid) - 1] = '\0';
  ph_auth_done = true;
  Serial.printf("[PiHole] Auth OK, SID: %.8s...\n", ph_sid[0] ? ph_sid : "(empty)");
  return true;
}

// ---------------------------------------------------------------------------
// HTTP GET helper — authenticates once on first call, handles 401 retry
// ---------------------------------------------------------------------------
static int phGet(const char *path, String &out) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  if (ph_host[0] == '\0') return -2;

  if (!ph_auth_done && !phAuthenticate()) return -3;

  char url[200];
  if (ph_sid[0] != '\0') {
    const char *sep = strchr(path, '?') ? "&" : "?";
    snprintf(url, sizeof(url), "http://%s:%u%s%ssid=%s", ph_host, ph_port, path, sep, ph_sid);
  } else {
    snprintf(url, sizeof(url), "http://%s:%u%s", ph_host, ph_port, path);
  }

  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt > 0) delay(attempt * 600);
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(8000);
    if (!http.begin(client, url)) continue;
    int code = http.GET();
    ph_last_http = code;

    if (code == 401) {
      http.end();
      ph_sid[0] = '\0'; ph_auth_done = false;
      if (!phAuthenticate()) return 401;
      if (ph_sid[0] != '\0') {
        const char *sep = strchr(path, '?') ? "&" : "?";
        snprintf(url, sizeof(url), "http://%s:%u%s%ssid=%s", ph_host, ph_port, path, sep, ph_sid);
      }
      continue;
    }
    if (code == HTTP_CODE_OK) out = http.getString();
    http.end();
    return code;
  }
  return -1;
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

// ===========================================================================
// ── New modes ───────────────────────────────────────────────────────────────
// ===========================================================================

// ---------------------------------------------------------------------------
// Status helper — Pi-hole v6 "allowed" status strings
// ---------------------------------------------------------------------------
static bool phStatusAllowed(const char *s) {
  return (strncmp(s, "FORWARDED",  9) == 0 ||
          strncmp(s, "CACHE",      5) == 0 ||
          strncmp(s, "RETRIED",    7) == 0 ||
          strcmp (s, "IN_PROGRESS")   == 0);
}

// ---------------------------------------------------------------------------
// Live DNS query feed (MODE_PIHOLE_FEED)
// ---------------------------------------------------------------------------
#define MAX_QUERIES 10

struct PiQuery {
  char domain[64];
  char client[16];
  bool allowed;
  bool valid;
};

static PiQuery ph_queries[MAX_QUERIES];
static int     ph_query_count = 0;

static bool phFetch() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (ph_host[0] == '\0') return false;

  char path[48];
  snprintf(path, sizeof(path), "/api/queries?max=%d", MAX_QUERIES);

  String payload;
  int code = phGet(path, payload);
  if (code != HTTP_CODE_OK) {
    Serial.printf("[PiHole] queries error %d\n", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) { Serial.println("[PiHole] queries JSON err"); return false; }

  JsonArray queries = doc["queries"].as<JsonArray>();
  if (queries.isNull()) return false;

  ph_query_count = 0;
  for (JsonObject row : queries) {
    if (ph_query_count >= MAX_QUERIES) break;
    PiQuery &q = ph_queries[ph_query_count++];
    const char *domain = row["domain"]       | "";
    const char *ip     = row["client"]["ip"] | "";
    const char *status = row["status"]       | "";
    strncpy(q.domain, domain, sizeof(q.domain) - 1); q.domain[sizeof(q.domain)-1] = '\0';
    strncpy(q.client, ip,     sizeof(q.client) - 1); q.client[sizeof(q.client)-1] = '\0';
    q.allowed = phStatusAllowed(status);
    q.valid   = true;
  }

  Serial.printf("[PiHole] Fetched %d queries\n", ph_query_count);
  return true;
}

// ---------------------------------------------------------------------------
// Top blocked domains (MODE_PIHOLE_TOP_BLOCKED)
// ---------------------------------------------------------------------------
#define MAX_TOP_BLOCKED 10

struct PiBlockEntry {
  char domain[64];
  long count;
  bool valid;
};

static PiBlockEntry ph_top_blocked[MAX_TOP_BLOCKED];
static int          ph_top_blocked_count = 0;

static bool phFetchTopBlocked() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (ph_host[0] == '\0') return false;

  char path[64];
  snprintf(path, sizeof(path), "/api/stats/top_domains?blocked=true&count=%d", MAX_TOP_BLOCKED);

  String payload;
  int code = phGet(path, payload);
  if (code != HTTP_CODE_OK) {
    Serial.printf("[PiHole] top_blocked error %d\n", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;

  JsonArray domains = doc["domains"].as<JsonArray>();
  if (domains.isNull()) return false;

  ph_top_blocked_count = 0;
  for (JsonObject row : domains) {
    if (ph_top_blocked_count >= MAX_TOP_BLOCKED) break;
    PiBlockEntry &e = ph_top_blocked[ph_top_blocked_count++];
    strncpy(e.domain, row["domain"] | "", sizeof(e.domain) - 1);
    e.domain[sizeof(e.domain) - 1] = '\0';
    e.count = row["count"] | 0L;
    e.valid = true;
  }

  Serial.printf("[PiHole] Top blocked: %d entries\n", ph_top_blocked_count);
  return true;
}

// ---------------------------------------------------------------------------
// 24-hour activity history (MODE_PIHOLE_ACTIVITY) — 144 x 10-min buckets
// ---------------------------------------------------------------------------
#define MAX_HISTORY 144

struct PiHistoryPoint {
  int total;
  int blocked;
};

static PiHistoryPoint ph_history[MAX_HISTORY];
static int            ph_history_count = 0;

static bool phFetchHistory() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (ph_host[0] == '\0') return false;

  String payload;
  int code = phGet("/api/history", payload);
  if (code != HTTP_CODE_OK) {
    Serial.printf("[PiHole] history error %d\n", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;

  JsonArray hist = doc["history"].as<JsonArray>();
  if (hist.isNull()) return false;

  ph_history_count = 0;
  for (JsonObject pt : hist) {
    if (ph_history_count >= MAX_HISTORY) break;
    ph_history[ph_history_count].total   = pt["total"]   | 0;
    ph_history[ph_history_count].blocked = pt["blocked"] | 0;
    ph_history_count++;
  }

  Serial.printf("[PiHole] History: %d points\n", ph_history_count);
  return true;
}

// ---------------------------------------------------------------------------
// Returns just the last octet of an IP string  e.g. "192.168.0.5" -> "5"
// ---------------------------------------------------------------------------
static void phLastOctet(const char *ip, char *buf, size_t bufLen) {
  const char *last = strrchr(ip, '.');
  if (last && *(last + 1) != '\0') {
    strncpy(buf, last + 1, bufLen - 1);
    buf[bufLen - 1] = '\0';
  } else {
    strncpy(buf, ip, bufLen - 1);
    buf[bufLen - 1] = '\0';
  }
}
