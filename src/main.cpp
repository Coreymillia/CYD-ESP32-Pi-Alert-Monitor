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

#include <XPT2046_Touchscreen.h>

// ---------------------------------------------------------------------------
// Touch — XPT2046 on VSPI (standard CYD wiring)
// ---------------------------------------------------------------------------
#define XPT2046_IRQ   36
#define XPT2046_MOSI  32
#define XPT2046_MISO  39
#define XPT2046_CLK   25
#define XPT2046_CS    33
#define TOUCH_DEBOUNCE 400

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
static unsigned long lastTouchTime = 0;

#include "Portal.h"
#include "PiAlert.h"
#include "ArpWatch.h"
#include "ArpStatus.h"

// ---------------------------------------------------------------------------
// Device identity — reported via GET /identify on port 80
// ---------------------------------------------------------------------------
#define DEVICE_NAME      "CYDPiAlert"
#define FIRMWARE_VERSION "1.0.0"
#include "CYDIdentity.h"

// ---------------------------------------------------------------------------
// Display modes
// ---------------------------------------------------------------------------
#define MODE_DASHBOARD  0
#define MODE_ONLINE     1
#define MODE_OFFLINE    2
#define MODE_NEW        3
#define MODE_DOWN       4
#define MODE_EVENTS     5
#define MODE_MAC        6
#define MODE_UPTIME     7
#define MODE_PRESENCE   8
#define MODE_ESP        9
#define MODE_ARP        10
#define MODE_ARP_STATUS 11
#define NUM_MODES       12

static int  currentMode            = MODE_DASHBOARD;
static bool modeHasData[NUM_MODES] = {false};

static const char *modeTitle[] = {
  "Pi.Alert",
  "Online Devices",
  "Offline Devices",
  "New Devices",
  "Down Devices",
  "Recent Events",
  "IP History",
  "Uptime",
  "Presence",
  "ESP Devices",
  "ARP Watch",
  "ARP Status"
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
#define COLOR_MAC      0xFFE0   // yellow — IP History mode

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
  } else if (currentMode == MODE_NEW || currentMode == MODE_DOWN) {
    gfx->setCursor(2,  COLHDR_Y + 1); gfx->print(".IP");
    gfx->setCursor(26, COLHDR_Y + 1); gfx->print("NAME / VENDOR");
    gfx->setCursor(162, COLHDR_Y + 1); gfx->print(".IP");
    gfx->setCursor(186, COLHDR_Y + 1); gfx->print("NAME / VENDOR");
  } else if (currentMode == MODE_EVENTS) {
    gfx->setCursor(2,  COLHDR_Y + 1); gfx->print("TIME");
    gfx->setCursor(38, COLHDR_Y + 1); gfx->print("TYPE");
    gfx->setCursor(86, COLHDR_Y + 1); gfx->print(".IP");
    gfx->setCursor(112, COLHDR_Y + 1); gfx->print("DEVICE");
  } else if (currentMode == MODE_MAC) {
    gfx->setCursor(2,  COLHDR_Y + 1); gfx->print(".IP");
    gfx->setCursor(26, COLHDR_Y + 1); gfx->print("NAME  /  MAC");
    gfx->setCursor(162, COLHDR_Y + 1); gfx->print(".IP");
    gfx->setCursor(186, COLHDR_Y + 1); gfx->print("NAME  /  MAC");
  } else if (currentMode == MODE_UPTIME) {
    gfx->setCursor(2,   COLHDR_Y + 1); gfx->print(".IP UPTIME");
    gfx->setCursor(162, COLHDR_Y + 1); gfx->print(".IP UPTIME");
  } else if (currentMode == MODE_PRESENCE) {
    gfx->setCursor(2,   COLHDR_Y + 1); gfx->print(".IP PRESENCE/30d");
    gfx->setCursor(162, COLHDR_Y + 1); gfx->print(".IP PRESENCE/30d");
  } else if (currentMode == MODE_ESP) {
    gfx->setCursor(2,   COLHDR_Y + 1); gfx->print(".IP");
    gfx->setCursor(30,  COLHDR_Y + 1); gfx->print("NAME");
    gfx->setCursor(136, COLHDR_Y + 1); gfx->print("VER");
    gfx->setCursor(184, COLHDR_Y + 1); gfx->print("RSSI");
    gfx->setCursor(232, COLHDR_Y + 1); gfx->print("UP");
  } else if (currentMode == MODE_ARP) {
    gfx->setCursor(2,   COLHDR_Y + 1); gfx->print("TYPE");
    gfx->setCursor(72,  COLHDR_Y + 1); gfx->print("IP");
    gfx->setCursor(210, COLHDR_Y + 1); gfx->print("TIME");
  } else if (currentMode == MODE_ARP_STATUS) {
    // Show live gateway + rate + status in the sub-header bar
    if (arp_status_data.valid) {
      gfx->setCursor(2, COLHDR_Y + 1);
      gfx->print(arp_status_data.gateway_ip[0] ? arp_status_data.gateway_ip : "GW?");
      char rateBuf[10];
      snprintf(rateBuf, sizeof(rateBuf), "  %.1f/m", arp_status_data.arp_rate);
      gfx->print(rateBuf);
      uint16_t sc = (strcmp(arp_status_data.status, "anomaly") == 0) ? COLOR_NEW  :
                    (strcmp(arp_status_data.status, "warning") == 0) ? 0xFFE0 : COLOR_ONLINE;
      gfx->setTextColor(sc);
      gfx->setCursor(246, COLHDR_Y + 1);
      gfx->print(arp_status_data.status);
    } else {
      gfx->setCursor(2, COLHDR_Y + 1); gfx->print("ARP STATUS");
    }
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
// Mode 3 — New/unknown devices: 2 columns, 2-line rows, up to 40 devices.
// Top line: .IP  Name (red if named, dim if unknown)
// Bottom line: vendor if known, MAC if not — always dimmed
// ---------------------------------------------------------------------------
void drawNewDevices() {
  gfx->fillRect(0, ROWS_Y, gfx->width(), gfx->height() - ROWS_Y, RGB565_BLACK);

  if (pa_new_count == 0) {
    gfx->setTextColor(COLOR_ONLINE);
    gfx->setTextSize(1);
    gfx->setCursor(4, ROWS_Y + 7);
    gfx->print("No new devices detected.");
    gfx->setCursor(4, ROWS_Y + 20);
    gfx->setTextColor(COLOR_DIM);
    gfx->print("Network looks clean!");
    return;
  }

  // Count banner
  char banner[40];
  snprintf(banner, sizeof(banner), "%d UNACKNOWLEDGED DEVICE%s",
           pa_new_count, pa_new_count == 1 ? "" : "S");
  gfx->setTextColor(COLOR_NEW);
  gfx->setTextSize(1);
  gfx->setCursor(4, ROWS_Y + 2);
  gfx->print(banner);
  gfx->drawFastHLine(0, ROWS_Y + 12, gfx->width(), COLOR_DIM);

  const int colW     = gfx->width() / 2;   // 160px
  const int rowH     = 21;                  // 2 lines — same as other modes
  const int listY    = ROWS_Y + 14;
  const int nameChars = (colW - 26) / 6;   // ~22 chars
  const int subChars  = (colW - 4)  / 6;   // ~26 chars for vendor/MAC line

  gfx->drawFastVLine(colW, ROWS_Y + 12, gfx->height() - ROWS_Y - 12, COLOR_DIM);

  for (int i = 0; i < pa_new_count; i++) {
    int col   = i / 10;
    int row   = i % 10;
    if (col >= 2) break;  // max 20 shown (10 per col)
    int xBase = col * colW;
    int y     = listY + row * rowH;

    gfx->fillRect(xBase, y, colW - 1, rowH, RGB565_BLACK);

    PaNewDevice &d = pa_new_devices[i];

    // Top line: .IP  Name
    char octet[6];
    paLastOctet(d.ip, octet, sizeof(octet));
    char ipBuf[8];
    snprintf(ipBuf, sizeof(ipBuf), ".%s", octet);
    gfx->setTextColor(COLOR_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(xBase + 2, y + 2);
    gfx->print(ipBuf);

    bool isUnknown = (d.name[0] == '\0' ||
                      strcmp(d.name, "Unknown")   == 0 ||
                      strcmp(d.name, "(unknown)") == 0 ||
                      strcmp(d.name, "unknown")   == 0);
    char nameBuf[26];
    truncate(d.name, nameBuf, nameChars);
    gfx->setTextColor(isUnknown ? COLOR_DIM : COLOR_NEW);
    gfx->setCursor(xBase + 26, y + 2);
    gfx->print(nameBuf);

    // Bottom line: vendor if known (and not "(Unknown)"), else MAC
    bool hasVendor = (d.vendor[0] != '\0' &&
                      strcmp(d.vendor, "(Unknown)") != 0 &&
                      strcmp(d.vendor, "Unknown")   != 0);
    const char *subLabel = hasVendor ? d.vendor : d.mac;
    char subBuf[28];
    truncate(subLabel, subBuf, subChars);
    gfx->setTextColor(0x4208);  // dark grey
    gfx->setCursor(xBase + 2, y + 12);
    gfx->print(subBuf);
  }
}

// ---------------------------------------------------------------------------
// Mode 4 — Down devices (monitored devices currently offline)
// ---------------------------------------------------------------------------
void drawDownDevices() {
  gfx->fillRect(0, ROWS_Y, gfx->width(), gfx->height() - ROWS_Y, RGB565_BLACK);

  if (pa_down_count == 0) {
    gfx->setTextColor(COLOR_ONLINE);
    gfx->setTextSize(1);
    gfx->setCursor(4, ROWS_Y + 7);
    gfx->print("All monitored devices are up!");
    return;
  }

  const int nameChars = (gfx->width() - 56) / 6;

  for (int i = 0; i < pa_down_count && i < 10; i++) {
    int y = ROWS_Y + i * ROW_H;
    gfx->fillRect(0, y, gfx->width(), ROW_H, RGB565_BLACK);

    PaDownDevice &d = pa_down[i];

    char octet[6];
    paLastOctet(d.ip, octet, sizeof(octet));
    char ipBuf[8];
    snprintf(ipBuf, sizeof(ipBuf), ".%s", octet);
    gfx->setTextColor(COLOR_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(2, y + 7);
    gfx->print(ipBuf);

    char nameBuf[52];
    truncate(d.name, nameBuf, nameChars);
    gfx->setTextColor(COLOR_NEW);  // red — these are down
    gfx->setCursor(26, y + 7);
    gfx->print(nameBuf);

    if (d.vendor[0] != '\0') {
      char vendBuf[20];
      truncate(d.vendor, vendBuf, 18);
      gfx->setTextColor(COLOR_DIM);
      gfx->setCursor(176, y + 7);
      gfx->print(vendBuf);
    }
  }
}

// ---------------------------------------------------------------------------
// Mode 5 — Recent events (connect / disconnect feed)
// ---------------------------------------------------------------------------
void drawEvents() {
  // Row height: 10px per row fits 20 events in ~210px
  const int evROW_H   = 10;
  const int typeChars = 7;   // "Connect" fits in 42px
  const int nameChars = (gfx->width() - 112) / 6;  // remaining after .IP col

  gfx->fillRect(0, ROWS_Y, gfx->width(), gfx->height() - ROWS_Y, RGB565_BLACK);

  if (pa_event_count == 0) {
    gfx->setTextColor(COLOR_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(4, ROWS_Y + 7);
    gfx->print("No recent events.");
    return;
  }

  for (int i = 0; i < pa_event_count; i++) {
    int y = ROWS_Y + i * evROW_H;
    PaEvent &e = pa_events[i];

    // Event type colour: green=Connected, grey=Disconnected, red=everything else
    uint16_t typeColor;
    if (strncmp(e.type, "Connected", 9) == 0)         typeColor = COLOR_ONLINE;
    else if (strncmp(e.type, "Disconnected", 12) == 0) typeColor = COLOR_DIM;
    else                                                typeColor = COLOR_NEW;

    // Time — show only HH:MM from "YYYY-MM-DD HH:MM:SS"
    char timeBuf[6] = "??:??";
    if (strlen(e.time) >= 16) {
      strncpy(timeBuf, e.time + 11, 5);
      timeBuf[5] = '\0';
    }
    gfx->setTextColor(COLOR_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(2, y + 1);
    gfx->print(timeBuf);

    // Event type (truncated to 7 chars: "Connect", "Disconn", etc.)
    char typeBuf[10];
    strncpy(typeBuf, e.type, typeChars);
    typeBuf[typeChars] = '\0';
    gfx->setTextColor(typeColor);
    gfx->setCursor(38, y + 1);
    gfx->print(typeBuf);

    // IP last octet
    char octet[6];
    paLastOctet(e.ip, octet, sizeof(octet));
    char ipBuf[8];
    snprintf(ipBuf, sizeof(ipBuf), ".%s", octet);
    gfx->setTextColor(COLOR_DIM);
    gfx->setCursor(86, y + 1);
    gfx->print(ipBuf);

    // Device name
    char nameBuf[52];
    truncate(e.name, nameBuf, nameChars);
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(112, y + 1);
    gfx->print(nameBuf);
  }
}

// ---------------------------------------------------------------------------
// Mode 6 — IP History: 20 most recently seen (MAC → IP) pairs in 2 columns
// Each entry shows 2 lines: ".IP  Name" on top, MAC dimmed below.
// ---------------------------------------------------------------------------
void drawMacHistory() {
  const int colW      = gfx->width() / 2;   // 160px
  const int nameChars = (colW - 26) / 6;    // ~22 chars for name
  const int macChars  = (colW - 4)  / 6;    // ~26 chars for MAC (fits 17)

  gfx->drawFastVLine(colW, ROWS_Y, gfx->height() - ROWS_Y, COLOR_DIM);

  for (int i = 0; i < PA_MAX_MAC_HISTORY; i++) {
    int col   = i / 10;
    int row   = i % 10;
    int xBase = col * colW;
    int y     = ROWS_Y + row * ROW_H;

    gfx->fillRect(xBase, y, colW - 1, ROW_H, RGB565_BLACK);
    if (i >= pa_mac_count || !pa_mac_history[i].valid) continue;

    PaMacEntry &e = pa_mac_history[i];

    // Top line: .IP  Name
    char octet[6];
    paLastOctet(e.ip, octet, sizeof(octet));
    char ipBuf[8];
    snprintf(ipBuf, sizeof(ipBuf), ".%s", octet);
    gfx->setTextColor(COLOR_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(xBase + 2, y + 2);
    gfx->print(ipBuf);

    bool isUnknown = (e.name[0] == '\0' ||
                      strcmp(e.name, "Unknown")   == 0 ||
                      strcmp(e.name, "(unknown)") == 0 ||
                      strcmp(e.name, "unknown")   == 0);
    char nameBuf[26];
    truncate(e.name, nameBuf, nameChars);
    gfx->setTextColor(isUnknown ? COLOR_DIM : COLOR_MAC);
    gfx->setCursor(xBase + 26, y + 2);
    gfx->print(nameBuf);

    // Bottom line: MAC address (always shown, dimmed)
    char macBuf[20];
    truncate(e.mac, macBuf, macChars);
    gfx->setTextColor(0x4208);  // dark grey — subtle but readable
    gfx->setCursor(xBase + 2, y + 12);
    gfx->print(macBuf);
  }
}


// ---------------------------------------------------------------------------
// Mode 7 — Uptime Bars: two columns of 20 rows each (up to 40 devices).
// Left col = devices 0-19, right col = devices 20-39, shortest bar at top.
// Bar color: green if named, grey if unknown. Time label right of bar.
// ---------------------------------------------------------------------------
void drawUptimeBars() {
  const int rowH    = 10;
  const int colW    = gfx->width() / 2;   // 160px each
  const int ipW     = 26;                  // ".xxx" label
  const int timeW   = 24;                  // "13d" label
  const int barX    = ipW + 2;
  const int barMaxW = colW - barX - timeW - 2;

  gfx->fillRect(0, ROWS_Y, gfx->width(), gfx->height() - ROWS_Y, RGB565_BLACK);
  gfx->drawFastVLine(colW, ROWS_Y, gfx->height() - ROWS_Y, COLOR_DIM);

  if (pa_uptime_count == 0) {
    gfx->setTextColor(COLOR_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(4, ROWS_Y + 7);
    gfx->print("No online devices.");
    return;
  }

  // Cap scale at 2 weeks — anything older fills the bar completely
  int maxMin = 20160;
  for (int i = 0; i < pa_uptime_count; i++) {
    if (pa_uptime[i].minutes > maxMin) pa_uptime[i].minutes = maxMin;
  }

  for (int i = 0; i < pa_uptime_count; i++) {
    int col   = i / 20;          // 0 = left, 1 = right
    int row   = i % 20;          // 0–19
    int xBase = col * colW;
    int y     = ROWS_Y + row * rowH;

    PaUptimeEntry &e = pa_uptime[i];

    bool isUnknown = (e.name[0] == '\0' ||
                      strcmp(e.name, "Unknown")   == 0 ||
                      strcmp(e.name, "(unknown)") == 0 ||
                      strcmp(e.name, "unknown")   == 0);

    // IP last octet
    char octet[6];
    paLastOctet(e.ip, octet, sizeof(octet));
    char ipBuf[8];
    snprintf(ipBuf, sizeof(ipBuf), ".%s", octet);
    gfx->setTextColor(COLOR_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(xBase + 2, y + 1);
    gfx->print(ipBuf);

    // Proportional bar
    int barW = (int)((long)e.minutes * barMaxW / maxMin);
    if (barW < 1 && e.minutes > 0) barW = 1;
    uint16_t barColor = isUnknown ? 0x2945 : COLOR_ONLINE;
    gfx->fillRect(xBase + barX, y + 1, barW, rowH - 2, barColor);
    gfx->fillRect(xBase + barX + barW, y + 1, barMaxW - barW, rowH - 2, RGB565_BLACK);

    // Time label: "Xm", "Xh", or "Xd"
    char timeBuf[6];
    if (e.minutes < 60)        snprintf(timeBuf, sizeof(timeBuf), "%dm",  e.minutes);
    else if (e.minutes < 1440) snprintf(timeBuf, sizeof(timeBuf), "%dh",  e.minutes / 60);
    else                       snprintf(timeBuf, sizeof(timeBuf), "%dd",  e.minutes / 1440);
    gfx->setTextColor(isUnknown ? COLOR_DIM : COLOR_TEXT);
    gfx->setCursor(xBase + colW - timeW, y + 1);
    gfx->print(timeBuf);
  }
}

// ---------------------------------------------------------------------------
// Mode 8 — Presence Bars: days each device was seen in last 30 days.
// Green bar = seen 20+ days, yellow = 7–19 days, grey = <7 or unknown.
// Two columns of 20 rows; label shows count of days (max 30).
// ---------------------------------------------------------------------------
void drawPresenceBars() {
  const int rowH    = 10;
  const int colW    = gfx->width() / 2;   // 160px each
  const int ipW     = 26;                  // ".xxx" label
  const int daysW   = 18;                  // "30" label (2 chars)
  const int barX    = ipW + 2;
  const int barMaxW = colW - barX - daysW - 2;
  const int maxDays = 30;

  gfx->fillRect(0, ROWS_Y, gfx->width(), gfx->height() - ROWS_Y, RGB565_BLACK);
  gfx->drawFastVLine(colW, ROWS_Y, gfx->height() - ROWS_Y, COLOR_DIM);

  if (pa_presence_count == 0) {
    gfx->setTextColor(COLOR_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(4, ROWS_Y + 7);
    gfx->print("No device data.");
    return;
  }

  for (int i = 0; i < pa_presence_count; i++) {
    int col   = i / 20;
    int row   = i % 20;
    int xBase = col * colW;
    int y     = ROWS_Y + row * rowH;

    PaPresenceEntry &e = pa_presence[i];

    bool isUnknown = (e.name[0] == '\0' ||
                      strcmp(e.name, "Unknown")   == 0 ||
                      strcmp(e.name, "(unknown)") == 0 ||
                      strcmp(e.name, "unknown")   == 0);

    // IP last octet
    char octet[6];
    paLastOctet(e.ip, octet, sizeof(octet));
    char ipBuf[8];
    snprintf(ipBuf, sizeof(ipBuf), ".%s", octet);
    gfx->setTextColor(COLOR_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(xBase + 2, y + 1);
    gfx->print(ipBuf);

    // Proportional bar — green if frequent, yellow if occasional, grey if rare
    int days = e.days > maxDays ? maxDays : e.days;
    int barW = (days > 0) ? (int)((long)days * barMaxW / maxDays) : 0;
    if (barW < 1 && days > 0) barW = 1;
    uint16_t barColor;
    if      (isUnknown) barColor = 0x2945;          // dark grey
    else if (days >= 20) barColor = COLOR_ONLINE;   // green
    else if (days >= 7)  barColor = 0xFFE0;         // yellow
    else                 barColor = COLOR_DIM;       // grey

    gfx->fillRect(xBase + barX, y + 1, barW, rowH - 2, barColor);
    gfx->fillRect(xBase + barX + barW, y + 1, barMaxW - barW, rowH - 2, RGB565_BLACK);

    // Day count label
    char daysBuf[4];
    snprintf(daysBuf, sizeof(daysBuf), "%d", days);
    gfx->setTextColor(isUnknown ? COLOR_DIM : COLOR_TEXT);
    gfx->setCursor(xBase + colW - daysW, y + 1);
    gfx->print(daysBuf);
  }
}

// ---------------------------------------------------------------------------
// Mode 9 — CYD Devices: lists all ESP32 CYD devices that responded to /identify.
// Each row: IP | NAME | VER | RSSI | UPTIME
// Green name = healthy (errors==0), red = has error flags set.
// ---------------------------------------------------------------------------
void drawCydDevices() {
  gfx->fillRect(0, ROWS_Y, gfx->width(), gfx->height() - ROWS_Y, RGB565_BLACK);

  if (pa_cyd_count == 0) {
    gfx->setTextColor(COLOR_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(4, ROWS_Y + 7);
    gfx->print("No ESP devices found.");
    gfx->setCursor(4, ROWS_Y + 20);
    gfx->print("(probing all known IPs for /identify)");
    return;
  }

  const int rowH = 18;

  for (int i = 0; i < pa_cyd_count; i++) {
    PaCydDevice &c = pa_cyd[i];
    int y = ROWS_Y + i * rowH;

    // IP last octet
    char octet[6];
    paLastOctet(c.ip, octet, sizeof(octet));
    char ipBuf[8];
    snprintf(ipBuf, sizeof(ipBuf), ".%s", octet);
    gfx->setTextColor(COLOR_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(2, y + 2);
    gfx->print(ipBuf);

    // Name — yellow if stale (missed last scan), red if errors, green if fresh
    gfx->setTextColor(pa_cyd_stale ? 0xFFE0 : (c.errors ? COLOR_NEW : COLOR_ONLINE));
    gfx->setCursor(30, y + 2);
    char nameBuf[18];
    truncate(c.name, nameBuf, 17);
    gfx->print(nameBuf);

    // Version
    gfx->setTextColor(COLOR_DIM);
    gfx->setCursor(136, y + 2);
    char verBuf[8];
    truncate(c.version, verBuf, 7);
    gfx->print(verBuf);

    // RSSI (e.g. "-65")
    char rssiBuf[6];
    snprintf(rssiBuf, sizeof(rssiBuf), "%d", c.rssi);
    gfx->setTextColor(c.rssi > -70 ? COLOR_ONLINE : COLOR_DIM);
    gfx->setCursor(184, y + 2);
    gfx->print(rssiBuf);

    // Uptime label
    char upBuf[8];
    unsigned long s = c.uptime_s;
    if      (s < 60)        snprintf(upBuf, sizeof(upBuf), "%lus",  s);
    else if (s < 3600)      snprintf(upBuf, sizeof(upBuf), "%lum",  s / 60);
    else if (s < 86400)     snprintf(upBuf, sizeof(upBuf), "%luh",  s / 3600);
    else                    snprintf(upBuf, sizeof(upBuf), "%lud",  s / 86400);
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(232, y + 2);
    gfx->print(upBuf);

    // Divider line
    if (i < pa_cyd_count - 1)
      gfx->drawFastHLine(0, y + rowH - 1, gfx->width(), 0x1082);
  }
}

// ---------------------------------------------------------------------------
// Mode 10 — ARP Watch: anomalies detected by arpwatch_daemon.py on the Pi.
// Each row (21px): line 1 = [TYPE] [IP] [HH:MM], line 2 = old_mac > new_mac
// Colour: GATEWAY_MAC/ARP_SPOOF = red, MAC_CHANGE = yellow, clean = green.
// ---------------------------------------------------------------------------
#define COLOR_SPOOF 0xFC00   // orange — ARP_SPOOF

void drawArpAlerts() {
  gfx->fillRect(0, ROWS_Y, gfx->width(), gfx->height() - ROWS_Y, RGB565_BLACK);

  if (arp_alert_count == 0) {
    gfx->setTextColor(COLOR_ONLINE);
    gfx->setTextSize(1);
    gfx->setCursor(4, ROWS_Y + 7);
    gfx->print("No ARP anomalies detected.");
    gfx->setTextColor(COLOR_DIM);
    gfx->setCursor(4, ROWS_Y + 20);
    gfx->print("Network looks clean!");
    return;
  }

  for (int i = 0; i < arp_alert_count; i++) {
    ArpAlert &a = arp_alerts[i];
    int y = ROWS_Y + i * ROW_H;

    // Alert type colour
    uint16_t typeColor;
    if      (strncmp(a.type, "GATEWAY_MAC", 11) == 0) typeColor = COLOR_NEW;
    else if (strncmp(a.type, "ARP_SPOOF",    9) == 0) typeColor = COLOR_SPOOF;
    else                                               typeColor = 0xFFE0;  // yellow

    // Line 1: TYPE (left) + IP (mid) + HH:MM (right)
    gfx->setTextSize(1);
    gfx->setTextColor(typeColor);
    gfx->setCursor(2, y + 2);
    gfx->print(a.type);

    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(72, y + 2);
    gfx->print(a.ip);

    // Time: extract HH:MM from "YYYY-MM-DD HH:MM:SS"
    char timeBuf[6] = "";
    if (strlen(a.time) >= 16) {
      strncpy(timeBuf, a.time + 11, 5);
      timeBuf[5] = '\0';
    }
    gfx->setTextColor(COLOR_DIM);
    gfx->setCursor(210, y + 2);
    gfx->print(timeBuf);

    // Line 2: old_mac > new_mac (dimmed; 17+3+17 = 37 chars = 222px — fits)
    gfx->setTextColor(0x4208);  // dark grey
    gfx->setCursor(2, y + 12);
    char macLine[40];
    snprintf(macLine, sizeof(macLine), "%s>%s", a.old_mac, a.new_mac);
    gfx->print(macLine);
  }
}

// ---------------------------------------------------------------------------
// Mode 11 — ARP Status Dashboard
// Full-width layout (320×212 content area):
//   Status banner (18px)  — colored bg, status text + current gateway MAC
//   Detail rows (11px ea) — CUR/EXP MAC, GW IP+changes+dupes, rate, anomaly
//   Divider → Top Talkers (3 rows with mini bars)
//   Divider → Last Events (5 rows: IP · MAC · req/rep · HH:MM:SS)
// ---------------------------------------------------------------------------
#define COLOR_WARN   0xFFE0   // yellow
#define COLOR_SPOOF2 0xFC00   // orange

void drawArpStatus() {
  gfx->fillRect(0, ROWS_Y, gfx->width(), gfx->height() - ROWS_Y, RGB565_BLACK);

  if (!arp_status_data.valid) {
    gfx->setTextColor(COLOR_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(4, ROWS_Y + 7);
    gfx->print("No ARP status data.");
    gfx->setCursor(4, ROWS_Y + 20);
    gfx->print("Is arpwatch_daemon.py running?");
    return;
  }

  // ── Status banner ────────────────────────────────────────────────────────
  uint16_t sBg, sFg;
  const char *sLabel;
  if      (strcmp(arp_status_data.status, "anomaly") == 0) { sBg=0x8000; sFg=COLOR_NEW;   sLabel="!! ANOMALY !!"; }
  else if (strcmp(arp_status_data.status, "warning") == 0) { sBg=0x8420; sFg=COLOR_WARN;  sLabel="! WARNING";    }
  else                                                      { sBg=0x0280; sFg=COLOR_ONLINE; sLabel="OK";          }

  gfx->fillRect(0, ROWS_Y, gfx->width(), 18, sBg);
  gfx->setTextColor(sFg);
  gfx->setTextSize(1);
  gfx->setCursor(4, ROWS_Y + 5);
  gfx->print(sLabel);

  // Current gateway MAC right-justified on the banner
  const char *curMac = arp_status_data.gateway_mac_cur[0]
                       ? arp_status_data.gateway_mac_cur : "learning...";
  gfx->setCursor(320 - (int)strlen(curMac) * 6 - 2, ROWS_Y + 5);
  gfx->print(curMac);

  // ── Detail rows ──────────────────────────────────────────────────────────
  int y = ROWS_Y + 21;  // just below the banner

  // CUR: current gateway MAC (truth)
  gfx->setTextColor(COLOR_DIM);  gfx->setTextSize(1);
  gfx->setCursor(2, y);   gfx->print("CUR");
  gfx->setTextColor(COLOR_TEXT);
  gfx->setCursor(24, y);  gfx->print(curMac);
  y += 11;

  // EXP: expected MAC + MATCH/DIFF indicator
  const char *expMac = arp_status_data.gateway_mac_exp[0]
                       ? arp_status_data.gateway_mac_exp : "unknown";
  bool macMatch = (arp_status_data.gateway_mac_exp[0] != '\0' &&
                   strcmp(arp_status_data.gateway_mac_cur, arp_status_data.gateway_mac_exp) == 0);
  gfx->setTextColor(COLOR_DIM);
  gfx->setCursor(2, y);   gfx->print("EXP");
  gfx->setTextColor(COLOR_TEXT);
  gfx->setCursor(24, y);  gfx->print(expMac);
  gfx->setTextColor(macMatch ? COLOR_ONLINE : COLOR_NEW);
  gfx->setCursor(260, y); gfx->print(macMatch ? "MATCH" : "DIFF!");
  y += 11;

  // GW IP + change count + dupe count
  gfx->setTextColor(COLOR_DIM);  gfx->setCursor(2, y);   gfx->print("GW");
  gfx->setTextColor(COLOR_TEXT); gfx->setCursor(16, y);  gfx->print(arp_status_data.gateway_ip);
  gfx->setTextColor(COLOR_DIM);  gfx->setCursor(110, y); gfx->print("chg:");
  gfx->setTextColor(arp_status_data.gw_mac_changes > 0 ? COLOR_NEW : COLOR_ONLINE);
  char chgBuf[4]; snprintf(chgBuf, sizeof(chgBuf), "%d", arp_status_data.gw_mac_changes);
  gfx->setCursor(140, y); gfx->print(chgBuf);
  gfx->setTextColor(COLOR_DIM);  gfx->setCursor(168, y); gfx->print("dup:");
  gfx->setTextColor(arp_status_data.duplicate_count > 0 ? COLOR_WARN : COLOR_ONLINE);
  char dupBuf[6]; snprintf(dupBuf, sizeof(dupBuf), "%d", arp_status_data.duplicate_count);
  gfx->setCursor(198, y); gfx->print(dupBuf);
  y += 11;

  // ARP rate + last ARP timestamp
  char rateBuf[12]; snprintf(rateBuf, sizeof(rateBuf), "%.1f/m", arp_status_data.arp_rate);
  gfx->setTextColor(COLOR_DIM);  gfx->setCursor(2, y);   gfx->print("rate:");
  gfx->setTextColor(arp_status_data.arp_rate > ANOMALY_RATE ? COLOR_NEW :
                    arp_status_data.arp_rate > WARN_RATE     ? COLOR_WARN : COLOR_ONLINE);
  gfx->setCursor(36, y);  gfx->print(rateBuf);
  gfx->setTextColor(COLOR_DIM);  gfx->setCursor(100, y); gfx->print("last:");
  char arpTs[9] = "--:--:--";
  if (strlen(arp_status_data.last_arp_ts) >= 19) {
    strncpy(arpTs, arp_status_data.last_arp_ts + 11, 8); arpTs[8] = '\0';
  }
  gfx->setTextColor(COLOR_TEXT); gfx->setCursor(136, y); gfx->print(arpTs);
  y += 11;

  // Last anomaly type + time
  bool hasAnomaly = (strcmp(arp_status_data.last_anomaly, "none") != 0 &&
                     arp_status_data.last_anomaly[0] != '\0');
  gfx->setTextColor(COLOR_DIM); gfx->setCursor(2, y); gfx->print("evt:");
  gfx->setTextColor(hasAnomaly ? COLOR_NEW : COLOR_ONLINE);
  char anomBuf[22]; strncpy(anomBuf, arp_status_data.last_anomaly, 21); anomBuf[21] = '\0';
  gfx->setCursor(30, y); gfx->print(anomBuf);
  if (hasAnomaly && strlen(arp_status_data.last_anomaly_ts) >= 16) {
    char anomTs[6]; strncpy(anomTs, arp_status_data.last_anomaly_ts + 11, 5); anomTs[5] = '\0';
    gfx->setTextColor(COLOR_DIM); gfx->setCursor(272, y); gfx->print(anomTs);
  }
  y += 11;

  // ── Top Talkers ──────────────────────────────────────────────────────────
  gfx->drawFastHLine(0, y, gfx->width(), COLOR_DIM); y += 3;
  gfx->setTextColor(COLOR_DIM); gfx->setCursor(2, y); gfx->print("TOP TALKERS");
  y += 9;

  const int ipColW  = 28;
  const int cntColW = 24;
  const int barMaxW = gfx->width() - ipColW - cntColW - 4;
  int maxCount = 1;
  for (int i = 0; i < arp_talker_count; i++)
    if (arp_talkers[i].count > maxCount) maxCount = arp_talkers[i].count;

  int n = arp_talker_count < 3 ? arp_talker_count : 3;
  if (n == 0) {
    gfx->setTextColor(COLOR_DIM); gfx->setCursor(2, y); gfx->print("no data yet"); y += 10;
  }
  for (int i = 0; i < n; i++) {
    char octet[6]; paLastOctet(arp_talkers[i].ip, octet, sizeof(octet));
    char ipBuf[8]; snprintf(ipBuf, sizeof(ipBuf), ".%s", octet);
    gfx->setTextColor(COLOR_DIM); gfx->setTextSize(1);
    gfx->setCursor(2, y); gfx->print(ipBuf);

    int barW = (int)((long)arp_talkers[i].count * barMaxW / maxCount);
    if (barW < 1 && arp_talkers[i].count > 0) barW = 1;
    gfx->fillRect(ipColW + 2, y, barW,          8, 0x2945);
    gfx->fillRect(ipColW + 2 + barW, y, barMaxW - barW, 8, RGB565_BLACK);

    char cntBuf[8]; snprintf(cntBuf, sizeof(cntBuf), "%d", arp_talkers[i].count);
    gfx->setTextColor(COLOR_TEXT);
    gfx->setCursor(gfx->width() - cntColW, y); gfx->print(cntBuf);
    y += 10;
  }

  // ── Last Events ──────────────────────────────────────────────────────────
  gfx->drawFastHLine(0, y, gfx->width(), COLOR_DIM); y += 3;
  gfx->setTextColor(COLOR_DIM); gfx->setCursor(2, y); gfx->print("LAST EVENTS");
  y += 9;

  if (arp_last_event_count == 0) {
    gfx->setTextColor(COLOR_DIM); gfx->setCursor(2, y); gfx->print("no events yet");
    return;
  }

  for (int i = 0; i < arp_last_event_count; i++) {
    if (y > gfx->height() - 9) break;
    ArpLastEvent &e = arp_last_events[i];

    // .IP
    char octet[6]; paLastOctet(e.ip, octet, sizeof(octet));
    char ipBuf[8]; snprintf(ipBuf, sizeof(ipBuf), ".%s", octet);
    gfx->setTextColor(COLOR_DIM); gfx->setCursor(2, y); gfx->print(ipBuf);

    // MAC (full 17 chars)
    gfx->setTextColor(0x4208); gfx->setCursor(32, y); gfx->print(e.mac);

    // type: "rep" (green) or "req" (yellow)
    bool isReply = (strncmp(e.type, "reply", 5) == 0);
    gfx->setTextColor(isReply ? COLOR_ONLINE : COLOR_WARN);
    gfx->setCursor(136, y); gfx->print(isReply ? "rep" : "req");

    // HH:MM:SS
    gfx->setTextColor(COLOR_DIM); gfx->setCursor(162, y); gfx->print(e.ts);
    y += 9;
  }
}

// ---------------------------------------------------------------------------
// Fetch + redraw for the current mode
void refreshDisplay() {
  // ESP Devices scan is slow — update only the header bar so the previous results
  // stay visible on screen instead of going blank for the entire scan duration.
  if (currentMode == MODE_ESP) {
    // Show "Scanning..." in the top chrome bar only; body keeps last results
    gfx->fillRect(0, HEADER_Y, gfx->width(), HEADER_H, 0x0010);
    gfx->setTextColor(COLOR_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(4, 3);
    gfx->print("Scanning...");
    bool ok = paFetchCydDevices();
    modeHasData[currentMode] = true;
    drawChrome();
    drawCydDevices();
    return;
  }

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
  if (currentMode == MODE_DOWN)      ok = paFetchDown();
  if (currentMode == MODE_EVENTS)    ok = paFetchEvents();
  if (currentMode == MODE_MAC)       ok = paFetchMacHistory();
  if (currentMode == MODE_UPTIME)    ok = paFetchUptime();
  if (currentMode == MODE_PRESENCE)  ok = paFetchPresence();
  if (currentMode == MODE_ARP)        ok = paFetchArpAlerts();
  if (currentMode == MODE_ARP_STATUS) ok = paFetchArpStatus();

  if (ok) {
    modeHasData[currentMode] = true;
    drawChrome();
    if (currentMode == MODE_DASHBOARD) drawDashboard();
    if (currentMode == MODE_ONLINE)    drawDeviceList(pa_online,  pa_online_count);
    if (currentMode == MODE_OFFLINE)   drawDeviceList(pa_offline, pa_offline_count);
    if (currentMode == MODE_NEW)       drawNewDevices();
    if (currentMode == MODE_DOWN)      drawDownDevices();
    if (currentMode == MODE_EVENTS)    drawEvents();
    if (currentMode == MODE_MAC)       drawMacHistory();
    if (currentMode == MODE_UPTIME)    drawUptimeBars();
    if (currentMode == MODE_PRESENCE)  drawPresenceBars();
    if (currentMode == MODE_ARP)        drawArpAlerts();
    if (currentMode == MODE_ARP_STATUS) drawArpStatus();
  } else if (modeHasData[currentMode]) {
    gfx->fillRect(0, HEADER_Y, gfx->width(), HEADER_H, 0x3000);  // dark red
    char errMsg[52];
    snprintf(errMsg, sizeof(errMsg), "ERR: %s", pa_last_error);
    gfx->setTextColor(COLOR_NEW);
    gfx->setTextSize(1);
    gfx->setCursor(4, 3);
    gfx->print(errMsg);
  } else {
    drawChrome();
    gfx->fillRect(0, ROWS_Y, gfx->width(), gfx->height() - ROWS_Y, RGB565_BLACK);
    char errMsg[52];
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

  // Touch controller
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

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

  identityBegin();

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

  // Touch: left half = previous mode, right half = next mode
  if (ts.tirqTouched() && ts.touched()) {
    unsigned long now = millis();
    if (now - lastTouchTime > TOUCH_DEBOUNCE) {
      lastTouchTime = now;
      TS_Point p = ts.getPoint();
      int tx = map(p.x, 200, 3900, 0, gfx->width());
      tx = constrain(tx, 0, gfx->width() - 1);
      if (tx < gfx->width() / 2)
        currentMode = (currentMode + NUM_MODES - 1) % NUM_MODES;  // left → prev
      else
        currentMode = (currentMode + 1) % NUM_MODES;              // right → next
      refreshDisplay();
      lastRefresh = millis();
    }
  }

  // Countdown bar: 1px at y=239, drains left→right over REFRESH_INTERVAL
  unsigned long elapsed = millis() - lastRefresh;
  int barW = (elapsed >= REFRESH_INTERVAL)
             ? 0
             : (int)((REFRESH_INTERVAL - elapsed) * (long)gfx->width() / REFRESH_INTERVAL);
  gfx->drawFastHLine(0,    gfx->height() - 1, barW,                RGB565_BLUE);
  gfx->drawFastHLine(barW, gfx->height() - 1, gfx->width() - barW, RGB565_BLACK);

  delay(20);
  identityHandle();
}
