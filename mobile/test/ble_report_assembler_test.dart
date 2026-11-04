import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:rovo_mobile/services/ble_report_assembler.dart';

Uint8List frame(int id, int index, int total, List<int> payload) =>
    Uint8List.fromList([id >> 8, id & 255, index, total, ...payload]);

void main() {
  test('reassembles exact original bytes out of order', () {
    final assembler = BleReportAssembler();
    expect(assembler.add(frame(1, 2, 3, [5, 6])), isNull);
    expect(assembler.add(frame(1, 0, 3, [1, 2])), isNull);
    final complete = assembler.add(frame(1, 1, 3, [3, 4]));
    expect(complete!.transportId, 1);
    expect(complete.bytes, Uint8List.fromList([1, 2, 3, 4, 5, 6]));
  });

  test('accepts identical retry fragment only once', () {
    final assembler = BleReportAssembler();
    final a = frame(12, 0, 2, [1]);
    expect(assembler.add(a), isNull);
    expect(assembler.add(a), isNull);
    expect(assembler.add(frame(12, 1, 2, [2]))!.bytes, [1, 2]);
  });

  test('rejects conflicting retry and does not assemble it', () {
    final assembler = BleReportAssembler();
    assembler.add(frame(3, 0, 2, [1]));
    expect(() => assembler.add(frame(3, 0, 2, [99])),
      throwsFormatException);
    expect(assembler.add(frame(3, 1, 2, [2])), isNull);
  });

  test('reset on disconnect discards partial report', () {
    final assembler = BleReportAssembler();
    assembler.add(frame(2, 0, 2, [1]));
    assembler.reset();
    expect(assembler.add(frame(2, 1, 2, [2])), isNull);
    expect(assembler.add(frame(2, 0, 2, [1]))!.bytes, [1, 2]);
  });

  test('incomplete assembly expires', () {
    final assembler = BleReportAssembler();
    final t = DateTime.utc(2026);
    assembler.add(frame(1, 0, 2, [1]), now: t);
    expect(assembler.add(frame(1, 1, 2, [2]),
      now: t.add(const Duration(seconds: 16))), isNull);
  });

  test('rejects malformed and oversized payload', () {
    final assembler = BleReportAssembler();
    expect(() => assembler.add(Uint8List.fromList([0, 1, 0, 2])),
      throwsFormatException);
    expect(() => assembler.add(frame(1, 2, 2, [1])),
      throwsFormatException);
    expect(() => assembler.add(frame(1, 0, 0, [1])),
      throwsFormatException);
    expect(() => assembler.add(frame(1, 0, 2, [])),
      throwsFormatException);
    expect(() => assembler.add(frame(1, 0, 2, List.filled(241, 1))),
      throwsFormatException);
  });
}
