# CYDPiAlert

A **Pi.Alert network presence monitor** running on the **CYD (Cheap Yellow Display)** ESP32 board.  
Fetches live network data from your local [Pi.Alert](https://github.com/pucherot/Pi.Alert) instance and displays it on the built-in 320×240 TFT. **Touch the screen** left or right to cycle through seven display modes.

![Mode 0 — Dashboard](IMG_20260222_160903.jpg)
*Mode 0: Dashboard — total devices, online/offline counts, new/down device alerts, last scan time*

---

## What It Shows

| Mode | Description |
|------|-------------|
| **0 — Dashboard** | Total devices, online, offline, new devices count, down devices count, last scan time |
| **1 — Online Devices** | All currently online devices — last IP octet + name, 2-column layout (up to 20) |
| **2 — Offline Devices** | All currently offline devices — same layout |
| **3 — New Devices** | Unknown/unacknowledged devices — IP, name, vendor (red alert if any) |
| **4 — Down Devices** | Monitored devices that are currently offline and flagged as critical — shown in red |
| **5 — Recent Events** | Live feed of the last 15 connect/disconnect events — time, event type, device name |
| **6 — IP History** | Last 20 distinct MAC→IP pairs seen on the network — name on top, full MAC below (yellow if named, grey if unknown). Useful for tracking devices with changing IPs (e.g. ESP32s, phones) |

![Mode 1 — Online Devices](IMG_20260222_160929.jpg)
*Mode 1: Online Devices — 2-column layout, up to 20 devices, last IP octet + device name*

---

## Navigation

| Action | Result |
|--------|--------|
| **Touch right half of screen** | Next mode → |
| **Touch left half of screen** | ← Previous mode |
| **Short press BOOT button** | Next mode → |
| **Hold BOOT button 3 seconds** | Restart into WiFi/server setup |

A **blue countdown bar** at the very bottom of the screen drains across over 30 seconds, showing time until the next automatic refresh.

---

## Hardware

| Function | GPIO |
|----------|------|
| TFT DC | 2 |
| TFT CS | 15 |
| TFT SCK | 14 |
| TFT MOSI | 13 |
| TFT MISO | 12 |
| Backlight | 21 |
| BOOT button | 0 |
| Touch IRQ | 36 |
| Touch MOSI | 32 |
| Touch MISO | 39 |
| Touch CLK | 25 |
| Touch CS | 33 |

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
  22             3
  ──────────────────────────────────
  Last scan: 14:32:05
  ! 2 device(s) marked DOWN

Mode 1/2 — Device list (2 columns, 10 rows each):
  .5   My-PC            .22  SmartTV
  .10  Pi-Hole          .25  iPhone
  ...

Mode 3 — New Devices:
  3 NEW DEVICES DETECTED
  .116  (unknown)        Espressif Inc.
  ...  (green "Network looks clean!" if none)

Mode 4 — Down Devices:
  .12   NAS-Server       Synology Inc.
  .31   Security-Cam     Hikvision
  ...  (green "All monitored devices are up!" if none)

Mode 5 — Recent Events:
  TIME   TYPE     DEVICE
  14:32  Connect  My-PC
  14:30  Disconn  SmartTV
  ...

Mode 6 — IP History (2 columns, 10 rows each):
  .IP  NAME  /  MAC        .IP  NAME  /  MAC
  .119  Pi-Alert            .114  (unknown)
  10:52:1c:f6:5e:4c         88:57:21:43:fa:ac
  .110  LAPTOP-ABQTBN9A     .118  (unknown)
  22:6d:13:0e:32:ea         3c:8a:1f:d7:72:44
  ...  (named devices in yellow, unknown in grey)
```

---

## Error Handling

When a fetch fails (network hiccup, Pi.Alert busy):
- If the mode has **previously loaded data**, the last good data stays on screen and only the header bar turns red with the error message. Data is never wiped on a transient failure.
- If there is **no prior data** (e.g. first boot), the error is shown in the data area as usual.
- The HTTP request is retried up to **3 times** with increasing delays (800 ms, then 1600 ms) before giving up.

---

## Requirements

### Pi.Alert Server

- **Pi.Alert** installed and running (tested with the original [Pi.Alert by pucherot](https://github.com/pucherot/Pi.Alert))
- **API key** configured in Pi.Alert → **Maintenance** → **API Key**
- Pi.Alert accessible on your local network via HTTP

### Server Modifications Required

Three of the six modes use API endpoints that **do not exist** in Pi.Alert by default. You need to add them manually to one PHP file on your Pi.Alert server. See the [Pi.Alert Server Modifications](#pialert-server-modifications) section below.

| Mode | Endpoint | Requires modification? |
|------|----------|------------------------|
| 0 — Dashboard | `system-status` | No — built-in |
| 1 — Online | `all-online` | No — built-in |
| 2 — Offline | `all-offline` | No — built-in |
| 3 — New Devices | `all-new` | **Yes** |
| 4 — Down Devices | `all-down` | **Yes** |
| 5 — Recent Events | `recent-events` | **Yes** |
| 6 — IP History | `ip-changes` | **Yes** |

---

## Pi.Alert Server Modifications

> ⚠️ This is a one-time change to a single PHP file. It adds three new read-only API endpoints. It does not affect any existing Pi.Alert functionality.

The complete modified `index.php` is included in this repo at [`pialert-patch/index.php`](pialert-patch/index.php). You can either copy it directly or apply the changes manually.

### Option A — Copy the patched file directly (easiest)

```bash
# Back up the original
sudo cp /opt/pialert/front/api/index.php /opt/pialert/front/api/index.php.bak

# Copy the patched version (from this repo)
sudo cp pialert-patch/index.php /opt/pialert/front/api/index.php
sudo chown www-data:www-data /opt/pialert/front/api/index.php
```

> ⚠️ The patched file is based on Pi.Alert as installed from the official repo. If your version has local customisations, use **Option B** instead.

---

### Option B — Apply changes manually

#### Step 1 — SSH into your Pi.Alert server

```bash
ssh your-username@your-pi-alert-ip
sudo nano /opt/pialert/front/api/index.php
```

#### Step 2 — Add three new cases to the switch block

Find the switch block (around line 50) that ends with:

```php
    case 'all-new':getAllNew();
        break;
    }
```

Add the three new cases **before** the closing `}`:

```php
    case 'all-new':getAllNew();
        break;
    case 'all-down':getAllDown();
        break;
    case 'recent-events':getRecentEvents();
        break;
    }
```

#### Step 3 — Add three new functions

Find the closing `?>` tag at the very bottom of the file and add these functions **before** it:

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

//example curl -k -X POST -F 'api-key=key' -F 'get=all-down' https://url/pialert/api/
function getAllDown() {
    global $db;
    $sql = 'SELECT dev_Name, dev_LastIP, dev_Vendor FROM Devices
            WHERE dev_AlertDeviceDown=1 AND dev_PresentLastScan=0 AND dev_Archived=0
            ORDER BY dev_Name ASC';
    $results_array = array();
    $results = $db->query($sql);
    $i = 0;
    while ($row = $results->fetchArray()) {
        $results_array[$i]['dev_Name']   = $row['dev_Name'];
        $results_array[$i]['dev_LastIP'] = $row['dev_LastIP'];
        $results_array[$i]['dev_Vendor'] = $row['dev_Vendor'];
        $i++;
    }
    echo json_encode($results_array);
    echo "\n";
}

//example curl -k -X POST -F 'api-key=key' -F 'get=recent-events' https://url/pialert/api/
function getRecentEvents() {
    global $db;
    $sql = 'SELECT e.eve_DateTime, e.eve_EventType, e.eve_IP, d.dev_Name
            FROM Events e
            LEFT JOIN Devices d ON e.eve_MAC = d.dev_MAC
            ORDER BY e.eve_DateTime DESC LIMIT 15';
    $results_array = array();
    $results = $db->query($sql);
    $i = 0;
    while ($row = $results->fetchArray()) {
        $results_array[$i]['eve_DateTime']  = $row['eve_DateTime'];
        $results_array[$i]['eve_EventType'] = $row['eve_EventType'];
        $results_array[$i]['eve_IP']        = $row['eve_IP'];
        $results_array[$i]['dev_Name']      = $row['dev_Name'] ? $row['dev_Name'] : 'Unknown';
        $i++;
    }
    echo json_encode($results_array);
    echo "\n";
}
?>
```

#### Step 4 — Verify the endpoints work

```bash
APIKEY="your-api-key-here"
HOST="your-pi-alert-ip"

curl -s -X POST -F "api-key=$APIKEY" -F "get=all-new"       http://$HOST/pialert/api/
curl -s -X POST -F "api-key=$APIKEY" -F "get=all-down"      http://$HOST/pialert/api/
curl -s -X POST -F "api-key=$APIKEY" -F "get=recent-events" http://$HOST/pialert/api/
```

Each should return a JSON array. `[]` means no devices in that category — that's correct.

---

## First-Time Setup (ESP32)

1. Flash the firmware using PlatformIO (`pio run --target upload`)
2. On first boot, the ESP32 broadcasts a setup access point:
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

Settings are stored in flash (NVS) and survive reboots. To re-enter setup, hold **BOOT for 3 seconds** at any time.

---

## Building with PlatformIO

### Dependencies (auto-installed)

- `moononournation/GFX Library for Arduino @ 1.4.7`
- `bblanchon/ArduinoJson @ ^7`
- `PaulStoffregen/XPT2046_Touchscreen` (via GitHub)

### Build & Upload

```bash
cd /path/to/CYDPiAlert
pio run --target upload
```

### Serial Monitor

```bash
pio device monitor --baud 115200
```

---

## Project Structure

```
CYDPiAlert/
├── platformio.ini              # PlatformIO config (board, libs)
├── pialert-patch/
│   └── index.php               # Modified Pi.Alert API file (drop-in replacement)
├── include/
│   ├── Portal.h                # Captive portal: WiFi + Pi.Alert credentials, NVS
│   └── PiAlert.h               # HTTP fetch functions + data structs for all modes
└── src/
    └── main.cpp                # Display init, mode logic, draw functions, touch + button
```

---

## Troubleshooting

| Error on screen | Cause | Fix |
|----------------|-------|-----|
| `ERR: Wrong API key` | API key doesn't match Pi.Alert | Re-enter setup, copy key from Pi.Alert → Maintenance → API Key |
| `Fetch failed: HTTP 404` | Wrong Pi.Alert host or path | Enter bare IP only, e.g. `192.168.0.105` |
| `Fetch failed: JSON error` | Missing API endpoint | Follow the Pi.Alert server modification steps above |
| `Fetch failed: No connection` | WiFi dropped | Retries automatically on next 30s interval |
| `WiFi failed: "YourSSID"` | Wrong SSID or password | Hold BOOT 3s to re-enter setup |
| Screen stays on "Refreshing..." | Pi.Alert scan still running | Wait — Pi.Alert scans run every 3–5 minutes |

---

## Notes

- Refresh interval is **30 seconds**. Pi.Alert's ARP scan runs every 3–5 minutes so dashboard counts only change that often.
- **Mode 3 — New Devices** shows devices Pi.Alert detected but you haven't acknowledged. Clear them in Pi.Alert → Devices.
- **Mode 4 — Down Devices** only shows devices that have `Alert when down` enabled in Pi.Alert device settings. Devices not flagged will appear in Mode 2 (Offline) instead.
- **Mode 5 — Recent Events** includes all event types: `Connected`, `Disconnected`, `VOIDED - Connected`, etc. `VOIDED` events are normal — Pi.Alert uses them to correct scan anomalies.
- Compatible with the original [Pi.Alert by pucherot](https://github.com/pucherot/Pi.Alert). Forks (Pi.Alert CE, IPAM) may have different API paths or database schemas.

---

## Related Project

**[CYDPiHole](../CYDPiHole)** — Displays live Pi-hole v6 DNS query data on the same CYD hardware with 5 modes: live query feed, stats summary, top blocked domains, top clients, and 24h activity graph.

