import 'package:flutter/material.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

import 'core/app_colors.dart';
import 'screens/home_screen.dart';
import 'screens/new_password_screen.dart';
import 'screens/welcome_screen.dart';
import 'services/report_queue.dart';
import 'services/report_uploader.dart';

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
        _openNewPassword();
      }
    },
    onError: (Object error) {
      debugPrint('Auth link error: $error');
    },
  );

  final signedIn = Supabase.instance.client.auth.currentSession != null;
  runApp(RovoApp(signedIn: signedIn));
}

void _openNewPassword() {
  navigatorKey.currentState?.push(
    MaterialPageRoute<void>(builder: (context) => const NewPasswordScreen()),
  );
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
