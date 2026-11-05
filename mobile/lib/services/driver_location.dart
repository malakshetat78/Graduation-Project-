import 'package:geolocator/geolocator.dart';

class DriverFix {
  const DriverFix({
    required this.lat,
    required this.lon,
    required this.speedMps,
    this.headingDeg,
  });

  final double lat;
  final double lon;
  final double speedMps;

  /// Null when the phone has no usable direction.
  final double? headingDeg;
}

class DriverLocation {
  DriverLocation._();

  static Future<bool> allowed() async {
    if (!await Geolocator.isLocationServiceEnabled()) return false;

    var permission = await Geolocator.checkPermission();

    if (permission == LocationPermission.denied) {
      permission = await Geolocator.requestPermission();
    }

    return permission == LocationPermission.always ||
        permission == LocationPermission.whileInUse;
  }

  static Stream<DriverFix> fixes() {
    return Geolocator.getPositionStream(
      locationSettings: AndroidSettings(
        accuracy: LocationAccuracy.high,
        distanceFilter: 0,
        // Works when the driver declines Google's Location Accuracy prompt.
        forceLocationManager: true,
        intervalDuration: const Duration(seconds: 1),
      ),
    ).map(_fix);
  }

  static Future<DriverFix> current() async {
    return _fix(
      await Geolocator.getCurrentPosition(
        locationSettings: AndroidSettings(
          accuracy: LocationAccuracy.high,
          forceLocationManager: true,
        ),
      ),
    );
  }

  static DriverFix _fix(Position position) {
    return DriverFix(
      lat: position.latitude,
      lon: position.longitude,
      speedMps: position.speed < 0 ? 0 : position.speed,
      headingDeg: position.heading < 0 ? null : position.heading % 360,
    );
  }
}
