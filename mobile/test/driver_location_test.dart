import 'package:flutter_test/flutter_test.dart';
import 'package:geolocator/geolocator.dart';
import 'package:rovo_mobile/services/driver_location.dart';

Position _at({required double speed, required double heading}) {
  return Position(
    latitude: 30.0444,
    longitude: 31.2827,
    timestamp: DateTime(2026),
    accuracy: 5,
    altitude: 0,
    altitudeAccuracy: 0,
    heading: heading,
    headingAccuracy: 0,
    speed: speed,
    speedAccuracy: 0,
  );
}

void main() {
  test('keeps the direction while driving', () {
    expect(DriverLocation.fixFrom(_at(speed: 12, heading: 90)).headingDeg, 90);
  });

  test('a stopped car has no direction, not north', () {
    // Android gives heading 0 when it has no bearing, e.g. at a red light.
    final fix = DriverLocation.fixFrom(_at(speed: 0, heading: 0));

    expect(fix.headingDeg, isNull);
    expect(fix.speedMps, 0);
  });

  test('a missing heading stays unknown', () {
    expect(
      DriverLocation.fixFrom(_at(speed: 12, heading: -1)).headingDeg,
      isNull,
    );
  });
}
