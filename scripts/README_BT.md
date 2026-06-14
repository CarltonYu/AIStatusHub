# macOS Bluetooth helper for AIStatusHub

`macos_bt_probe.swift` is a tiny CoreBluetooth scanner/connector for finding and
talking to the MX-01PS (or similar BLE-SPP) module from macOS.

## Build

```bash
cd /Users/carlton/Documents/code/self/AIStatusHub
swiftc scripts/macos_bt_probe.swift -o scripts/macos_bt_probe \
  -Xlinker -sectcreate -Xlinker __TEXT -Xlinker __info_plist -Xlinker /tmp/bt_info.plist
```

A pre-built binary is already at `scripts/macos_bt_probe`.

## Scan only (safe)

```bash
./scripts/macos_bt_probe
```

Default filter is `MX-01`. To look for another advertised name:

```bash
BT_TARGET="AIStatusHub-BT" ./scripts/macos_bt_probe
```

## Connect and send one command

```bash
BT_TARGET="AIStatusHub-BT" BT_CONNECT=1 BT_CMD="default happy\n" ./scripts/macos_bt_probe
```

The tool tries Nordic UART TX first, then any writable characteristic.

## Notes

- The tool uses BLE scanning. If the module is a dual-mode SPP+BLE module but
  is not advertising BLE, it will not appear.
- macOS will ask for Bluetooth permission the first time. Grant it to the
  terminal/shell process.
- Use the Duo-side `/root/configure-bt-module.py` to force the module to
  advertise a known name and use 9600 baud.
