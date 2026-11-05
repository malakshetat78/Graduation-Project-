import 'dart:typed_data';

/// ICVSP BLE frame: [transport_id BE(2), fragment_index(1), count(1), payload].
/// Transport IDs are scoped to a BLE link session, NOT backend report IDs.
class CompleteBleReport {
  const CompleteBleReport(this.transportId, this.bytes);
  final int transportId;
  final Uint8List bytes;
}

class BleReportAssembler {
  BleReportAssembler({this.timeout = const Duration(seconds: 15)});

  static const maxReportBytes = 2048;
  static const maxFragments = 128; // 2048 / 16 at MTU 23
  final Duration timeout;
  int? _transportId;
  int? _total;
  final Map<int, Uint8List> _pieces = {};
  DateTime? _started;

  void reset() {
    _transportId = null;
    _total = null;
    _started = null;
    _pieces.clear();
  }

  CompleteBleReport? add(Uint8List frame, {DateTime? now}) {
    now ??= DateTime.now();
    if (frame.length < 5) throw const FormatException('Short BLE frame');
    final id = (frame[0] << 8) | frame[1];
    final index = frame[2], count = frame[3];
    if (id == 0 || count == 0 || count > maxFragments || index >= count) {
      throw const FormatException('Invalid BLE frame header');
    }
    final part = Uint8List.sublistView(frame, 4);
    if (part.isEmpty || part.length > 240) {
      throw const FormatException('Invalid fragment payload');
    }
    if (_started != null && now.difference(_started!) > timeout) reset();
    if (_transportId != id) {
      // Stop-and-wait peripheral: only one report in flight. A new ID
      // invalidates any incomplete report, without acknowledging it.
      reset();
      _transportId = id;
      _total = count;
      _started = now;
    }
    if (_total != count) {
      reset();
      throw const FormatException('Changed BLE fragment count');
    }
    final old = _pieces[index];
    if (old != null) {
      if (!_same(old, part)) {
        reset();
        throw const FormatException('Conflicting duplicate fragment');
      }
      return null;
    }
    _pieces[index] = Uint8List.fromList(part);
    final bytesSoFar = _pieces.values.fold<int>(0, (sum, p) => sum + p.length);
    if (bytesSoFar > maxReportBytes) {
      reset();
      throw const FormatException('Oversize signed report');
    }
    if (_pieces.length != count) return null;
    final out = BytesBuilder(copy: false);
    for (var i = 0; i < count; i++) {
      final piece = _pieces[i];
      if (piece == null) return null;
      out.add(piece);
    }
    final report = CompleteBleReport(id, out.takeBytes());
    reset();
    return report;
  }

  bool _same(Uint8List a, Uint8List b) {
    if (a.length != b.length) return false;
    for (var i = 0; i < a.length; i++) {
      if (a[i] != b[i]) return false;
    }
    return true;
  }
}
