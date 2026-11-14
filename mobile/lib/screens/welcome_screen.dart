import 'package:flutter/material.dart';

import '../core/app_colors.dart';
import 'login_screen.dart';
import 'signup_screen.dart';

class WelcomeScreen extends StatelessWidget {
  const WelcomeScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.navy,
      body: SafeArea(
        child: Center(
          child: SingleChildScrollView(
            padding: const EdgeInsets.symmetric(horizontal: 28, vertical: 40),
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 380),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  ClipOval(
                    child: Image.asset(
                      'assets/images/rovo_logo.jpeg',
                      width: 220,
                      height: 220,
                      fit: BoxFit.cover,
                    ),
                  ),

                  const SizedBox(height: 32),

                  const Text(
                    'Stay aware.\nDrive safer.',
                    textAlign: TextAlign.center,
                    style: TextStyle(
                      color: AppColors.cream,
                      fontSize: 34,
                      fontWeight: FontWeight.bold,
                      height: 1.2,
                    ),
                  ),

                  const SizedBox(height: 16),

                  const Text(
                    'Connected alerts for the road ahead.',
                    textAlign: TextAlign.center,
                    style: TextStyle(
                      color: AppColors.cream,
                      fontSize: 16,
                      height: 1.5,
                    ),
                  ),

                  const SizedBox(height: 48),

                  SizedBox(
                    width: double.infinity,
                    height: 54,
                    child: ElevatedButton(
                      onPressed: () {
                        Navigator.push(
                          context,
                          MaterialPageRoute(
                            builder: (context) => const SignupScreen(),
                          ),
                        );
                      },
                      style: ElevatedButton.styleFrom(
                        backgroundColor: AppColors.turquoise,
                        foregroundColor: AppColors.navy,
                        elevation: 0,
                        shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(16),
                        ),
                      ),
                      child: const Row(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          Text(
                            'Get started',
                            style: TextStyle(
                              fontSize: 17,
                              fontWeight: FontWeight.bold,
                            ),
                          ),
                          SizedBox(width: 12),
                          Icon(Icons.arrow_forward_rounded),
                        ],
                      ),
                    ),
                  ),

                  const SizedBox(height: 12),

                  TextButton(
                    onPressed: () {
                      Navigator.push(
                        context,
                        MaterialPageRoute(
                          builder: (context) => const LoginScreen(),
                        ),
                      );
                    },
                    style: TextButton.styleFrom(
                      foregroundColor: AppColors.cream,
                      minimumSize: const Size(0, 48),
                    ),
                    child: const Text(
                      'I already have an account',
                      style: TextStyle(
                        fontSize: 14,
                        decoration: TextDecoration.underline,
                        decorationColor: AppColors.cream,
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
