import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:path/path.dart' as p;
import 'package:sqflite/sqflite.dart';

enum StoreOutcome { stored, alreadyStored, conflict, full }

class QueuedReport {
  const QueuedReport({required this.id, required this.line});
  final int id;
  final Uint8List line;
}

class ReportSummary {
  const ReportSummary({required this.eventId, required this.type,
    required this.state, required this.attempts, required this.lastResult});
  final String eventId, type, state;
  final int attempts;
  final String? lastResult;
}

class ReportQueue extends ChangeNotifier {
  ReportQueue._();
  @visibleForTesting
  ReportQueue.withDatabaseForTesting(Database db) { _database = Future.value(db); }
  static final ReportQueue instance = ReportQueue._();

  static const stateQueued = 'queued';
  static const stateSent = 'sent';
  static const stateRejected = 'rejected';
  static const _maxQueued = 1000;
  static const _potholeLifetime = Duration(hours: 47);
  static const _speedBumpLifetime = Duration(days: 13);
  static const _historyLifetime = Duration(days: 7);
  Future<Database>? _database;

  Future<Database> get _db => _database ??= _open();
  Future<Database> _open() async {
    final path = p.join(await getDatabasesPath(), 'rovo_reports.db');
    return openDatabase(path, version: 2, onCreate: (db, _) async {
      await db.execute('''
        CREATE TABLE reports (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          device_id TEXT NOT NULL, event_id TEXT NOT NULL,
          type TEXT NOT NULL, line BLOB NOT NULL,
          state TEXT NOT NULL, attempts INTEGER NOT NULL DEFAULT 0,
          last_result TEXT, created_at INTEGER NOT NULL,
          expires_at INTEGER NOT NULL, retry_at INTEGER NOT NULL DEFAULT 0,
          UNIQUE (device_id, event_id)
        )
      ''');
    }, onUpgrade: (db, oldVersion, _) async {
      if (oldVersion < 2) {
        await db.execute(
          'ALTER TABLE reports ADD COLUMN retry_at INTEGER NOT NULL DEFAULT 0');
      }
    });
  }

  /// Never mutate/re-encode signed bytes. A matching durable duplicate
  /// is ACK-safe; an ID conflict or full queue is NOT ACK-safe.
  Future<StoreOutcome> store(Uint8List raw) async {
    final parsed = jsonDecode(utf8.decode(raw));
    if (parsed is! Map<String, dynamic>) {
      throw const FormatException('Invalid signed envelope');
    }
    final event = parsed['event'];
    if (event is! Map<String, dynamic>) {
      throw const FormatException('Invalid signed event');
    }
    final sender = parsed['sender'];
    final deviceId = event['device_id'];
    final eventId = event['event_id'];
    final type = event['type'];
    final detectedAt = event['detected_at'];
    if (sender is! String || deviceId is! String || sender != deviceId ||
        eventId is! String || type is! String || detectedAt is! String) {
      throw const FormatException('Invalid signed report fields');
    }
    final lifetime = type == 'speed_bump' ? _speedBumpLifetime : _potholeLifetime;
    final expires = DateTime.parse(detectedAt).add(lifetime);
    final db = await _db;
    final outcome = await db.transaction<StoreOutcome>((txn) async {
      final existing = await txn.query('reports', columns: ['line'],
        where: 'device_id = ? AND event_id = ?',
        whereArgs: [deviceId, eventId], limit: 1);
      if (existing.isNotEmpty) {
        final stored = existing.first['line'] as Uint8List;
        return listEquals(stored, raw)
            ? StoreOutcome.alreadyStored : StoreOutcome.conflict;
      }
      final queued = Sqflite.firstIntValue(await txn.rawQuery(
        'SELECT COUNT(*) FROM reports WHERE state = ?', [stateQueued])) ?? 0;
      // Never evict an unsent report to make room. Backpressure BLE instead.
      if (queued >= _maxQueued) return StoreOutcome.full;
      await txn.insert('reports', {
        'device_id': deviceId, 'event_id': eventId, 'type': type,
        'line': Uint8List.fromList(raw), 'state': stateQueued,
        'created_at': DateTime.now().millisecondsSinceEpoch,
        'expires_at': expires.millisecondsSinceEpoch, 'retry_at': 0,
      }, conflictAlgorithm: ConflictAlgorithm.abort);
      return StoreOutcome.stored;
    });
    if (outcome == StoreOutcome.stored) notifyListeners();
    return outcome;
  }

  // Backward-compatible helper used by the test-report screen.
  Future<bool> enqueue(Uint8List line) async =>
      await store(line) == StoreOutcome.stored;

  Future<QueuedReport?> nextQueued() async {
    final db = await _db;
    final rows = await db.query('reports', columns: ['id', 'line'],
      where: 'state = ? AND retry_at <= ?',
      whereArgs: [stateQueued, DateTime.now().millisecondsSinceEpoch],
      orderBy: 'id', limit: 1);
    if (rows.isEmpty) return null;
    return QueuedReport(id: rows.first['id'] as int,
      line: rows.first['line'] as Uint8List);
  }

  Future<Duration?> nextWait() async {
    final db = await _db;
    final rows = await db.rawQuery(
      'SELECT MIN(retry_at) AS deadline FROM reports WHERE state = ?',
      [stateQueued]);
    final millis = rows.first['deadline'] as int?;
    if (millis == null) return null;
    final remaining = millis - DateTime.now().millisecondsSinceEpoch;
    return Duration(milliseconds: remaining > 0 ? remaining : 0);
  }

  Future<void> makeQueuedDueNow() async {
    final db = await _db;
    await db.update('reports', {'retry_at': 0},
      where: 'state = ?', whereArgs: [stateQueued]);
    notifyListeners();
  }

  Future<void> markSent(int id, String result) =>
      _finish(id, stateSent, result);

  Future<void> markRejected(int id, String result) =>
      _finish(id, stateRejected, result);

  Future<void> _finish(int id, String state, String result) async {
    final db = await _db;
    // Preserve original bytes in dead-letter history for debugging and
    // idempotent BLE redelivery until the configured history retention.
    await db.update('reports',
      {'state': state, 'last_result': result, 'retry_at': 0},
      where: 'id = ?', whereArgs: [id]);
    notifyListeners();
  }

  Future<void> recordFailure(int id, String result, Duration delay) async {
    final db = await _db;
    final next = DateTime.now().add(delay).millisecondsSinceEpoch;
    await db.rawUpdate(
      'UPDATE reports SET attempts = attempts + 1, last_result = ?, retry_at = ? WHERE id = ?',
      [result, next, id]);
    notifyListeners();
  }

  Future<void> cleanUp() async {
    final db = await _db;
    final now = DateTime.now();
    await db.update('reports',
      {'state': stateRejected, 'last_result': 'expired', 'retry_at': 0},
      where: 'state = ? AND expires_at <= ?',
      whereArgs: [stateQueued, now.millisecondsSinceEpoch]);
    await db.delete('reports', where: 'state != ? AND created_at <= ?',
      whereArgs: [stateQueued,
        now.subtract(_historyLifetime).millisecondsSinceEpoch]);
    notifyListeners();
  }

  Future<List<ReportSummary>> recent() async {
    final db = await _db;
    final rows = await db.query('reports',
      columns: ['event_id', 'type', 'state', 'attempts', 'last_result'],
      orderBy: 'id DESC', limit: 200);
    return rows.map((r) => ReportSummary(
      eventId: r['event_id'] as String, type: r['type'] as String,
      state: r['state'] as String, attempts: r['attempts'] as int,
      lastResult: r['last_result'] as String?)).toList();
  }
}
