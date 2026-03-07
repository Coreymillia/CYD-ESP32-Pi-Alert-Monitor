#pragma once

#include <Arduino_GFX_Library.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// gfx is defined in main.cpp
extern Arduino_GFX *gfx;

// ---------------------------------------------------------------------------
// Persisted settings
// ---------------------------------------------------------------------------
static char pa_wifi_ssid[64]  = "";
static char pa_wifi_pass[64]  = "";
static char pa_host[64]       = "";  // Pi.Alert IP or hostname
static char pa_apikey[128]    = "";  // Pi.Alert API key
static char ph_host[64]       = "";  // Pi-hole IP or hostname (optional)
// ph_host is also referenced in PiHole.h — declared here so Portal.h
// can load/save it before PiHole.h fetch functions run.
static bool pa_has_settings   = false;
static bool pa_force_portal   = false;

// ---------------------------------------------------------------------------
// Portal state
// ---------------------------------------------------------------------------
static WebServer *portalServer = nullptr;
static DNSServer *portalDNS    = nullptr;
static bool       portalDone   = false;

// ---------------------------------------------------------------------------
// NVS helpers
// ---------------------------------------------------------------------------
static void paLoadSettings() {
  Preferences prefs;
  prefs.begin("cydpialert", true);
  String ssid   = prefs.getString("ssid",    "");
  String pass   = prefs.getString("pass",    "");
  String host   = prefs.getString("pahost",  "");
  String apikey = prefs.getString("paapikey","");
  String phhost = prefs.getString("phhost",  "");
  bool   force  = prefs.getBool("forceportal", false);
  prefs.end();

  if (force) {
    Preferences rw;
    rw.begin("cydpialert", false);
    rw.putBool("forceportal", false);
    rw.end();
  }

  ssid.toCharArray(pa_wifi_ssid, sizeof(pa_wifi_ssid));
  pass.toCharArray(pa_wifi_pass, sizeof(pa_wifi_pass));
  host.toCharArray(pa_host,      sizeof(pa_host));
  apikey.toCharArray(pa_apikey,  sizeof(pa_apikey));
  phhost.toCharArray(ph_host,    sizeof(ph_host));
  pa_has_settings = (ssid.length() > 0 && host.length() > 0 && apikey.length() > 0);
  pa_force_portal = force;
}

static void paSaveSettings(const char *ssid, const char *pass,
                            const char *host, const char *apikey,
                            const char *phhost) {
  Preferences prefs;
  prefs.begin("cydpialert", false);
  prefs.putString("ssid",     ssid);
  prefs.putString("pass",     pass);
  prefs.putString("pahost",   host);
  prefs.putString("paapikey", apikey);
  prefs.putString("phhost",   phhost);
  prefs.end();

  strncpy(pa_wifi_ssid, ssid,   sizeof(pa_wifi_ssid)  - 1);
  strncpy(pa_wifi_pass, pass,   sizeof(pa_wifi_pass)  - 1);
  strncpy(pa_host,      host,   sizeof(pa_host)       - 1);
  strncpy(pa_apikey,    apikey, sizeof(pa_apikey)     - 1);
  strncpy(ph_host,      phhost, sizeof(ph_host)       - 1);
  pa_has_settings = true;
}

// ---------------------------------------------------------------------------
// On-screen setup instructions
// ---------------------------------------------------------------------------
static void paShowPortalScreen() {
  gfx->fillScreen(RGB565_BLACK);

  gfx->setTextColor(0x07FF);  // cyan
  gfx->setTextSize(2);
  gfx->setCursor(10, 5);
  gfx->print("CYDPiAlert Setup");

  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(50, 26);
  gfx->print("Pi.Alert Network Monitor");

  gfx->setTextColor(0xFFE0);  // yellow
  gfx->setCursor(4, 46);
  gfx->print("1. Connect your phone/PC to WiFi:");
  gfx->setTextColor(0x07FF);
  gfx->setTextSize(2);
  gfx->setCursor(14, 58);
  gfx->print("CYDPiAlert_Setup");

  gfx->setTextColor(0xFFE0);
  gfx->setTextSize(1);
  gfx->setCursor(4, 82);
  gfx->print("2. Open your browser and go to:");
  gfx->setTextColor(0x07FF);
  gfx->setTextSize(2);
  gfx->setCursor(50, 94);
  gfx->print("192.168.4.1");

  gfx->setTextColor(0xFFE0);
  gfx->setTextSize(1);
  gfx->setCursor(4, 118);
  gfx->print("3. Enter your WiFi and Pi.Alert");
  gfx->setCursor(4, 130);
  gfx->print("   details, then tap Save.");

  if (pa_has_settings) {
    gfx->setTextColor(0x07E0);  // green
    gfx->setCursor(4, 152);
    gfx->print("Existing settings found. Tap");
    gfx->setCursor(4, 164);
    gfx->print("'No Changes' to keep them.");
  }
}

// ---------------------------------------------------------------------------
// Web handlers
// ---------------------------------------------------------------------------
static void paHandleRoot() {
  String html = "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>CYDPiAlert Setup</title>"
    "<style>"
    "body{background:#0d0d1a;color:#00ccff;font-family:Arial,sans-serif;"
         "text-align:center;padding:20px;max-width:480px;margin:auto;}"
    "h1{color:#00ffff;font-size:1.6em;margin-bottom:4px;}"
    "p{color:#88aacc;font-size:0.9em;}"
    "label{display:block;text-align:left;margin:14px 0 4px;color:#88ddff;font-weight:bold;}"
    "input{width:100%;box-sizing:border-box;background:#001a33;color:#00ccff;"
          "border:2px solid #0066aa;border-radius:6px;padding:10px;font-size:1em;}"
    ".btn{display:block;width:100%;padding:14px;margin:10px 0;font-size:1.05em;"
         "border-radius:8px;border:none;cursor:pointer;font-weight:bold;}"
    ".btn-save{background:#004488;color:#00ffff;border:2px solid #0099dd;}"
    ".btn-save:hover{background:#0066bb;}"
    ".btn-skip{background:#1a1a2e;color:#667788;border:2px solid #334455;}"
    ".btn-skip:hover{background:#223344;color:#aabbcc;}"
    ".note{color:#445566;font-size:0.82em;margin-top:16px;}"
    "hr{border:1px solid #113355;margin:20px 0;}"
    "</style></head><body>"
    "<h1>&#128273; CYDPiAlert Setup</h1>"
    "<p>Enter your WiFi and Pi.Alert credentials.</p>"
    "<form method='post' action='/save'>"
    "<label>WiFi Network Name (SSID):</label>"
    "<input type='text' name='ssid' value='";
  html += String(pa_wifi_ssid);
  html += "' placeholder='Your 2.4 GHz WiFi name' maxlength='63' required>"
    "<label>WiFi Password:</label>"
    "<input type='password' name='pass' value='";
  html += String(pa_wifi_pass);
  html += "' placeholder='Leave blank if open network' maxlength='63'>"
    "<hr>"
    "<label>Pi.Alert IP / Hostname:</label>"
    "<input type='text' name='pahost' value='";
  html += String(pa_host);
  html += "' placeholder='e.g. 192.168.0.105' maxlength='63' required>"
    "<label>Pi.Alert API Key:</label>"
    "<input type='password' name='paapikey' value='";
  html += String(pa_apikey);
  html += "' placeholder='Set in Pi.Alert \u2192 Maintenance \u2192 API Key' maxlength='127' required>"
    "<hr>"
    "<label>Pi-hole IP / Hostname: <span style='color:#445566;font-weight:normal'>(optional)</span></label>"
    "<input type='text' name='phhost' value='";
  html += String(ph_host);
  html += "' placeholder='e.g. 192.168.0.103 — leave blank to disable Pi-hole modes' maxlength='63'>"
    "<br><button class='btn btn-save' type='submit'>&#128190; Save &amp; Connect</button>"
    "</form>";
  if (pa_has_settings) {
    html += "<hr>"
      "<form method='post' action='/nochange'>"
      "<button class='btn btn-skip' type='submit'>&#10006; No Changes &mdash; Use Current Settings</button>"
      "</form>";
  }
  html += "<p class='note'>&#9888; ESP32 supports 2.4 GHz WiFi only.</p>"
    "</body></html>";

  portalServer->send(200, "text/html", html);
}

static void paHandleSave() {
  String ssid   = portalServer->hasArg("ssid")     ? portalServer->arg("ssid")     : "";
  String pass   = portalServer->hasArg("pass")     ? portalServer->arg("pass")     : "";
  String host   = portalServer->hasArg("pahost")   ? portalServer->arg("pahost")   : "";
  String apikey = portalServer->hasArg("paapikey") ? portalServer->arg("paapikey") : "";
  String phhost = portalServer->hasArg("phhost")   ? portalServer->arg("phhost")   : "";

  if (ssid.length() == 0) {
    portalServer->send(400, "text/html",
      "<html><body style='background:#0d0d1a;color:#ff5555;font-family:Arial;"
      "text-align:center;padding:40px'>"
      "<h2>&#10060; SSID cannot be empty!</h2>"
      "<a href='/' style='color:#00ccff'>&#8592; Go Back</a></body></html>");
    return;
  }
  if (host.length() == 0) {
    portalServer->send(400, "text/html",
      "<html><body style='background:#0d0d1a;color:#ff5555;font-family:Arial;"
      "text-align:center;padding:40px'>"
      "<h2>&#10060; Pi.Alert IP cannot be empty!</h2>"
      "<a href='/' style='color:#00ccff'>&#8592; Go Back</a></body></html>");
    return;
  }
  if (apikey.length() == 0) {
    portalServer->send(400, "text/html",
      "<html><body style='background:#0d0d1a;color:#ff5555;font-family:Arial;"
      "text-align:center;padding:40px'>"
      "<h2>&#10060; API Key cannot be empty!</h2>"
      "<a href='/' style='color:#00ccff'>&#8592; Go Back</a></body></html>");
    return;
  }

  paSaveSettings(ssid.c_str(), pass.c_str(), host.c_str(), apikey.c_str(), phhost.c_str());

  String confirmHtml =
    "<html><head><meta charset='UTF-8'>"
    "<style>body{background:#0d0d1a;color:#00ccff;font-family:Arial;"
    "text-align:center;padding:40px;}h2{color:#00ffff;}"
    "p{color:#88aacc;}</style></head><body>"
    "<h2>&#9989; Settings Saved!</h2>"
    "<p>WiFi: <b>" + ssid + "</b></p>"
    "<p>Pi.Alert: <b>" + host + "</b></p>";
  if (phhost.length() > 0) {
    confirmHtml += "<p>Pi-hole: <b>" + phhost + "</b></p>";
  }
  confirmHtml += "<p>You can close this page and disconnect from <b>CYDPiAlert_Setup</b>.</p>"
    "</body></html>";
  portalServer->send(200, "text/html", confirmHtml);

  delay(1500);
  portalDone = true;
}

static void paHandleNoChange() {
  portalServer->send(200, "text/html",
    "<html><head><meta charset='UTF-8'>"
    "<style>body{background:#0d0d1a;color:#00ccff;font-family:Arial;"
    "text-align:center;padding:40px;}h2{color:#00ffff;}"
    "p{color:#88aacc;}</style></head><body>"
    "<h2>&#128077; No Changes</h2>"
    "<p>Using saved settings. Connecting now.</p>"
    "<p>You can close this page and disconnect from <b>CYDPiAlert_Setup</b>.</p>"
    "</body></html>");

  delay(1500);
  portalDone = true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
static void paInitPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("CYDPiAlert_Setup", "");
  delay(500);

  portalDNS    = new DNSServer();
  portalServer = new WebServer(80);

  portalDNS->start(53, "*", WiFi.softAPIP());

  portalServer->on("/",         paHandleRoot);
  portalServer->on("/save",     HTTP_POST, paHandleSave);
  portalServer->on("/nochange", HTTP_POST, paHandleNoChange);
  portalServer->onNotFound(paHandleRoot);
  portalServer->begin();

  portalDone = false;
  paShowPortalScreen();

  Serial.printf("[Portal] AP up — connect to CYDPiAlert_Setup, open %s\n",
                WiFi.softAPIP().toString().c_str());
}

static void paRunPortal() {
  portalDNS->processNextRequest();
  portalServer->handleClient();
}

static void paClosePortal() {
  portalServer->stop();
  portalDNS->stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(300);

  delete portalServer; portalServer = nullptr;
  delete portalDNS;    portalDNS    = nullptr;
}
