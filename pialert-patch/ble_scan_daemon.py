#!/usr/bin/env python3
"""ble_scan_daemon.py — BLE device scanner with card-skimmer detection.

Uses 'bluetoothctl' to scan for nearby BLE devices.
Writes /tmp/ble_devices.json every ~45 seconds (10s scan + 35s sleep).

Run as root or with Bluetooth permissions.
"""

import json, re, subprocess, time, datetime, os

SCAN_DURATION  = 10    # seconds per active BLE scan
SCAN_INTERVAL  = 35    # seconds to sleep between scan cycles
BLE_FILE       = '/tmp/ble_devices.json'
MAX_DEVICES    = 20    # keep only top-N by RSSI

# OUI prefixes (first 8 chars of MAC, lowercase) associated with cheap
# BLE modules commonly found in card skimmers and covert trackers.
SKIMMER_OUI = {
    'ac:23:3f',  # cheap BLE/WiFi combo modules
    'f8:1d:78',
    '00:06:66',  # HC-06 clone modules
    '20:16:04',
    '20:17:06',
}

# BLE device name fragments that raise suspicion (lowercase match)
SUSPICIOUS_NAMES = [
    'hc-0', 'hc-1', 'hc-05', 'hc-06',
    'ble_', 'skimmer', 'atm_', 'pos_',
    'linvor', 'dps_', 'bluetooth5',
]


# ---------------------------------------------------------------------------
# BLE scan via bluetoothctl
# ---------------------------------------------------------------------------
def scan_ble():
    """Open bluetoothctl, enable scan for SCAN_DURATION, collect devices."""
    devices = {}   # mac → {'name': str, 'rssi': int}

    try:
        proc = subprocess.Popen(
            ['bluetoothctl'],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )
        proc.stdin.write('scan on\n')
        proc.stdin.flush()

        deadline = time.time() + SCAN_DURATION
        while time.time() < deadline:
            try:
                line = proc.stdout.readline()
                if not line:
                    break

                # [NEW] Device AA:BB:CC:DD:EE:FF  Name
                m = re.search(r'\[NEW\] Device ([0-9A-Fa-f:]{17})\s+(.*)', line)
                if m:
                    mac  = m.group(1).lower()
                    name = m.group(2).strip()
                    if mac not in devices:
                        devices[mac] = {'name': name, 'rssi': -100}
                    elif name:
                        devices[mac]['name'] = name

                # [CHG] Device AA:BB:CC:DD:EE:FF RSSI: -75
                m = re.search(r'Device ([0-9A-Fa-f:]{17}).*RSSI:\s*([-\d]+)', line)
                if m:
                    mac  = m.group(1).lower()
                    rssi = int(m.group(2))
                    if mac not in devices:
                        devices[mac] = {'name': '', 'rssi': rssi}
                    else:
                        devices[mac]['rssi'] = rssi

            except Exception:
                break

        proc.stdin.write('scan off\n')
        proc.stdin.write('quit\n')
        proc.stdin.flush()
        proc.terminate()
        proc.wait(timeout=3)

    except FileNotFoundError:
        print('[ble_scan_daemon] bluetoothctl not found', flush=True)
    except Exception as e:
        print(f'[ble_scan_daemon] Scan error: {e}', flush=True)

    return devices


# ---------------------------------------------------------------------------
# Suspicion check
# ---------------------------------------------------------------------------
def flag_device(mac, name):
    flags = []
    oui   = mac[:8].lower()
    name_lower = name.lower()

    if oui in SKIMMER_OUI:
        flags.append('suspicious_oui')

    for frag in SUSPICIOUS_NAMES:
        if frag in name_lower:
            flags.append('suspicious_name')
            break

    if not name.strip():
        flags.append('unnamed')

    return flags


# ---------------------------------------------------------------------------
# Write JSON
# ---------------------------------------------------------------------------
def write_json(devices):
    ts = datetime.datetime.now().strftime('%H:%M:%S')

    device_list = []
    for mac, info in devices.items():
        flags = flag_device(mac, info['name'])
        suspicious = any(f in flags for f in ('suspicious_oui', 'suspicious_name'))
        device_list.append({
            'mac':        mac,
            'name':       info['name'] if info['name'].strip() else '<unknown>',
            'rssi':       info['rssi'],
            'suspicious': suspicious,
            'flags':      flags,
        })

    device_list.sort(key=lambda x: x['rssi'], reverse=True)
    device_list = device_list[:MAX_DEVICES]

    suspicious_count = sum(1 for d in device_list if d['suspicious'])

    data = {
        'device_count':     len(device_list),
        'suspicious_count': suspicious_count,
        'status':           'threat' if suspicious_count > 0 else 'clean',
        'devices':          device_list,
        'scan_time':        ts,
    }
    with open(BLE_FILE, 'w') as f:
        json.dump(data, f)

    return len(device_list), suspicious_count


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------
def main():
    print(f'[ble_scan_daemon] Starting: {SCAN_DURATION}s scan every '
          f'{SCAN_DURATION + SCAN_INTERVAL}s', flush=True)
    while True:
        try:
            devs = scan_ble()
            cnt, sus = write_json(devs)
            print(f'[ble_scan_daemon] {cnt} BLE devices, {sus} suspicious', flush=True)
        except Exception as e:
            print(f'[ble_scan_daemon] Error: {e}', flush=True)
        time.sleep(SCAN_INTERVAL)


if __name__ == '__main__':
    main()
