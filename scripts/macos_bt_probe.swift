#!/usr/bin/env swift
import Foundation
import CoreBluetooth

// Simple BLE probe for MX-01PS / similar BLE-SPP modules.
// Scans for 10 s, prints discovered peripherals, then optionally connects
// to the first one whose name contains TARGET_NAME and writes a test string
// to a Nordic UART-like TX characteristic if found.

let TARGET_NAME = ProcessInfo.processInfo.environment["BT_TARGET"] ?? "MX-01"
let WRITE_TEXT  = ProcessInfo.processInfo.environment["BT_CMD"]    ?? "default happy\n"
let SCAN_SECONDS = 10.0
let CONNECT_MODE = ProcessInfo.processInfo.environment["BT_CONNECT"] ?? "0"

class Probe: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var cm: CBCentralManager!
    var target: CBPeripheral?
    var done = false
    var connectTimer: Timer?

    override init() {
        super.init()
        cm = CBCentralManager(delegate: self, queue: nil)
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn {
            print("[BLE] powered on, scanning for \(SCAN_SECONDS) s ...")
            central.scanForPeripherals(withServices: nil,
                                       options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
            Timer.scheduledTimer(withTimeInterval: SCAN_SECONDS, repeats: false) { _ in
                self.finishScan()
            }
        } else {
            print("[BLE] adapter unavailable (state=\(central.state.rawValue))")
            exit(1)
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {
        let name = peripheral.name ?? ""
        let uuids = advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []
        print("[BLE] found: name='\(name)' id=\(peripheral.identifier) rssi=\(RSSI) uuids=\(uuids.map { $0.uuidString })")
        if target == nil && !TARGET_NAME.isEmpty && name.localizedCaseInsensitiveContains(TARGET_NAME) {
            target = peripheral
            print("[BLE] selected target: \(name)")
        }
    }

    func finishScan() {
        cm.stopScan()
        guard let t = target else {
            print("[BLE] no target matched '\(TARGET_NAME)'. Set BT_TARGET to change filter.")
            print("[BLE] to connect, set BT_CONNECT=1")
            exit(0)
        }
        if CONNECT_MODE != "1" {
            print("[BLE] target matched '\(t.name ?? "")' but BT_CONNECT != 1, not connecting.")
            print("[BLE] to connect and write, run with BT_CONNECT=1")
            exit(0)
        }
        print("[BLE] connecting to \(t.name ?? "") ...")
        t.delegate = self
        cm.connect(t, options: nil)
        connectTimer = Timer.scheduledTimer(withTimeInterval: 10.0, repeats: false) { _ in
            print("[BLE] connect timeout")
            exit(1)
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        connectTimer?.invalidate()
        print("[BLE] connected, discovering services ...")
        peripheral.discoverServices(nil)
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        print("[BLE] failed to connect: \(error?.localizedDescription ?? "unknown")")
        exit(1)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let e = error { print("[BLE] service discovery error: \(e)"); exit(1) }
        guard let services = peripheral.services else { return }
        for s in services {
            print("[BLE] service: \(s.uuid)")
            peripheral.discoverCharacteristics(nil, for: s)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let e = error { print("[BLE] char discovery error: \(e)"); return }
        guard let chars = service.characteristics else { return }
        for c in chars {
            let props = c.properties
            print("[BLE]   char: \(c.uuid) properties=\(props.rawValue) (write=\(props.contains(.write)) writeWithoutResponse=\(props.contains(.writeWithoutResponse)) notify=\(props.contains(.notify)))")
        }

        // Try Nordic UART TX characteristic first.
        let nordicTx = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
        if let tx = chars.first(where: { $0.uuid == nordicTx &&
            ($0.properties.contains(.write) || $0.properties.contains(.writeWithoutResponse)) }) {
            send(to: peripheral, characteristic: tx, text: WRITE_TEXT)
            return
        }
        // Fallback: any writable characteristic.
        if let tx = chars.first(where: { $0.properties.contains(.write) || $0.properties.contains(.writeWithoutResponse) }) {
            send(to: peripheral, characteristic: tx, text: WRITE_TEXT)
            return
        }
    }

    func send(to peripheral: CBPeripheral, characteristic: CBCharacteristic, text: String) {
        guard let data = text.data(using: .utf8) else { return }
        print("[BLE] writing '\(text.trimmingCharacters(in: .whitespacesAndNewlines))' to \(characteristic.uuid)")
        let type: CBCharacteristicWriteType = characteristic.properties.contains(.writeWithoutResponse) ? .withoutResponse : .withResponse
        peripheral.writeValue(data, for: characteristic, type: type)
        if type == .withoutResponse {
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                print("[BLE] write queued (withoutResponse)")
                exit(0)
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if let e = error {
            print("[BLE] write error: \(e)")
            exit(1)
        } else {
            print("[BLE] write succeeded")
            exit(0)
        }
    }
}

let probe = Probe()
RunLoop.main.run()
