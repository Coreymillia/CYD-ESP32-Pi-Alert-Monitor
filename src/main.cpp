// CYDPiAlert - Pi.Alert Network Monitor for CYD (Cheap Yellow Display)
// Displays network presence data from a local Pi.Alert instance on an
// ILI9341 320x240 TFT. Updates every 30 seconds.
// Setup: First boot opens CYDPiAlert_Setup AP. Hold BOOT button (3s)
//        at any time to re-enter setup.

#include <Arduino.h>
#include <WiFi.h>

/*******************************************************************************
 * Display setup - CYD (Cheap Yellow Display) proven working config
 * ILI9341 320x240 landscape via hardware SPI
 ******************************************************************************/
#include <Arduino_GFX_Library.h>

#define GFX_BL 21

Arduino_DataBus *bus = new Arduino_HWSPI(
    2  /* DC */,
    15 /* CS */,
    14 /* SCK */,
    13 /* MOSI */,
    12 /* MISO */);

Arduino_GFX *gfx = new Arduino_ILI9341(bus, GFX_NOT_DEFINED /* RST */, 1 /* rotation: landscape */);
/*******************************************************************************
 * End of display setup
 ******************************************************************************/

#include "Portal.h"
#include "PiAlert.h"

// ---------------------------------------------------------------------------
// Display modes
// ---------------------------------------------------------------------------
#define MODE_DASHBOARD  0
#define MODE_ONLINE     1
#define MODE_OFFLINE    2
#define MODE_NEW        3
#define NUM_MODES       4

static int currentMode = MODE_DASHBOARD;

static const char *modeTitle[] = {
  "Pi.Alert",
  "Online Devices",
  "Offline Devices",
  "New Devices"
};

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------
#define HEADER_Y   0
#define HEADER_H   14
#define COLHDR_Y   15
#define COLHDR_H   10
#define DIVIDER_Y  26
#define ROWS_Y     28
#define ROW_H      21

#define COLOR_ONLINE   0x07E0   // green
#define COLOR_OFFLINE  0x8410   // grey
#define COLOR_NEW      0xF800   // red
#define COLOR_HEADER   0x001F   // dark blue
#define COLOR_TEXT     RGB565_WHITE
#define COLOR_DIM      0x8410   // grey

// ---------------------------------------------------------------------------
// Status display (used during WiFi connect / errors)
// ---------------------------------------------------------------------------
void showStatus(const char *msg) {
  gfx->fillRect(0, 0, gfx->width(), HEADER_H, COLOR_HEADER);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(4, 3);
  gfx->print(msg);
  Serial.println(msg);
}

// ---------------------------------------------------------------------------
// Truncate string to maxChars, appending ".." if cut
// ---------------------------------------------------------------------------
void truncate(const char *src, char *dst, int maxChars) {
  int len = strlen(src);
  if (len <= maxChars) {
    strcpy(dst, src);
  } else {
    strncpy(dst, src, maxChars - 2);
    dst[maxChars - 2] = '.';
    dst[maxChars - 1] = '.';
    dst[maxChars]     = '\0';
  }
}

// ---------------------------------------------------------------------------
// Draw the header bar and column labels for the current mode
// ---------------------------------------------------------------------------
void drawChrome() {
  gfx->fillRect(0, HEADER_Y, gfx->width(), HEADER_H, COLOR_HEADER);
  gfx->setTextColor(0x07FF);  // cyan
  gfx->setTextSize(1);
  gfx->setCursor(4, 3);
  gfx->print(modeTitle[currentMode]);
  gfx->setTextColor(COLOR_DIM);
  gfx->setCursor(100, 3);
  gfx->print(pa_host);

  gfx->fillRect(0, COLHDR_Y, gfx->width(), COLHDR_H, RGB565_BLACK);
  gfx->setTextColor(COLOR_DIM);
  gfx->setTextSize(1);

  if (currentMode == MODE_ONLINE || currentMode == MODE_OFFLINE) {
    gfx->setCursor(2,  COLHDR_Y + 1); gfx->print(".IP");
    gfx->setCursor(26, COLHDR_Y + 1); gfx->print("DEVICE");
    gfx->setCursor(162, COLHDR_Y + 1); gfx->print(".IP");
    gfx->setCursor(186, COLHDR_Y + 1); gfx->print("DEVICE");
  } else if (currentMode == MODE_NEW) {
    gfx->setCursor(2,  COLHDR_Y + 1); gfx->print(".IP");
    gfx->setCursor(26, COLHDR_Y + 1); gfx->print("NAME");
    gfx->setCursor(148, COLHDR_Y + 1); gfx->print("VENDOR");
  }
  // MODE_DASHBOARD: no column headers

  gfx->drawFastHLine(0, DIVIDER_Y, gfx->width(), COLOR_DIM);
}

// ---------------------------------------------------------------------------
// Mode 0 — Dashboard: counts + last scan time
// ---------------------------------------------------------------------------
void drawDashboard() {
  gfx->fillRect(0, ROWS_Y, gfx->width(), gfx->height() - ROWS_Y, RGB565_BLACK);
  if (!pa_status.valid) return;

  // Row 1: All Devices | Online
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_DIM);
  gfx->setCursor(2,   ROWS_Y + 4);  gfx->print("ALL DEVICES");
  gfx->setCursor(164, ROWS_Y + 4);  gfx->print("ONLINE NOW");

  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_TEXT);
  gfx->setCursor(2,   ROWS_Y + 14); gfx->print(pa_status.all_devices);
  gfx->setTextColor(COLOR_ONLINE);
  gfx->setCursor(164, ROWS_Y + 14); gfx->print(pa_status.online);

  gfx->drawFastHLine(0, ROWS_Y + 38, gfx->width(), COLOR_DIM);

  // Row 2: Offline | New Devices
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_DIM);
  gfx->setCursor(2,   ROWS_Y + 46); gfx->print("OFFLINE");
  gfx->setCursor(164, ROWS_Y + 46); gfx->print("NEW DEVICES");

  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_OFFLINE);
  gfx->setCursor(2,   ROWS_Y + 56); gfx->print(pa_status.offline);

  // New devices: red if any, green if none
  gfx->setTextColor(pa_status.new_devices > 0 ? COLOR_NEW : COLOR_ONLINE);
  gfx->setCursor(164, ROWS_Y + 56); gfx->print(pa_status.new_devices);

  gfx->drawFastHLine(0, ROWS_Y + 80, gfx->width(), COLOR_DIM);

  // Last scan time — extract HH:MM:SS from "YYYY-MM-DD HH:MM:SS"
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_DIM);
  gfx->setCursor(2, ROWS_Y + 88);
  gfx->print("Last scan:");

  gfx->setTextColor(COLOR_TEXT);
  gfx->setCursor(66, ROWS_Y + 88);
  // last_scan format: "2024-02-22 14:32:05" — show only time (offset 11)
  const char *timeStr = pa_status.last_scan;
  if (strlen(timeStr) >= 19) timeStr += 11;  // skip to HH:MM:SS
  gfx->print(timeStr);

  // Down devices row (only show if any)
  if (pa_status.down > 0) {
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_NEW);
    gfx->setCursor(2, ROWS_Y + 104);
    char buf[32];
    snprintf(buf, sizeof(buf), "! %d device(s) marked DOWN", pa_status.down);
    gfx->print(buf);
  }
}

// ---------------------------------------------------------------------------
// Modes 1 & 2 — Device list in 2 columns (10 per column = 20 total)
// ---------------------------------------------------------------------------
void drawDeviceList(PaDevice *devices, int count) {
  // Each column is 160px wide: ".IP" (4 chars=24px) + name (rest)
  const int colW     = gfx->width() / 2;  // 160px
  const int nameChars = (colW - 26) / 6;  // ~22 chars per name

  // Vertical divider between columns
  gfx->drawFastVLine(colW, ROWS_Y, gfx->height() - ROWS_Y, COLOR_DIM);

  for (int i = 0; i < PA_MAX_DEVICES; i++) {
    int col  = i / 10;         // 0 = left, 1 = right
    int row  = i % 10;         // 0–9
    int xBase = col * colW;
    int y    = ROWS_Y + row * ROW_H;

    gfx->fillRect(xBase, y, colW - 1, ROW_H, RGB565_BLACK);
    if (i >= count || !devices[i].valid) continue;

    PaDevice &d = devices[i];

    // Last octet of IP
    char octet[6];
    paLastOctet(d.ip, octet, sizeof(octet));
    char ipBuf[8];
    snprintf(ipBuf, sizeof(ipBuf), ".%s", octet);

    gfx->setTextColor(COLOR_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(xBase + 2, y + 7);
    gfx->print(ipBuf);

    // Device name — truncated to fit column
    char nameBuf[26];
    truncate(d.name, nameBuf, nameChars);
    gfx->setTextColor(currentMode == MODE_ONLINE ? COLOR_ONLINE : COLOR_TEXT);
    gfx->setCursor(xBase + 26, y + 7);
    gfx->print(nameBuf);
  }
}

// ---------------------------------------------------------------------------
// Mode 3 — New/unknown devices
// ---------------------------------------------------------------------------
void drawNewDevices() {
  gfx->fillRect(0, ROWS_Y, gfx->width(), gfx->height() - ROWS_Y, RGB565_BLACK);

  if (pa_new_count == 0) {
    // All clear
    gfx->setTextColor(COLOR_ONLINE);
    gfx->setTextSize(1);
    gfx->setCursor(4, ROWS_Y + 7);
    gfx->print("No new devices detected.");
    gfx->setCursor(4, ROWS_Y + 20);
    gfx->setTextColor(COLOR_DIM);
    gfx->print("Network looks clean!");
    return;
  }

  // Show count banner
  char banner[32];
  snprintf(banner, sizeof(banner), "%d NEW DEVICE%s DETECTED",
           pa_new_count, pa_new_count == 1 ? "" : "S");
  gfx->setTextColor(COLOR_NEW);
  gfx->setTextSize(1);
  gfx->setCursor(4, ROWS_Y + 3);
  gfx->print(banner);
  gfx->drawFastHLine(0, ROWS_Y + 13, gfx->width(), COLOR_DIM);

  // List each new device: .IP  Name  (Vendor, dimmed)
  const int listY     = ROWS_Y + 16;
  const int entryH    = 19;
  const int nameChars = 20;
  const int vendChars = (gfx->width() - 4) / 6 - 28;  // remaining after IP+name

  for (int i = 0; i < pa_new_count && i < 10; i++) {
    PaNewDevice &d = pa_new_devices[i];
    int y = listY + i * entryH;

    gfx->fillRect(0, y, gfx->width(), entryH, RGB565_BLACK);

    // IP
    char octet[6];
    paLastOctet(d.ip, octet, sizeof(octet));
    char ipBuf[8];
    snprintf(ipBuf, sizeof(ipBuf), ".%s", octet);
    gfx->setTextColor(COLOR_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(2, y + 6);
    gfx->print(ipBuf);

    // Name
    char nameBuf[24];
    truncate(d.name, nameBuf, nameChars);
    gfx->setTextColor(COLOR_NEW);
    gfx->setCursor(26, y + 6);
    gfx->print(nameBuf);

    // Vendor (dimmed, after name)
    if (d.vendor[0] != '\0') {
      char vendBuf[32];
      truncate(d.vendor, vendBuf, vendChars);
      gfx->setTextColor(COLOR_DIM);
      gfx->setCursor(148, y + 6);
      gfx->print(vendBuf);
    }
  }
}

// ---------------------------------------------------------------------------
// Fetch + redraw for the current mode
// ---------------------------------------------------------------------------
void refreshDisplay() {
  gfx->fillRect(0, HEADER_Y, gfx->width(), HEADER_H, 0x0010);
  gfx->setTextColor(COLOR_DIM);
  gfx->setTextSize(1);
  gfx->setCursor(4, 3);
  gfx->print("Refreshing...");

  bool ok = false;
  if (currentMode == MODE_DASHBOARD) ok = paFetchStatus();
  if (currentMode == MODE_ONLINE)    ok = paFetchOnline();
  if (currentMode == MODE_OFFLINE)   ok = paFetchOffline();
  if (currentMode == MODE_NEW)       ok = paFetchNew();

  drawChrome();

  if (ok) {
    if (currentMode == MODE_DASHBOARD) drawDashboard();
    if (currentMode == MODE_ONLINE)    drawDeviceList(pa_online,  pa_online_count);
    if (currentMode == MODE_OFFLINE)   drawDeviceList(pa_offline, pa_offline_count);
    if (currentMode == MODE_NEW)       drawNewDevices();
  } else {
    gfx->fillRect(0, ROWS_Y, gfx->width(), gfx->height() - ROWS_Y, RGB565_BLACK);
    char errMsg[48];
    snprintf(errMsg, sizeof(errMsg), "Fetch failed: %s", pa_last_error);
    gfx->setTextColor(COLOR_NEW);
    gfx->setTextSize(1);
    gfx->setCursor(4, ROWS_Y + 7);
    gfx->print(errMsg);
  }
}

// ---------------------------------------------------------------------------
// Button handler — short press = cycle mode, 3s hold = restart into setup
// ---------------------------------------------------------------------------
void checkButton() {
  if (digitalRead(0) != LOW) return;

  unsigned long pressStart = millis();
  while (digitalRead(0) == LOW) {
    if (millis() - pressStart >= 3000) {
      showStatus("Restarting setup...");
      Preferences prefs;
      prefs.begin("cydpialert", false);
      prefs.putBool("forceportal", true);
      prefs.end();
      delay(1000);
      ESP.restart();
    }
    delay(20);
  }

  currentMode = (currentMode + 1) % NUM_MODES;
  refreshDisplay();
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("CYDPiAlert - Pi.Alert Network Monitor");

  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }
  gfx->fillScreen(RGB565_BLACK);

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  pinMode(0, INPUT_PULLUP);

  paLoadSettings();

  bool showPortal = !pa_has_settings || pa_force_portal;

  if (!showPortal) {
    showStatus("Hold BOOT to change settings...");
    for (int i = 0; i < 30 && !showPortal; i++) {
      if (digitalRead(0) == LOW) showPortal = true;
      delay(100);
    }
  }

  if (showPortal) {
    paInitPortal();
    while (!portalDone) {
      paRunPortal();
      delay(5);
    }
    paClosePortal();
  }

  gfx->fillScreen(RGB565_BLACK);
  WiFi.mode(WIFI_STA);
  WiFi.begin(pa_wifi_ssid, pa_wifi_pass);

  int dots = 0;
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiStart > 30000) {
      char errMsg[60];
      snprintf(errMsg, sizeof(errMsg), "WiFi failed: \"%s\"", pa_wifi_ssid);
      showStatus(errMsg);
      while (true) delay(1000);
    }
    delay(500);
    char msg[48];
    snprintf(msg, sizeof(msg), "Connecting to WiFi%.*s", (dots % 4) + 1, "....");
    showStatus(msg);
    dots++;
  }

  showStatus("WiFi connected!");
  delay(400);

  drawChrome();
  refreshDisplay();
}

// ---------------------------------------------------------------------------
// Loop — refresh every 30 seconds
// ---------------------------------------------------------------------------
#define REFRESH_INTERVAL (30 * 1000UL)
unsigned long lastRefresh = 0;

void loop() {
  checkButton();
  if (lastRefresh == 0 || (millis() - lastRefresh) >= REFRESH_INTERVAL) {
    refreshDisplay();
    lastRefresh = millis();
  }
  delay(20);
}
