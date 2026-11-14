import 'package:supabase_flutter/supabase_flutter.dart' show AuthException;

const _resetLinkCodes = {
  'bad_code_verifier',
  'flow_state_expired',
  'flow_state_not_found',
  'otp_expired',
};

/// True when a password reset link can no longer be used.
bool isResetLinkError(Object error) {
  if (error is! AuthException) return false;

  return _resetLinkCodes.contains(error.code) ||
      _resetLinkCodes.contains(error.statusCode) ||
      error.message.contains('Code verifier');
}
