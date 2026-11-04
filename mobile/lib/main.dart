import 'package:flutter/material.dart';

import 'core/app_colors.dart';
import 'screens/welcome_screen.dart';
import 'services/report_queue.dart';
import 'services/report_uploader.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await ReportQueue.instance.cleanUp();
  ReportUploader.instance.kick();
  runApp(const RovoApp());
}

class RovoApp extends StatelessWidget {
  const RovoApp({super.key});

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
      home: const WelcomeScreen(),
    );
  }
}
