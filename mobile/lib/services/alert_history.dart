import 'package:flutter/foundation.dart';
import 'package:path/path.dart' as p;
import 'package:sqflite/sqflite.dart';

import 'hazard_alert_service.dart';

class AlertRecord {
  const AlertRecord({
    required this.title,
    required this.evidence,
    required this.at,
  });

  final String title;
  final String evidence;
  final DateTime at;
}

/// Alerts this phone has shown, kept only on the phone (the backend records
/// when a user was alerted, for the cooldown, but not a history). No position
/// is stored, so this is not a record of where the driver has been.
class AlertHistory extends ChangeNotifier {
  AlertHistory({this.path});

  static final AlertHistory instance = AlertHistory();

  static const _lifetime = Duration(days: 7);

  final String? path;

  Future<Database>? _database;

  Future<Database> get _db => _database ??= _open();

  Future<Database> _open() async {
    return openDatabase(
      path ?? p.join(await getDatabasesPath(), 'rovo_alert_history.db'),
      version: 1,
      onCreate: (db, version) {
        return db.execute('''
          CREATE TABLE alerts (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            evidence TEXT NOT NULL,
            at INTEGER NOT NULL
          )
        ''');
      },
    );
  }

  Future<void> add(HazardAhead hazard) async {
    final db = await _db;
    final now = DateTime.now();

    await db.insert('alerts', {
      'title': hazard.title,
      'evidence': hazard.evidenceLabel,
      'at': now.millisecondsSinceEpoch,
    });
    await db.delete(
      'alerts',
      where: 'at <= ?',
      whereArgs: [now.subtract(_lifetime).millisecondsSinceEpoch],
    );

    notifyListeners();
  }

  Future<List<AlertRecord>> recent() async {
    final db = await _db;
    final rows = await db.query(
      'alerts',
      orderBy: 'at DESC, id DESC',
      limit: 200,
    );

    return rows.map((row) {
      return AlertRecord(
        title: row['title'] as String,
        evidence: row['evidence'] as String,
        at: DateTime.fromMillisecondsSinceEpoch(row['at'] as int),
      );
    }).toList();
  }
}
