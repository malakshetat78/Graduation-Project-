import 'package:flutter_test/flutter_test.dart';
import 'package:rovo_mobile/services/device_service.dart';

void main() {
  test('forgets the devices after sign out', () {
    final service = DeviceService.instance;

    service.devices = ['car-1'];
    service.lastProblem = 'Could not load your devices.';

    service.clear();

    expect(service.devices, isEmpty);
    expect(service.lastProblem, isNull);
  });
}
