import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:rovo_mobile/services/ble_delivery_processor.dart';
import 'package:rovo_mobile/services/report_queue.dart';

Uint8List frame(int id, List<int> bytes) =>
    Uint8List.fromList([id >> 8, id & 255, 0, 1, ...bytes]);

void main() {
  test('durably stores BEFORE BLE acknowledgement', () async {
    final actions = <String>[];
    final processor = BleDeliveryProcessor(
      persist: (bytes) async {
        actions.add('stored');
        expect(bytes, [10, 20]);
        return StoreOutcome.stored;
      },
      ack: (id) async {
        expect(id, 55);
        actions.add('ack');
      },
    );
    expect(await processor.accept(frame(55, [10, 20])), isTrue);
    expect(actions, ['stored', 'ack']);
  });

  test('lost BLE ACK can be repeated for identical durable report', () async {
    var attempts = 0;
    final processor = BleDeliveryProcessor(
      persist: (_) async => StoreOutcome.alreadyStored,
      ack: (_) async {
        attempts++;
        if (attempts == 1) throw StateError('ACK write interrupted');
      },
    );
    await expectLater(processor.accept(frame(1, [10])), throwsStateError);
    expect(await processor.accept(frame(1, [10])), isTrue);
    expect(attempts, 2);
  });

  test('never ACK on full queue or ID conflict', () async {
    var acked = 0;
    var outcome = StoreOutcome.full;
    final processor = BleDeliveryProcessor(
      persist: (_) async => outcome,
      ack: (_) async => acked++,
    );
    expect(await processor.accept(frame(1, [5])), isFalse);
    outcome = StoreOutcome.conflict;
    expect(await processor.accept(frame(1, [5])), isFalse);
    expect(acked, 0);
  });

  test('never ACK on local persistence exception', () async {
    var acked = 0;
    final processor = BleDeliveryProcessor(
      persist: (_) async => throw StateError('disk full'),
      ack: (_) async => acked++,
    );
    await expectLater(processor.accept(frame(1, [5])), throwsStateError);
    expect(acked, 0);
  });
}
