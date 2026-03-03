<?php
// Check API Key
// Print Api-Key for debugging
// echo $_POST['api-key'];
$config_file = "../../config/pialert.conf";
$config_file_lines = file($config_file);
$config_file_lines_bypass = array_values(preg_grep('/^PIALERT_APIKEY\s.*/', $config_file_lines));
if ($config_file_lines_bypass != False) {
	$apikey_line = explode("'", $config_file_lines_bypass[0]);
	$pia_apikey = trim($apikey_line[1]);
} else {echo "No API-Key is set\n";exit;}

// Exit if API-Key is unequal
if ($_REQUEST['api-key'] != $pia_apikey) {
	echo "Wrong API-Key\n";
	exit;
}

// When API is correct
// include db.php
require '../php/server/db.php';
// Overwrite variable from db.php because of current working dir
$DBFILE = '../../db/pialert.db';

// Set maximum execution time to 30 seconds
ini_set('max_execution_time', '30');

// Secure and verify query
if (isset($_REQUEST['mac'])) {
	$mac_address = str_replace('-', ':', strtolower($_REQUEST['mac']));
	if (filter_var($mac_address, FILTER_VALIDATE_MAC) === False) {echo 'Invalid MAC Address.';exit;}
}

// Open DB
OpenDB();

// Action functions
if (isset($_REQUEST['get']) && !empty($_REQUEST['get'])) {
	$action = $_REQUEST['get'];
	switch ($action) {
	case 'mac-status':getStatusofMAC($mac_address);
		break;
	case 'all-online':getAllOnline();
		break;
	case 'all-offline':getAllOffline();
		break;
	case 'system-status':getSystemStatus();
		break;
	case 'all-online-icmp':getAllOnline_ICMP();
		break;
	case 'all-offline-icmp':getAllOffline_ICMP();
		break;
	case 'all-new':getAllNew();
		break;
	case 'all-down':getAllDown();
		break;
	case 'recent-events':getRecentEvents();
		break;
	case 'ip-changes':getIPChanges();
		break;
	case 'online-uptime':getOnlineUptime();
		break;
	}
}

//example curl -k -X POST -F 'api-key=key' -F 'get=system-status' https://url/pialert/api/
function getSystemStatus() {
	# Detect Language
	foreach (glob("../../config/setting_language*") as $filename) {
		$pia_lang_selected = str_replace('setting_language_', '', basename($filename));
	}
	if (strlen($pia_lang_selected) == 0) {$pia_lang_selected = 'en_us';}
	$en_us = array("On", "Off");
	$de_de = array("Ein", "Aus");
	$es_es = array("En", "Off");
	$fr_fr = array("Allumé", "Éteint");
	$it_it = array("Acceso", "Spento");

	# Check Scanning Status
	if (file_exists("../../db/setting_stoparpscan")) {$temp_api_online_devices['Scanning'] = $$pia_lang_selected[1];} else { $temp_api_online_devices['Scanning'] = $$pia_lang_selected[0];}

	global $db;
	$results = $db->query('SELECT * FROM Online_History WHERE data_source="main_scan_local" ORDER BY Scan_Date DESC LIMIT 1');
	while ($row = $results->fetchArray()) {
		$time_raw = explode(' ', $row['Scan_Date']);
		$temp_api_online_devices['Last_Scan'] = $time_raw[1];
	}
	unset($results);
	$result = $db->query(
		'SELECT
        (SELECT COUNT(*) FROM Devices WHERE dev_Archived=0) as All_Devices,
        (SELECT COUNT(*) FROM Devices WHERE dev_Archived=0 AND dev_PresentLastScan=1) as Online_Devices,
        (SELECT COUNT(*) FROM Devices WHERE dev_Archived=0 AND dev_NewDevice=1) as New_Devices,
        (SELECT COUNT(*) FROM Devices WHERE dev_Archived=0 AND dev_AlertDeviceDown=1 AND dev_PresentLastScan=0) as Down_Devices,
        (SELECT COUNT(*) FROM Devices WHERE dev_Archived=0 AND dev_AlertDeviceDown=0 AND dev_PresentLastScan=0) as Offline_Devices,
        (SELECT COUNT(*) FROM Devices WHERE dev_Archived=1) as Archived_Devices
   ');
	$row = $result->fetchArray(SQLITE3_NUM);
	$temp_api_online_devices['All_Devices'] = $row[0];
	$temp_api_online_devices['Online_Devices'] = $row[1];
	$temp_api_online_devices['New_Devices'] = $row[2];
	$temp_api_online_devices['Down_Devices'] = $row[3];
	$temp_api_online_devices['Offline_Devices'] = $row[4];
	$temp_api_online_devices['Archived_Devices'] = $row[5];
	unset($results);

	$result = $db->query(
		"SELECT
        (SELECT COUNT(*) FROM Devices WHERE dev_ScanSource='local' AND dev_Archived=0) as All_Devices,
        (SELECT COUNT(*) FROM Devices WHERE dev_ScanSource='local' AND dev_Archived=0 AND dev_PresentLastScan=1) as Online_Devices,
        (SELECT COUNT(*) FROM Devices WHERE dev_ScanSource='local' AND dev_Archived=0 AND dev_NewDevice=1) as New_Devices,
        (SELECT COUNT(*) FROM Devices WHERE dev_ScanSource='local' AND dev_Archived=0 AND dev_AlertDeviceDown=1 AND dev_PresentLastScan=0) as Down_Devices,
        (SELECT COUNT(*) FROM Devices WHERE dev_ScanSource='local' AND dev_Archived=0 AND dev_AlertDeviceDown=0 AND dev_PresentLastScan=0) as Offline_Devices,
        (SELECT COUNT(*) FROM Devices WHERE dev_ScanSource='local' AND dev_Archived=1) as Archived_Devices
   ");
	$subrow = $result->fetchArray(SQLITE3_NUM);
	$temp_api_online_devices['local']['All_Devices'] = $subrow[0];
	$temp_api_online_devices['local']['Online_Devices'] = $subrow[1];
	$temp_api_online_devices['local']['New_Devices'] = $subrow[2];
	$temp_api_online_devices['local']['Down_Devices'] = $subrow[3];
	$temp_api_online_devices['local']['Offline_Devices'] = $subrow[4];
	$temp_api_online_devices['local']['Archived_Devices'] = $subrow[5];
	unset($result);
	$results = $db->query('SELECT * FROM Satellites');
	while ($row = $results->fetchArray()) {
		$sat_token = $row['sat_token'];
		$sat_name = $row['sat_name'];

		$result = $db->query(
			"SELECT
	        (SELECT COUNT(*) FROM Devices WHERE dev_ScanSource='".$row['sat_token']."' AND dev_Archived=0) as All_Devices,
	        (SELECT COUNT(*) FROM Devices WHERE dev_ScanSource='".$row['sat_token']."' AND dev_Archived=0 AND dev_PresentLastScan=1) as Online_Devices,
	        (SELECT COUNT(*) FROM Devices WHERE dev_ScanSource='".$row['sat_token']."' AND dev_Archived=0 AND dev_NewDevice=1) as New_Devices,
	        (SELECT COUNT(*) FROM Devices WHERE dev_ScanSource='".$row['sat_token']."' AND dev_Archived=0 AND dev_AlertDeviceDown=1 AND dev_PresentLastScan=0) as Down_Devices,
	        (SELECT COUNT(*) FROM Devices WHERE dev_ScanSource='".$row['sat_token']."' AND dev_Archived=0 AND dev_AlertDeviceDown=0 AND dev_PresentLastScan=0) as Offline_Devices,
	        (SELECT COUNT(*) FROM Devices WHERE dev_ScanSource='".$row['sat_token']."' AND dev_Archived=1) as Archived_Devices
	   ");
		$subrow = $result->fetchArray(SQLITE3_NUM);
		$temp_api_online_devices[$sat_name]['All_Devices'] = $subrow[0];
		$temp_api_online_devices[$sat_name]['Online_Devices'] = $subrow[1];
		$temp_api_online_devices[$sat_name]['New_Devices'] = $subrow[2];
		$temp_api_online_devices[$sat_name]['Down_Devices'] = $subrow[3];
		$temp_api_online_devices[$sat_name]['Offline_Devices'] = $subrow[4];
		$temp_api_online_devices[$sat_name]['Archived_Devices'] = $subrow[5];
		unset($result);
	}
	unset($results);
	$results = $db->query('SELECT * FROM Online_History WHERE data_source="icmp_scan" ORDER BY Scan_Date DESC LIMIT 1');
	while ($row = $results->fetchArray()) {
		$temp_api_online_devices['All_Devices_ICMP'] = $row['All_Devices'];
		$temp_api_online_devices['Offline_Devices_ICMP'] = $row['Down_Devices'];
		$temp_api_online_devices['Online_Devices_ICMP'] = $row['Online_Devices'];
	}
	unset($results);
	$results = $db->query('SELECT * FROM Online_History WHERE data_source="icmp_scan" ORDER BY Scan_Date DESC LIMIT 1');
	while ($row = $results->fetchArray()) {
		$temp_api_online_devices['All_Devices_ICMP'] = $row['All_Devices'];
		$temp_api_online_devices['Offline_Devices_ICMP'] = $row['Down_Devices'];
		$temp_api_online_devices['Online_Devices_ICMP'] = $row['Online_Devices'];
	}
	unset($results);
	$result = $db->query('SELECT COUNT(*) as count FROM Services');
	$row = $result->fetchArray(SQLITE3_ASSOC);
	if ($row) {
		$temp_api_online_devices['All_Services'] = $row['count'];
	}
	$api_online_devices = $temp_api_online_devices;
	$json = json_encode($api_online_devices);
	echo $json;
	echo "\n";
}

//example curl -k -X POST -F 'api-key=key' -F 'get=mac-status' -F 'mac=dc:a6:32:23:06:d3' https://url/pialert/api/
function getStatusofMAC($query_mac) {
	global $db;
	$sql = 'SELECT * FROM Devices WHERE dev_MAC="' . $query_mac . '"';
	$result = $db->query($sql);
	$row = $result->fetchArray(SQLITE3_ASSOC);
	$json = json_encode($row);
	echo $json;
	echo "\n";
}

//example curl -k -X POST -F 'api-key=key' -F 'get=all-online' https://url/pialert/api/
function getAllOnline() {
	global $db;
	$sql = 'SELECT * FROM Devices WHERE dev_PresentLastScan="1" ORDER BY dev_LastConnection DESC';
	$api_online_devices = array();
	$results = $db->query($sql);
	$i = 0;
	while ($row = $results->fetchArray()) {
		$temp_api_online_devices['dev_MAC'] = $row['dev_MAC'];
		$temp_api_online_devices['dev_Name'] = $row['dev_Name'];
		$temp_api_online_devices['dev_Vendor'] = $row['dev_Vendor'];
		$temp_api_online_devices['dev_LastIP'] = $row['dev_LastIP'];
		$temp_api_online_devices['dev_Infrastructure'] = $row['dev_Infrastructure'];
		$temp_api_online_devices['dev_Infrastructure_port'] = $row['dev_Infrastructure_port'];
		$api_online_devices[$i] = $temp_api_online_devices;
		$i++;
	}
	$json = json_encode($api_online_devices);
	echo $json;
	echo "\n";
}

//example curl -k -X POST -F 'api-key=key' -F 'get=all-offline' https://url/pialert/api/
function getAllOffline() {
	global $db;
	$sql = 'SELECT * FROM Devices WHERE dev_PresentLastScan="0"';
	$api_online_devices = array();
	$results = $db->query($sql);
	$i = 0;
	while ($row = $results->fetchArray()) {
		$temp_api_online_devices['dev_MAC'] = $row['dev_MAC'];
		$temp_api_online_devices['dev_Name'] = $row['dev_Name'];
		$temp_api_online_devices['dev_Vendor'] = $row['dev_Vendor'];
		$temp_api_online_devices['dev_LastIP'] = $row['dev_LastIP'];
		$temp_api_online_devices['dev_Infrastructure'] = $row['dev_Infrastructure'];
		$temp_api_online_devices['dev_Infrastructure_port'] = $row['dev_Infrastructure_port'];
		$api_online_devices[$i] = $temp_api_online_devices;
		$i++;
	}
	$json = json_encode($api_online_devices);
	echo $json;
	echo "\n";
}

//example curl -k -X POST -F 'api-key=key' -F 'get=all-online-icmp' https://url/pialert/api/
function getAllOnline_ICMP() {
	global $db;
	$sql = 'SELECT * FROM ICMP_Mon WHERE icmp_PresentLastScan="1"';
	$api_online_devices = array();
	$results = $db->query($sql);
	$i = 0;
	while ($row = $results->fetchArray()) {
		$temp_api_online_devices['icmp_ip'] = $row['icmp_ip'];
		$temp_api_online_devices['icmp_hostname'] = $row['icmp_hostname'];
		$temp_api_online_devices['icmp_avgrtt'] = $row['icmp_avgrtt'];
		$api_online_devices[$i] = $temp_api_online_devices;
		$i++;
	}
	$json = json_encode($api_online_devices);
	echo $json;
	echo "\n";
}

//example curl -k -X POST -F 'api-key=key' -F 'get=all-offline-icmp' https://url/pialert/api/
function getAllOffline_ICMP() {
	global $db;
	$sql = 'SELECT * FROM ICMP_Mon WHERE icmp_PresentLastScan="0"';
	$api_online_devices = array();
	$results = $db->query($sql);
	$i = 0;
	while ($row = $results->fetchArray()) {
		$temp_api_online_devices['icmp_ip'] = $row['icmp_ip'];
		$temp_api_online_devices['icmp_hostname'] = $row['icmp_hostname'];
		$api_online_devices[$i] = $temp_api_online_devices;
		$i++;
	}
	$json = json_encode($api_online_devices);
	echo $json;
	echo "\n";
}

function getAllNew() {
	global $db;
	$sql = 'SELECT dev_MAC, dev_Name, dev_Vendor, dev_LastIP, dev_FirstConnection FROM Devices WHERE dev_NewDevice="1" ORDER BY dev_FirstConnection DESC LIMIT 20';
	$results = $db->query($sql);
	$devices = array();
	$i = 0;
	while ($row = $results->fetchArray()) {
		$devices[$i] = array('dev_MAC'=>$row['dev_MAC'],'dev_Name'=>$row['dev_Name'],'dev_Vendor'=>$row['dev_Vendor'],'dev_LastIP'=>$row['dev_LastIP'],'dev_FirstConnection'=>$row['dev_FirstConnection']);
		$i++;
	}
	echo json_encode($devices);
	echo "
";
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
	        WHERE e.eve_EventType NOT LIKE "VOIDED%"
	        ORDER BY e.eve_DateTime DESC LIMIT 20';
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

// Returns online devices sorted newest-connection-first with server-computed
// uptime in minutes, so the ESP32 doesn't need its own clock.
function getOnlineUptime() {
	global $db;
	$now = time();
	$sql = 'SELECT dev_LastIP, dev_Name, dev_LastConnection
	        FROM Devices WHERE dev_PresentLastScan=1
	        ORDER BY dev_LastConnection DESC LIMIT 40';
	$results = $db->query($sql);
	$out = array();
	$i = 0;
	while ($row = $results->fetchArray()) {
		$conn_time = strtotime($row['dev_LastConnection']);
		$minutes = ($conn_time > 0) ? (int)floor(($now - $conn_time) / 60) : 0;
		$out[$i]['dev_LastIP'] = $row['dev_LastIP'];
		$out[$i]['dev_Name']   = $row['dev_Name'];
		$out[$i]['minutes']    = $minutes;
		$i++;
	}
	echo json_encode($out);
	echo "\n";
}

// Returns the 20 most recently seen (MAC, IP) pairs so you can track which
// MAC addresses have been using which IP addresses over time.
function getIPChanges() {
	global $db;
	$sql = 'SELECT e.eve_MAC, COALESCE(d.dev_Name, "Unknown") as dev_Name,
	               e.eve_IP, MAX(e.eve_DateTime) as last_seen
	        FROM Events e
	        LEFT JOIN Devices d ON e.eve_MAC = d.dev_MAC
	        WHERE e.eve_IP != "" AND e.eve_IP IS NOT NULL
	        GROUP BY e.eve_MAC, e.eve_IP
	        ORDER BY last_seen DESC
	        LIMIT 20';
	$results = $db->query($sql);
	$out = array();
	$i = 0;
	while ($row = $results->fetchArray()) {
		$out[$i]['eve_MAC']   = $row['eve_MAC'];
		$out[$i]['dev_Name']  = $row['dev_Name'];
		$out[$i]['eve_IP']    = $row['eve_IP'];
		$out[$i]['last_seen'] = $row['last_seen'];
		$i++;
	}
	echo json_encode($out);
	echo "\n";
}
?>