import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import 'ble_delivery_processor.dart';
import 'report_queue.dart';
import 'report_uploader.dart';

/// BLE central for the ICVSP bench transport. No secure pairing/ownership
/// handshake exists in firmware yet: not ready for field deployment.
class BleService extends ChangeNotifier {
  BleService._();
  static final BleService instance = BleService._();

  static final Guid serviceId = Guid('7b100001-6a34-4c82-9b51-2f048e9a6300');
  static final Guid dataId = Guid('7b100002-6a34-4c82-9b51-2f048e9a6300');
  static final Guid ackId = Guid('7b100003-6a34-4c82-9b51-2f048e9a6300');

  late final BleDeliveryProcessor _processor = BleDeliveryProcessor(
    persist: ReportQueue.instance.store,
    ack: (id) async {
      final characteristic = _ack;
      if (!connected || characteristic == null) {
        throw StateError('BLE disconnected before ACK');
      }
      await characteristic.write([(id >> 8) & 255, id & 255], withoutResponse: false);
      ReportUploader.instance.kick();
    },
  );
  StreamSubscription<List<ScanResult>>? _scanSubscription;
  StreamSubscription<List<int>>? _dataSubscription;
  StreamSubscription<BluetoothConnectionState>? _connectionSubscription;
  BluetoothDevice? _device;
  BluetoothCharacteristic? _ack;
  Future<void> _serial = Future.value();

  bool connecting = false;
  bool connected = false;
  String? lastProblem;

  Future<void> connect() async {
    if (connecting || connected) return;
    connecting = true;
    lastProblem = null;
    notifyListeners();
    try {
      if (!await FlutterBluePlus.isSupported) {
        throw StateError('BLE is not available on this phone');
      }
      await FlutterBluePlus.adapterState
          .where((state) => state == BluetoothAdapterState.on)
          .first.timeout(const Duration(seconds: 12));

      final found = Completer<BluetoothDevice>();
      _scanSubscription = FlutterBluePlus.onScanResults.listen((results) {
        for (final result in results) {
          if (result.advertisementData.serviceUuids.contains(serviceId) &&
              !found.isCompleted) {
            found.complete(result.device);
            break;
          }
        }
      });
      await FlutterBluePlus.startScan(
        withServices: [serviceId], timeout: const Duration(seconds: 12));
      final device = await found.future.timeout(const Duration(seconds: 13));
      await FlutterBluePlus.stopScan();
      await _scanSubscription?.cancel();
      _scanSubscription = null;

      _device = device;
      await device.connect(timeout: const Duration(seconds: 12));
      _connectionSubscription = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          connected = false;
          _ack = null;
          _processor.reset();
          notifyListeners();
        }
      });
      final services = await device.discoverServices();
      final service = services.where((s) => s.uuid == serviceId).firstOrNull;
      if (service == null) throw StateError('ICVSP service not found');
      final data = service.characteristics.where((c) => c.uuid == dataId).firstOrNull;
      _ack = service.characteristics.where((c) => c.uuid == ackId).firstOrNull;
      if (data == null || _ack == null) {
        throw StateError('ICVSP report/ACK characteristics missing');
      }
      if (!data.properties.notify || !_ack!.properties.write) {
        throw StateError('Device BLE characteristics do not match protocol');
      }
      _processor.reset();
      _dataSubscription = data.onValueReceived.listen((value) {
        // Serialize SQLite commits and ACK writes: never ACK out of order.
        _serial = _serial.then((_) => _handleFragment(Uint8List.fromList(value)))
            .catchError((Object error, StackTrace stack) {
          lastProblem = 'BLE report rejected: $error';
          notifyListeners();
        });
      });
      await data.setNotifyValue(true);
      connected = true;
    } catch (error) {
      lastProblem = 'Bluetooth connection failed: $error';
      await disconnect();
    } finally {
      connecting = false;
      notifyListeners();
    }
  }

  Future<void> _handleFragment(Uint8List frame) async {
    try {
      await _processor.accept(frame);
    } on Object catch (error) {
      // Never ACK invalid fragments, conflicted IDs or failed SQLite writes.
      lastProblem = 'BLE report was not acknowledged: $error';
      notifyListeners();
    }
  }

  Future<void> disconnect() async {
    try {
      await FlutterBluePlus.stopScan();
    } on Exception {
      // Ignore scan stop if there was no active scan.
    }
    await _scanSubscription?.cancel();
    _scanSubscription = null;
    await _dataSubscription?.cancel();
    _dataSubscription = null;
    await _connectionSubscription?.cancel();
    _connectionSubscription = null;
    _processor.reset();
    final device = _device;
    _device = null;
    _ack = null;
    connected = false;
    if (device != null) {
      try {
        await device.disconnect();
      } on Exception {
        // Device might already be gone.
      }
    }
    notifyListeners();
  }
}
