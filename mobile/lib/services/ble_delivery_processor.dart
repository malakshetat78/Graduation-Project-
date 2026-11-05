import 'dart:typed_data';

import 'ble_report_assembler.dart';
import 'report_queue.dart';

/// Testable BLE -> durable SQLite -> ACK boundary.
/// The transport ACK is only sent for bytes successfully saved or already saved
/// identically. Any storage error, full queue, or ID conflict leaves ESP32 owner.
class BleDeliveryProcessor {
  BleDeliveryProcessor({required this.persist, required this.ack,
    BleReportAssembler? assembler}) : assembler = assembler ?? BleReportAssembler();

  final Future<StoreOutcome> Function(Uint8List) persist;
  final Future<void> Function(int transportId) ack;
  final BleReportAssembler assembler;

  void reset() => assembler.reset();

  Future<bool> accept(Uint8List frame) async {
    final report = assembler.add(frame);
    if (report == null) return false;
    final outcome = await persist(report.bytes);
    if (outcome != StoreOutcome.stored &&
        outcome != StoreOutcome.alreadyStored) {
      return false;
    }
    await ack(report.transportId);
    return true;
  }
}
