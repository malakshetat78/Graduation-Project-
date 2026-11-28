import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../core/app_colors.dart';
import '../services/hazard_alert_service.dart';

class AlertScreen extends StatefulWidget {
  const AlertScreen({super.key, required this.hazard});

  final HazardAhead hazard;

  @override
  State<AlertScreen> createState() => _AlertScreenState();
}

class _AlertScreenState extends State<AlertScreen> {
  /// The driver should not have to touch the phone to clear a warning.
  static const _closeAfter = Duration(seconds: 10);

  Timer? _timer;

  @override
  void initState() {
    super.initState();
    unawaited(SystemSound.play(SystemSoundType.alert));
    unawaited(HapticFeedback.heavyImpact());
    _timer = Timer(_closeAfter, () {
      if (mounted) Navigator.maybePop(context);
    });
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final hazard = widget.hazard;

    return Scaffold(
      backgroundColor: AppColors.navy,
      body: SafeArea(
        child: Center(
          child: SingleChildScrollView(
            padding: const EdgeInsets.all(28),
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 380),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const Text(
                    'ROAD ALERT',
                    style: TextStyle(
                      color: AppColors.cream,
                      fontSize: 15,
                      letterSpacing: 3,
                    ),
                  ),

                  const SizedBox(height: 36),

                  const Icon(
                    Icons.warning_amber_rounded,
                    color: AppColors.coral,
                    size: 120,
                  ),

                  const SizedBox(height: 24),

                  Text(
                    hazard.title,
                    textAlign: TextAlign.center,
                    style: const TextStyle(
                      color: AppColors.cream,
                      fontSize: 30,
                      fontWeight: FontWeight.bold,
                    ),
                  ),

                  const SizedBox(height: 12),

                  Text(
                    hazard.aheadLabel,
                    style: const TextStyle(
                      color: AppColors.coral,
                      fontSize: 68,
                      fontWeight: FontWeight.w900,
                    ),
                  ),

                  const SizedBox(height: 8),

                  const Text(
                    'Slow down safely',
                    textAlign: TextAlign.center,
                    style: TextStyle(color: AppColors.cream, fontSize: 22),
                  ),

                  const SizedBox(height: 28),

                  Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 16,
                      vertical: 10,
                    ),
                    decoration: BoxDecoration(
                      color: AppColors.cream,
                      borderRadius: BorderRadius.circular(24),
                    ),
                    child: Text(
                      hazard.evidenceLabel,
                      style: const TextStyle(
                        color: AppColors.navy,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                  ),

                  const SizedBox(height: 36),

                  SizedBox(
                    width: double.infinity,
                    height: 54,
                    child: ElevatedButton(
                      onPressed: () {
                        Navigator.pop(context);
                      },
                      style: ElevatedButton.styleFrom(
                        backgroundColor: AppColors.turquoise,
                        foregroundColor: AppColors.navy,
                        elevation: 0,
                        shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(16),
                        ),
                      ),
                      child: const Text(
                        'Got it',
                        style: TextStyle(
                          fontSize: 17,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}
