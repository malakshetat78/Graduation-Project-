import 'package:flutter_test/flutter_test.dart';
import 'package:rovo_mobile/main.dart';

void main() {
  testWidgets('ROVO welcome screen appears', (WidgetTester tester) async {
    await tester.pumpWidget(const RovoApp());

    expect(find.text('Stay aware.\nDrive safer.'), findsOneWidget);
    expect(find.text('Connected alerts for the road ahead.'), findsOneWidget);
  });
}
