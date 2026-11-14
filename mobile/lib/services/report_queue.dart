import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:path/path.dart' as p;
import 'package:sqflite/sqflite.dart';

class QueuedReport {
  const QueuedReport({required this.id, required this.line});

  final int id;
  final Uint8List line;
}

class ReportSummary {
  const ReportSummary({
    required this.eventId,
    required this.type,
    required this.state,
    required this.attempts,
    required this.lastResult,
  });

  final String eventId;
  final String type;
  final String state;
  final int attempts;
  final String? lastResult;
}

class ReportQueue extends ChangeNotifier {
  ReportQueue({this._databasePath});

  static final ReportQueue instance = ReportQueue();

  static const stateQueued = 'queued';
  static const stateSent = 'sent';
  static const stateRejected = 'rejected';

  static const _maxQueued = 1000;
  static const _potholeLifetime = Duration(hours: 47);
  static const _speedBumpLifetime = Duration(days: 13);
  static const _historyLifetime = Duration(days: 7);

  final String? _databasePath;

  Future<Database>? _database;

  Future<Database> get _db => _database ??= _open();

  Future<Database> _open() async {
    final path =
        _databasePath ?? p.join(await getDatabasesPath(), 'rovo_reports.db');

    return openDatabase(
      path,
      version: 1,
      onCreate: (db, version) {
        return db.execute('''
          CREATE TABLE reports (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT NOT NULL,
            event_id TEXT NOT NULL,
            type TEXT NOT NULL,
            line BLOB NOT NULL,
            state TEXT NOT NULL,
            attempts INTEGER NOT NULL DEFAULT 0,
            last_result TEXT,
            created_at INTEGER NOT NULL,
            expires_at INTEGER NOT NULL,
            UNIQUE (device_id, event_id)
          )
        ''');
      },
    );
  }

  /// Stores the signed line exactly as received. The JSON is read only to
  /// fill the columns; the stored bytes are never rebuilt from it.
  Future<bool> enqueue(Uint8List line) async {
    final envelope = jsonDecode(utf8.decode(line));

    if (envelope is! Map<String, dynamic>) {
      throw const FormatException('Invalid signed report');
    }

    final event = envelope['event'];

    if (event is! Map<String, dynamic>) {
      throw const FormatException('Invalid signed report');
    }

    final deviceId = event['device_id'];
    final eventId = event['event_id'];
    final type = event['type'];
    final detectedAt = event['detected_at'];

    if (deviceId is! String ||
        eventId is! String ||
        type is! String ||
        detectedAt is! String) {
      throw const FormatException('Invalid signed report');
    }

    final lifetime = type == 'speed_bump'
        ? _speedBumpLifetime
        : _potholeLifetime;
    final expiresAt = DateTime.parse(detectedAt).add(lifetime);
    final db = await _db;

    final insertedId = await db.transaction<int>((txn) async {
      final stored = await txn.query(
        'reports',
        columns: ['id', 'line', 'state'],
        where: 'device_id = ? AND event_id = ?',
        whereArgs: [deviceId, eventId],
        limit: 1,
      );

      // A duplicate must never push an older report out of a full queue.
      if (stored.isNotEmpty) {
        final row = stored.first;
        final state = row['state'] as String;
        final storedLine = row['line'] as Uint8List;

        // Only a still-queued report keeps its original bytes (a
        // finished report's line is cleared), so content can only be
        // compared while it's still queued. Same event_id but different
        // content is logged on the existing row instead of being
        // silently dropped.
        if (state == stateQueued && !_bytesEqual(storedLine, line)) {
          await txn.rawUpdate(
            'UPDATE reports SET attempts = attempts + 1, last_result = ? '
            'WHERE id = ?',
            ['event_id_conflict', row['id'] as int],
          );
        }

        return 0;
      }

      final queued =
          Sqflite.firstIntValue(
            await txn.rawQuery('SELECT COUNT(*) FROM reports WHERE state = ?', [
              stateQueued,
            ]),
          ) ??
          0;

      if (queued >= _maxQueued) {
        await txn.rawDelete(
          '''
          DELETE FROM reports WHERE id = (
            SELECT id FROM reports
            WHERE state = ?
            ORDER BY CASE type WHEN 'pothole' THEN 0 ELSE 1 END, id
            LIMIT 1
          )
          ''',
          [stateQueued],
        );
      }

      return txn.insert('reports', {
        'device_id': deviceId,
        'event_id': eventId,
        'type': type,
        'line': line,
        'state': stateQueued,
        'created_at': DateTime.now().millisecondsSinceEpoch,
        'expires_at': expiresAt.millisecondsSinceEpoch,
      }, conflictAlgorithm: ConflictAlgorithm.ignore);
    });

    notifyListeners();
    return insertedId > 0;
  }

  bool _bytesEqual(Uint8List a, Uint8List b) {
    if (identical(a, b)) return true;
    if (a.length != b.length) return false;

    for (var i = 0; i < a.length; i++) {
      if (a[i] != b[i]) return false;
    }

    return true;
  }

  Future<QueuedReport?> nextQueued() async {
    final db = await _db;
    final rows = await db.query(
      'reports',
      columns: ['id', 'line'],
      where: 'state = ?',
      whereArgs: [stateQueued],
      orderBy: 'id',
      limit: 1,
    );

    if (rows.isEmpty) return null;

    return QueuedReport(
      id: rows.first['id'] as int,
      line: rows.first['line'] as Uint8List,
    );
  }

  Future<void> markSent(int id, String result) {
    return _finish(id, stateSent, result);
  }

  Future<void> markRejected(int id, String result) {
    return _finish(id, stateRejected, result);
  }

  Future<void> _finish(int id, String state, String result) async {
    final db = await _db;

    await db.update(
      'reports',
      {'state': state, 'last_result': result, 'line': Uint8List(0)},
      where: 'id = ?',
      whereArgs: [id],
    );

    notifyListeners();
  }

  Future<void> recordFailure(int id, String result) async {
    final db = await _db;

    await db.rawUpdate(
      'UPDATE reports SET attempts = attempts + 1, last_result = ? '
      'WHERE id = ?',
      [result, id],
    );

    notifyListeners();
  }

  Future<void> cleanUp() async {
    final db = await _db;
    final now = DateTime.now();

    await db.update(
      'reports',
      {'state': stateRejected, 'last_result': 'expired', 'line': Uint8List(0)},
      where: 'state = ? AND expires_at <= ?',
      whereArgs: [stateQueued, now.millisecondsSinceEpoch],
    );

    await db.delete(
      'reports',
      where: 'state != ? AND created_at <= ?',
      whereArgs: [
        stateQueued,
        now.subtract(_historyLifetime).millisecondsSinceEpoch,
      ],
    );

    notifyListeners();
  }

  Future<List<ReportSummary>> recent() async {
    final db = await _db;
    final rows = await db.query(
      'reports',
      columns: ['event_id', 'type', 'state', 'attempts', 'last_result'],
      orderBy: 'id DESC',
      limit: 200,
    );

    return rows.map((row) {
      return ReportSummary(
        eventId: row['event_id'] as String,
        type: row['type'] as String,
        state: row['state'] as String,
        attempts: row['attempts'] as int,
        lastResult: row['last_result'] as String?,
      );
    }).toList();
  }

  /// Counts over the whole table, not just the most recent rows `recent()`
  /// returns, so the Reports screen's totals are always accurate.
  Future<Map<String, int>> counts() async {
    final db = await _db;
    final rows = await db.rawQuery(
      'SELECT state, COUNT(*) AS count FROM reports GROUP BY state',
    );

    final result = <String, int>{
      stateQueued: 0,
      stateSent: 0,
      stateRejected: 0,
    };

    for (final row in rows) {
      final state = row['state'] as String?;
      final count = row['count'] as int?;

      if (state != null && count != null) {
        result[state] = count;
      }
    }

    return result;
  }
}
