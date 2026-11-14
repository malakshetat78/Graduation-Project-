import 'dart:async';

import 'package:flutter/material.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

import 'core/app_colors.dart';
import 'screens/forgot_password_screen.dart';
import 'screens/home_screen.dart';
import 'screens/new_password_screen.dart';
import 'screens/welcome_screen.dart';
import 'services/beacon_service.dart';
import 'services/device_service.dart';
import 'services/report_queue.dart';
import 'services/report_uploader.dart';
import 'services/reset_link.dart';

final GlobalKey<NavigatorState> navigatorKey = GlobalKey<NavigatorState>();

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  await Supabase.initialize(
    url: const String.fromEnvironment('SUPABASE_URL'),
    publishableKey: const String.fromEnvironment('SUPABASE_PUBLISHABLE_KEY'),
  );

  await ReportQueue.instance.cleanUp();
  ReportUploader.instance.kick();

  Supabase.instance.client.auth.onAuthStateChange.listen(
    (data) {
      debugPrint('Auth event: ${data.event}');

      if (data.event == AuthChangeEvent.passwordRecovery) {
        _open(const NewPasswordScreen());
      }

      if (data.event == AuthChangeEvent.signedOut) {
        unawaited(BeaconService.instance.clear());
        DeviceService.instance.clear();
      }
    },
    onError: (Object error) {
      debugPrint('Auth link error: $error');

      if (isResetLinkError(error)) {
        _open(
          const ForgotPasswordScreen(
            notice: 'This reset link has expired. Request a new one.',
          ),
        );
      }
    },
  );

  final signedIn = Supabase.instance.client.auth.currentSession != null;
  runApp(RovoApp(signedIn: signedIn));
}

/// Opens a screen now, or as soon as the app has its first frame.
void _open(Widget screen) {
  final navigator = navigatorKey.currentState;

  if (navigator != null) {
    navigator.push(MaterialPageRoute<void>(builder: (context) => screen));
    return;
  }

  WidgetsBinding.instance.addPostFrameCallback((_) => _open(screen));
  WidgetsBinding.instance.ensureVisualUpdate();
}

class RovoApp extends StatelessWidget {
  const RovoApp({super.key, this.signedIn = false});

  final bool signedIn;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ROVO',
      navigatorKey: navigatorKey,
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        useMaterial3: true,
        scaffoldBackgroundColor: AppColors.navy,
        colorScheme: ColorScheme.fromSeed(seedColor: AppColors.turquoise),
      ),
      home: signedIn ? const HomeScreen() : const WelcomeScreen(),
    );
  }
}
