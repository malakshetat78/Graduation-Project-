import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:sqflite/sqflite.dart' show Sqflite;
import 'package:rovo_mobile/services/report_queue.dart';
import 'package:sqflite_common_ffi/sqflite_ffi.dart';

Uint8List signedFixture(String eventId, {String type = 'pothole'}) {
  final raw = jsonEncode({
    'event': {
      'device_id': 'test-device',
      'event_id': eventId,
      'type': type,
      'detected_at': DateTime.now().toUtc().toIso8601String(),
    },
    'sender': 'test-device',
    'msg_id': eventId,
    'signature': 'example-only',
  });
  return Uint8List.fromList(utf8.encode(raw));
}

void main() {
  sqfliteFfiInit();
  test('durable bytes, exact duplicate, conflicting same ID', () async {
    final db = await databaseFactoryFfi.openDatabase(inMemoryDatabasePath,
      options: OpenDatabaseOptions(version: 2, onCreate: (db, _) async {
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
      }));
    final queue = ReportQueue.withDatabaseForTesting(db);
    final original = signedFixture('first');
    expect(await queue.store(original), StoreOutcome.stored);
    expect(await queue.store(original), StoreOutcome.alreadyStored);
    expect(await queue.store(signedFixture('first', type: 'speed_bump')),
      StoreOutcome.conflict);
    expect((await queue.nextQueued())!.line, original);
    await queue.markSent((await queue.nextQueued())!.id, 'accepted');
    expect(await queue.store(original), StoreOutcome.alreadyStored);
    await db.close();
  });

  test('backoff persists and manual retry restores eligibility', () async {
    final db = await databaseFactoryFfi.openDatabase(inMemoryDatabasePath,
      options: OpenDatabaseOptions(version: 2, onCreate: (db, _) async {
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
      }));
    final queue = ReportQueue.withDatabaseForTesting(db);
    expect(await queue.store(signedFixture('backoff')), StoreOutcome.stored);
    final pending = await queue.nextQueued();
    expect(pending, isNotNull);
    await queue.recordFailure(pending!.id, 'offline', const Duration(minutes: 5));
    expect(await queue.nextQueued(), isNull);
    expect((await queue.nextWait())!.inSeconds, greaterThan(200));
    await queue.makeQueuedDueNow();
    expect((await queue.nextQueued())!.id, pending.id);
    await queue.markRejected(pending.id, 'bad_signature');
    final history = await db.query('reports', columns: ['line', 'state']);
    expect(history.single['line'], isNotEmpty);
    expect(history.single['state'], ReportQueue.stateRejected);
    await db.close();
  });

  test('full queue never evicts an unsent report', () async {
    final db = await databaseFactoryFfi.openDatabase(inMemoryDatabasePath,
      options: OpenDatabaseOptions(version: 2, onCreate: (db, _) async {
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
      }));
    await db.transaction((txn) async {
      for (var i = 0; i < 1000; i++) {
        await txn.insert('reports', {
          'device_id': 'test-device', 'event_id': 'fixture-$i',
          'type': 'pothole', 'line': Uint8List.fromList([i % 256]),
          'state': 'queued', 'attempts': 0, 'created_at': 0,
          'expires_at': 2147483647000, 'retry_at': 0,
        });
      }
    });
    final queue = ReportQueue.withDatabaseForTesting(db);
    expect(await queue.store(signedFixture('overflow')), StoreOutcome.full);
    expect(Sqflite.firstIntValue(await db.rawQuery(
      'SELECT COUNT(*) FROM reports WHERE state = ?', ['queued'])), 1000);
    final rows = await db.query('reports', where: 'event_id = ?',
      whereArgs: ['fixture-0']);
    expect(rows, isNotEmpty);
    await db.close();
  });
}
