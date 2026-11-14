import 'package:flutter_test/flutter_test.dart';
import 'package:rovo_mobile/services/alert_history.dart';
import 'package:rovo_mobile/services/hazard_alert_service.dart';
import 'package:sqflite_common_ffi/sqflite_ffi.dart';

const hazard = HazardAhead(
  id: 'h1',
  type: 'speed_bump',
  level: 'warning',
  lat: 30.0444,
  lon: 31.2357,
  distanceM: 450,
  distanceBasis: 'straight_line',
  sources: 1,
  inAlertRange: true,
  alert: true,
);

void main() {
  setUpAll(() {
    sqfliteFfiInit();
    databaseFactory = databaseFactoryFfi;
  });

  test('keeps alerts newest first, with no position', () async {
    final history = AlertHistory(path: inMemoryDatabasePath);

    await history.add(hazard);
    await history.add(hazard);

    final alerts = await history.recent();
    expect(alerts, hasLength(2));
    expect(alerts.first.title, 'Speed bump ahead');
    expect(alerts.first.evidence, 'Reported by 1 vehicle');

    final db = await databaseFactory.openDatabase(inMemoryDatabasePath);
    final columns = await db.rawQuery('PRAGMA table_info(alerts)');
    final names = columns.map((column) => column['name']).toSet();
    expect(names, {'id', 'title', 'evidence', 'at'});
  });
}
