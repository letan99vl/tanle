from pathlib import Path
import sys

p = Path(sys.argv[1])
s = p.read_text(encoding='utf-8')

s = s.replace('import android.app.Activity;\n', 'import android.app.Activity;\nimport android.app.AlertDialog;\n')
s = s.replace(
    'import java.util.ArrayDeque;\nimport java.util.Queue;\nimport java.util.UUID;\n',
    'import java.util.ArrayDeque;\nimport java.util.ArrayList;\nimport java.util.LinkedHashMap;\nimport java.util.List;\nimport java.util.Queue;\nimport java.util.UUID;\n'
)
s = s.replace(
    '    private SharedPreferences prefs;\n',
    '    private SharedPreferences prefs;\n'
    '    private final LinkedHashMap<String, BluetoothDevice> pickerDevices = new LinkedHashMap<>();\n'
    '    private final LinkedHashMap<String, Integer> pickerRssi = new LinkedHashMap<>();\n'
    '    private boolean pickerScanActive = false;\n'
)

helper_marker = '    private void ensurePermissionsThenConnect(boolean auto) {'
helper_code = '''    private void ensureBluetoothAdapter() {
        if (bluetoothAdapter != null) return;
        try {
            BluetoothManager bm = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
            bluetoothAdapter = bm != null ? bm.getAdapter() : null;
        } catch (Throwable ignored) {
            bluetoothAdapter = null;
        }
    }

'''
if 'private void ensureBluetoothAdapter()' not in s:
    s = s.replace(helper_marker, helper_code + helper_marker)

old_connect = '''        String saved = prefs.getString("deviceAddress", null);\n        if (saved != null) {\n            try {\n                connecting = true;\n                connectDevice(bluetoothAdapter.getRemoteDevice(saved));\n                return;\n            } catch (Exception ignored) { }\n        }\n        if (!auto) startScan();\n'''
new_connect = '''        // v11: never auto-connect a previously used BLE device.\n        // Every manual press must show the picker again.\n        if (auto) {\n            try { prefs.edit().remove("deviceAddress").apply(); } catch (Exception ignored) { }\n            return;\n        }\n\n        try { prefs.edit().remove("deviceAddress").apply(); } catch (Exception ignored) { }\n        startDevicePickerScan();\n'''
if old_connect not in s:
    raise SystemExit('BLE picker patch: connect block not found')
s = s.replace(old_connect, new_connect)

start_marker = '    @SuppressLint("MissingPermission")\n    private void startScan() {'
end_marker = '\n    @SuppressLint("MissingPermission")\n    private void stopScan()'
start = s.index(start_marker)
end = s.index(end_marker, start)

picker_code = r'''    @SuppressLint("MissingPermission")
    private void startDevicePickerScan() {
        if (!hasBlePermissions()) return;
        ensureBluetoothAdapter();
        if (bluetoothAdapter == null) {
            jsConnectError("Điện thoại không hỗ trợ Bluetooth.");
            return;
        }
        scanner = bluetoothAdapter.getBluetoothLeScanner();
        if (scanner == null) {
            jsConnectError("Không mở được BLE scanner.");
            return;
        }

        stopScan();
        pickerDevices.clear();
        pickerRssi.clear();
        pickerScanActive = true;
        connecting = true;
        Toast.makeText(this, "Đang quét thiết bị BLE...", Toast.LENGTH_SHORT).show();

        scanCallback = new ScanCallback() {
            @Override public void onScanResult(int callbackType, ScanResult result) {
                addPickerResult(result);
            }

            @Override public void onBatchScanResults(List<ScanResult> results) {
                if (results == null) return;
                for (ScanResult result : results) addPickerResult(result);
            }

            @Override public void onScanFailed(int errorCode) {
                pickerScanActive = false;
                connecting = false;
                stopScan();
                jsConnectError("BLE scan lỗi: " + errorCode);
            }
        };

        try {
            scanner.startScan(scanCallback);
        } catch (Throwable t) {
            pickerScanActive = false;
            connecting = false;
            jsConnectError("Không bắt đầu được BLE scan: " + t.getMessage());
            return;
        }

        main.postDelayed(() -> {
            if (!pickerScanActive) return;
            pickerScanActive = false;
            stopScan();
            connecting = false;
            showDevicePicker();
        }, 3500);
    }

    @SuppressLint("MissingPermission")
    private void addPickerResult(ScanResult result) {
        if (result == null || !pickerScanActive) return;
        BluetoothDevice device = result.getDevice();
        if (device == null) return;
        String address;
        try { address = device.getAddress(); } catch (Throwable t) { return; }
        if (address == null || address.isEmpty()) return;
        pickerDevices.put(address, device);
        pickerRssi.put(address, result.getRssi());
    }

    @SuppressLint("MissingPermission")
    private void showDevicePicker() {
        if (isFinishing() || isDestroyed()) return;

        final ArrayList<BluetoothDevice> devices = new ArrayList<>(pickerDevices.values());
        devices.sort((a, b) -> {
            String an = rawDeviceName(a);
            String bn = rawDeviceName(b);
            boolean ap = an != null && an.startsWith(DEVICE_PREFIX);
            boolean bp = bn != null && bn.startsWith(DEVICE_PREFIX);
            if (ap != bp) return ap ? -1 : 1;
            int ar = pickerRssi.getOrDefault(safeAddress(a), -127);
            int br = pickerRssi.getOrDefault(safeAddress(b), -127);
            return Integer.compare(br, ar);
        });

        if (devices.isEmpty()) {
            new AlertDialog.Builder(this)
                    .setTitle("Không tìm thấy thiết bị BLE")
                    .setMessage("Hãy bật ESP32-S3 và kiểm tra nó đang phát tên BLINK-REDLEO, sau đó quét lại.")
                    .setPositiveButton("QUÉT LẠI", (d, w) -> startDevicePickerScan())
                    .setNegativeButton("HỦY", (d, w) -> jsConnectError("Đã hủy chọn thiết bị Bluetooth."))
                    .show();
            return;
        }

        int limit = Math.min(devices.size(), 30);
        final BluetoothDevice[] shown = new BluetoothDevice[limit];
        final String[] labels = new String[limit];
        for (int i = 0; i < limit; i++) {
            BluetoothDevice d = devices.get(i);
            shown[i] = d;
            String name = rawDeviceName(d);
            String address = safeAddress(d);
            int rssi = pickerRssi.getOrDefault(address, -127);
            boolean blink = name != null && name.startsWith(DEVICE_PREFIX);
            if (name == null || name.trim().isEmpty()) name = "Thiết bị BLE";
            labels[i] = (blink ? "★ " : "") + name + "\n" + address + "   " + rssi + " dBm";
        }

        new AlertDialog.Builder(this)
                .setTitle("Chọn thiết bị Bluetooth")
                .setItems(labels, (dialog, which) -> {
                    if (which < 0 || which >= shown.length) return;
                    BluetoothDevice chosen = shown[which];
                    connectedName = safeDeviceName(chosen);
                    connectDevice(chosen);
                })
                .setNeutralButton("QUÉT LẠI", (d, w) -> startDevicePickerScan())
                .setNegativeButton("HỦY", (d, w) -> jsConnectError("Đã hủy chọn thiết bị Bluetooth."))
                .show();
    }

    @SuppressLint("MissingPermission")
    private String rawDeviceName(BluetoothDevice d) {
        try { return d == null ? null : d.getName(); }
        catch (Throwable t) { return null; }
    }

    @SuppressLint("MissingPermission")
    private String safeAddress(BluetoothDevice d) {
        try { return d.getAddress() == null ? "--:--:--:--:--:--" : d.getAddress(); }
        catch (Throwable t) { return "--:--:--:--:--:--"; }
    }
'''

s = s[:start] + picker_code + s[end:]
p.write_text(s, encoding='utf-8')


# Disable all native saved-device reconnect paths and persistence.
s = s.replace('            main.postDelayed(this::tryAutoReconnect, 400);', '            // v11: manual BLE selection only; no auto reconnect after page load.')
s = s.replace('                if (!userDisconnect) main.postDelayed(MainActivity.this::tryAutoReconnect, 1800);', '                // v11: do not auto reconnect after BLE disconnect.')
s = s.replace('        main.postDelayed(this::tryAutoReconnect, 700);', '        // v11: do not auto reconnect when app resumes.')
s = s.replace('            try { prefs.edit().putString("deviceAddress", bg.getDevice().getAddress()).apply(); } catch (Exception ignored) { }',
              '            try { prefs.edit().remove("deviceAddress").apply(); } catch (Exception ignored) { }')
s = s.replace('            return prefs.contains("deviceAddress");', '            return false;')
p.write_text(s, encoding='utf-8')
