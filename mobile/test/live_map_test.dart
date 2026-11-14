import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:geolocator/geolocator.dart';
import 'package:latlong2/latlong.dart';
import 'package:rovo_mobile/screens/live_map.dart';
import 'package:rovo_mobile/services/hazard_alert_service.dart';

/// A phone that is at the given place, with location on and allowed.
class _FakeLocation extends GeolocatorPlatform {
  _FakeLocation(this.lat, this.lon);

  final double lat;
  final double lon;

  @override
  Future<bool> isLocationServiceEnabled() async => true;

  @override
  Future<LocationPermission> checkPermission() async =>
      LocationPermission.whileInUse;

  @override
  Stream<Position> getPositionStream({LocationSettings? locationSettings}) {
    return Stream.value(
      Position(
        latitude: lat,
        longitude: lon,
        timestamp: DateTime.now(),
        accuracy: 5,
        altitude: 0,
        altitudeAccuracy: 0,
        heading: 90,
        headingAccuracy: 0,
        speed: 12,
        speedAccuracy: 0,
      ),
    );
  }
}

HazardAhead _pothole({double lon = 31.2857, double distanceM = 290}) {
  return HazardAhead(
    id: 'h1',
    type: 'pothole',
    level: 'confirmed',
    lat: 30.0444,
    lon: lon,
    distanceM: distanceM,
    distanceBasis: 'straight_line',
    sources: 2,
    inAlertRange: true,
    alert: false,
  );
}

Future<void> _pump(WidgetTester tester, Widget map) async {
  await tester.pumpWidget(
    MaterialApp(
      home: Scaffold(body: SizedBox(width: 400, height: 600, child: map)),
    ),
  );
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 100));
}

void main() {
  setUp(() => GeolocatorPlatform.instance = _FakeLocation(30.0444, 31.2827));

  testWidgets('tapping a hazard marker opens that hazard', (tester) async {
    HazardAhead? tapped;

    await _pump(
      tester,
      LiveMap(hazards: [_pothole()], onHazardTap: (h) => tapped = h),
    );

    await tester.tap(find.byKey(const ValueKey('hazard-h1')));
    await tester.pump();

    expect(tapped?.id, 'h1');
  });

  testWidgets('the camera shows the car and the next hazard together', (
    tester,
  ) async {
    // About 900 m ahead: off screen at the old fixed zoom of 16.
    await _pump(
      tester,
      LiveMap(hazards: [_pothole(lon: 31.2920, distanceM: 900)]),
    );

    final camera = MapCamera.of(tester.element(find.byType(MarkerLayer).first));
    final size = tester.getSize(find.byType(FlutterMap));

    for (final point in const [(30.0444, 31.2827), (30.0444, 31.2920)]) {
      final offset = camera.latLngToScreenOffset(LatLng(point.$1, point.$2));
      expect(offset.dx, inInclusiveRange(0, size.width), reason: '$point');
      expect(offset.dy, inInclusiveRange(0, size.height), reason: '$point');
    }

    // Zoomed out just enough, not to the whole city.
    expect(camera.zoom, inInclusiveRange(13, 16));
  });

  testWidgets('without hazards the map just follows the car', (tester) async {
    await _pump(tester, const LiveMap());

    final camera = MapCamera.of(tester.element(find.byType(MarkerLayer).first));

    expect(camera.center.latitude, closeTo(30.0444, 1e-6));
    expect(camera.center.longitude, closeTo(31.2827, 1e-6));
    expect(find.byIcon(Icons.warning_amber_rounded), findsNothing);
  });
}
