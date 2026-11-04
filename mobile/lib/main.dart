import 'package:flutter/material.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

import 'core/app_colors.dart';
import 'screens/home_screen.dart';
import 'screens/welcome_screen.dart';
import 'services/report_queue.dart';
import 'services/report_uploader.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  await Supabase.initialize(
    url: const String.fromEnvironment('SUPABASE_URL'),
    publishableKey: const String.fromEnvironment('SUPABASE_PUBLISHABLE_KEY'),
  );

  await ReportQueue.instance.cleanUp();
  ReportUploader.instance.kick();

  final signedIn = Supabase.instance.client.auth.currentSession != null;
  runApp(RovoApp(signedIn: signedIn));
}

class RovoApp extends StatelessWidget {
  const RovoApp({super.key, this.signedIn = false});

  final bool signedIn;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ROVO',
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
