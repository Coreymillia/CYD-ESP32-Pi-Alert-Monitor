# CYDPiAlert

A **Pi.Alert network presence monitor** running on the **CYD (Cheap Yellow Display)** ESP32 board.  
Fetches live network data from your local [Pi.Alert](https://github.com/pucherot/Pi.Alert) instance and displays it on the built-in 320×240 TFT, cycling through four views with the push of a button.

![Mode 0 — Dashboard](IMG_20260222_160903.jpg)
*Mode 0: Dashboard — total devices, online/offline counts, new device alert, last scan time*

---
## Features

- 4 display modes (Dashboard, Online, Offline, New Devices)
- Color‑coded device statuses
- 20‑device 2‑column layout
- Wi‑Fi captive portal for setup (no code editing required)
- Stores settings in NVS (survives reboots)
- Works with stock Pi.Alert API (only Mode 3 requires a small patch)
- 30‑second auto‑refresh cycle

## Why This Exists

Pi.Alert is powerful, but it lives in a browser tab.  
This project turns it into a **physical network presence monitor** you can keep on your desk — always visible, always updating, no browser required.

## What It Shows

| Mode | Button Press | Description |
|------|-------------|-------------|
| **0 — Dashboard** | — (default) | Total devices, online count, offline count, new devices count, last scan time |
| **1 — Online Devices** | 1 short press | All currently online devices — last IP octet + name, 2-column layout (up to 20) |
| **2 — Offline Devices** | 2 short presses | All currently offline devices — same layout |
| **3 — New Devices** | 3 short presses | Unknown/unacknowledged devices — IP, name, vendor chip (red alert if any) |

![Mode 1 — Online Devices](IMG_20260222_160929.jpg)
*Mode 1: Online Devices — 2-column layout, up to 20 devices, last IP octet + device name*

Short-press the **BOOT button** (GPIO 0) to cycle through modes.  
Hold the **BOOT button for 3 seconds** at any time to re-enter Wi-Fi/server setup.

---

## Hardware

- **Board:** ESP32 CYD (ESP32-2432S028R or compatible)
- **Display:** ILI9341 TFT 320×240, landscape
  - DC = GPIO 2, CS = GPIO 15, SCK = GPIO 14, MOSI = GPIO 13, MISO = GPIO 12
- **Backlight:** GPIO 21 (active HIGH)
- **BOOT button:** GPIO 0 (active LOW, INPUT_PULLUP)

> ⚠️ The ESP32 only supports **2.4 GHz** WiFi networks.

---

## Display Layout

```
[ Pi.Alert           192.168.0.105 ]  ← header (mode title + Pi.Alert host)
[ .IP  DEVICE        .IP  DEVICE   ]  ← column labels (modes 1 & 2)
[──────────────────────────────────]  ← divider

Mode 0 — Dashboard:
  ALL DEVICES    ONLINE NOW
  42             18
  ──────────────────────────────────
  OFFLINE        NEW DEVICES
  24             3
  ──────────────────────────────────
  Last scan: 14:32:05

Mode 1/2 — Device list (2 columns, 10 rows each):
  .5   My-PC            .22  SmartTV
  .10  Pi-Hole          .25  iPhone
  ...

Mode 3 — New Devices:
  3 NEW DEVICES DETECTED
  .116  (unknown)       (Unknown)
  .121  (unknown)       Espressif Inc.
  ...  (green "Network looks clean!" if none)
```

---

## Requirements

### Pi.Alert Server

- **Pi.Alert** installed and running (tested with Pi.Alert on Raspberry Pi OS)
- **API key** configured in Pi.Alert → **Maintenance** → **API Key**
- Pi.Alert must be accessible on your local network via HTTP

### Modes 0–2: No server changes needed

These modes use the built-in Pi.Alert API endpoints (`system-status`, `all-online`, `all-offline`) which are available by default.

### Mode 3 (New Devices): Requires a one-time server modification

Mode 3 queries an `all-new` endpoint that does **not** exist in Pi.Alert by default. You need to add it manually to the Pi.Alert API file.

---

![Mode 3 — New Devices](IMG_20260222_161106.jpg)
*Mode 3: New Devices — requires the Pi.Alert server modification below. Shows unacknowledged devices with IP, name, and vendor chip in red.*

---

## Adding the `all-new` API Endpoint to Pi.Alert

> ⚠️ This is a one-time change. It modifies one PHP file on your Pi.Alert server. It does not affect any existing functionality.

### Step 1 — SSH into your Pi.Alert server

```bash
ssh your-username@your-pi-alert-ip
```

### Step 2 — Open the API file

Pi.Alert is typically installed at `/opt/pialert/`. Open the API file:

```bash
sudo nano /opt/pialert/front/api/index.php
```

### Step 3 — Add the `all-new` case to the switch block

Find this block near the top of the file (around line 50):

```php
case 'all-offline-icmp':getAllOffline_ICMP();
    break;
}
```

Add the new case **before** the closing `}`:

```php
case 'all-offline-icmp':getAllOffline_ICMP();
    break;
case 'all-new':getAllNew();
    break;
}
```

### Step 4 — Add the `getAllNew()` function

Find the closing `?>` PHP tag near the **bottom** of the file (before the last comment block). Add the new function **before** the `?>`:

```php
//example curl -k -X POST -F 'api-key=key' -F 'get=all-new' https://url/pialert/api/
function getAllNew() {
    global $db;
    $sql = 'SELECT dev_MAC, dev_Name, dev_Vendor, dev_LastIP, dev_FirstConnection
            FROM Devices WHERE dev_NewDevice="1"
            ORDER BY dev_FirstConnection DESC LIMIT 20';
    $results = $db->query($sql);
    $devices = array();
    $i = 0;
    while ($row = $results->fetchArray()) {
        $devices[$i] = array(
            'dev_MAC'             => $row['dev_MAC'],
            'dev_Name'            => $row['dev_Name'],
            'dev_Vendor'          => $row['dev_Vendor'],
            'dev_LastIP'          => $row['dev_LastIP'],
            'dev_FirstConnection' => $row['dev_FirstConnection']
        );
        $i++;
    }
    echo json_encode($devices);
    echo "\n";
}

?>
```

> ⚠️ **Important:** The function must be placed **inside** the PHP block — before the closing `?>` tag, not after it. If placed after `?>`, PHP will treat it as plain text and it will not execute.

### Step 5 — Verify it works

```bash
curl -s -X POST \
  -F "api-key=YOUR_API_KEY_HERE" \
  -F "get=all-new" \
  http://your-pi-alert-ip/pialert/api/
```

You should get a JSON array back. An empty array `[]` means no unacknowledged devices — that's fine and correct.

---

## First-Time Setup (ESP32)

1. Flash the firmware using PlatformIO (`pio run --target upload`)
2. On first boot, the display shows **"CYDPiAlert Setup"** and the ESP32 broadcasts a WiFi access point:
   ```
   SSID: CYDPiAlert_Setup   (no password)
   ```
3. Connect your phone or PC to `CYDPiAlert_Setup`
4. Open a browser and navigate to `192.168.4.1`
5. Fill in the form:
   - **WiFi Network Name** — your home 2.4 GHz network SSID
   - **WiFi Password** — leave blank for open networks
   - **Pi.Alert IP / Hostname** — bare IP only, e.g. `192.168.0.105` (no `http://`, no `/pialert/`)
   - **Pi.Alert API Key** — found in Pi.Alert → Maintenance → API Key
6. Tap **Save & Connect**
7. Close the browser and disconnect from `CYDPiAlert_Setup`

Settings are stored in flash (NVS) and survive reboots.

### Re-entering Setup

Hold the **BOOT button for 3 seconds** while the device is running. The screen will say "Restarting setup..." and the device will reboot into setup mode.

Alternatively, hold BOOT at power-on before the 3-second window expires.

---

## Building with PlatformIO

### Dependencies (auto-installed by PlatformIO)

- `moononournation/GFX Library for Arduino @ 1.4.7`
- `bblanchon/ArduinoJson @ ^7`

### Build & Upload

```bash
cd /path/to/CYDPiAlert
pio run --target upload
```

### Serial Monitor (debug output)

```bash
pio device monitor --baud 115200
```

---

## Project Structure

```
CYDPiAlert/
├── platformio.ini          # PlatformIO config (board, libs)
├── include/
│   ├── Portal.h            # Captive portal: WiFi + Pi.Alert credentials, NVS persistence
│   └── PiAlert.h           # HTTP fetch functions + data structs for all modes
└── src/
    └── main.cpp            # Display init, mode logic, draw functions, button handling
```

---

## Troubleshooting

| Error on screen | Cause | Fix |
|----------------|-------|-----|
| `Fetch failed: Wrong API key` | API key doesn't match Pi.Alert | Re-enter setup, copy key from Pi.Alert → Maintenance → API Key |
| `Fetch failed: HTTP 404` | Wrong Pi.Alert host, or `/pialert/` path not found | Check the IP — enter bare IP only (e.g. `192.168.0.105`) |
| `Fetch failed: JSON error` | Mode 3 `all-new` endpoint missing | Follow the server modification steps above |
| `Fetch failed: No connection` | WiFi dropped | Device retries on next 30s interval; check router |
| `WiFi failed: "YourSSID"` | Wrong SSID or password | Hold BOOT 3s to re-enter setup |
| Screen stays on "Refreshing..." | Pi.Alert scan still running | Wait for Pi.Alert scan to complete (default scan interval: ~3–5 min) |

---

## Notes

- Refresh interval is **30 seconds**. Pi.Alert scans typically run every 3–5 minutes, so the dashboard data only changes that often.
- "New Devices" in Mode 3 shows devices Pi.Alert has detected but that you haven't acknowledged in the Pi.Alert web UI. You can dismiss them in Pi.Alert → Devices to clear them from this view.
- This project works with the original [Pi.Alert by pucherot](https://github.com/pucherot/Pi.Alert). Forks (like IPAM, Pi.Alert CE) may have different API paths.

---
## Upstream Project

This project depends on **Pi.Alert**, created by pucherot:

➡️ **Pi.Alert GitHub:** https://github.com/pucherot/Pi.Alert

Pi.Alert handles all device discovery, scanning, and API responses.  
CYDPiAlert simply displays that data on the ESP32 CYD.

## Related Project

**[CYDPiHole](../CYDPiHole)** — The companion project. Displays live Pi-hole v6 DNS query data on the same CYD hardware with 3 modes: live query feed, stats summary, and top blocked domains.

## License
MIT — do whatever you want with it.

