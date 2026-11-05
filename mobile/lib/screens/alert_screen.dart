import 'package:flutter/material.dart';

import '../core/app_colors.dart';

class AlertScreen extends StatelessWidget {
  const AlertScreen({super.key});

  @override
  Widget build(BuildContext context) {
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

                  const Text(
                    'Pothole ahead',
                    textAlign: TextAlign.center,
                    style: TextStyle(
                      color: AppColors.cream,
                      fontSize: 30,
                      fontWeight: FontWeight.bold,
                    ),
                  ),

                  const SizedBox(height: 12),

                  const Text(
                    '250 m',
                    style: TextStyle(
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
                    child: const Text(
                      'Verified by 3 vehicles',
                      style: TextStyle(
                        color: AppColors.navy,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                  ),

                  const SizedBox(height: 24),

                  const Text(
                    'Sample alert · Demonstration only',
                    textAlign: TextAlign.center,
                    style: TextStyle(color: AppColors.cream, fontSize: 12),
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
