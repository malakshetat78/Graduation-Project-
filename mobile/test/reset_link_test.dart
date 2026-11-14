import 'package:flutter_test/flutter_test.dart';
import 'package:rovo_mobile/services/reset_link.dart';
import 'package:supabase_flutter/supabase_flutter.dart' show AuthException;

void main() {
  test('recognises an expired link from the redirect', () {
    final error = AuthException('Link expired', statusCode: 'otp_expired');

    expect(isResetLinkError(error), isTrue);
  });

  test('recognises an expired code exchange', () {
    final error = AuthException('Expired', code: 'flow_state_expired');

    expect(isResetLinkError(error), isTrue);
  });

  test('recognises a missing code verifier', () {
    final error = AuthException(
      'Code verifier could not be found in local storage.',
    );

    expect(isResetLinkError(error), isTrue);
  });

  test('ignores other errors', () {
    expect(isResetLinkError(AuthException('Network failed')), isFalse);
    expect(isResetLinkError(Exception('other')), isFalse);
  });
}
