import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:rovo_mobile/screens/alert_screen.dart';
import 'package:rovo_mobile/services/hazard_alert_service.dart';

HazardAhead hazard(String basis) {
  return HazardAhead(
    id: 'h1',
    type: 'pothole',
    level: 'warning',
    lat: 30.0,
    lon: 31.0,
    distanceM: 450,
    distanceBasis: basis,
    sources: 2,
    inAlertRange: true,
    alert: true,
  );
}

void main() {
  testWidgets('without a route it says Ahead, not a distance', (tester) async {
    await tester.pumpWidget(
      MaterialApp(home: AlertScreen(hazard: hazard('straight_line'))),
    );

    expect(find.text('Pothole ahead'), findsOneWidget);
    expect(find.text('Ahead'), findsOneWidget);
    expect(find.text('450 m'), findsNothing);
    expect(find.text('Reported by 2 vehicles'), findsOneWidget);
    await tester.pumpAndSettle(const Duration(seconds: 11));
  });

  testWidgets('with a route it gives the distance', (tester) async {
    await tester.pumpWidget(
      MaterialApp(home: AlertScreen(hazard: hazard('route'))),
    );

    expect(find.text('450 m'), findsOneWidget);
    await tester.pumpAndSettle(const Duration(seconds: 11));
  });

  testWidgets('closes by itself so the driver need not touch it', (
    tester,
  ) async {
    await tester.pumpWidget(
      MaterialApp(
        home: Builder(
          builder: (context) => TextButton(
            onPressed: () => Navigator.push(
              context,
              MaterialPageRoute(
                builder: (_) => AlertScreen(hazard: hazard('route')),
              ),
            ),
            child: const Text('open'),
          ),
        ),
      ),
    );
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();
    expect(find.text('Pothole ahead'), findsOneWidget);

    await tester.pump(const Duration(seconds: 10));
    await tester.pumpAndSettle();
    expect(find.text('Pothole ahead'), findsNothing);
  });
}
