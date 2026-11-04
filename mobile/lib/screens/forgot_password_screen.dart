import 'package:flutter/material.dart';

import '../core/app_colors.dart';

class ForgotPasswordScreen extends StatefulWidget {
  const ForgotPasswordScreen({super.key});

  @override
  State<ForgotPasswordScreen> createState() => _ForgotPasswordScreenState();
}

class _ForgotPasswordScreenState extends State<ForgotPasswordScreen> {
  final _formKey = GlobalKey<FormState>();
  bool _showConfirmation = false;

  void _previewReset() {
    if (!_formKey.currentState!.validate()) return;

    FocusScope.of(context).unfocus();

    setState(() {
      _showConfirmation = true;
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.cream,
      appBar: AppBar(
        backgroundColor: AppColors.cream,
        foregroundColor: AppColors.navy,
        surfaceTintColor: Colors.transparent,
      ),
      body: SafeArea(
        child: Center(
          child: SingleChildScrollView(
            padding: const EdgeInsets.all(28),
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 380),
              child: Form(
                key: _formKey,
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    const Icon(
                      Icons.lock_reset_rounded,
                      size: 80,
                      color: AppColors.navy,
                    ),

                    const SizedBox(height: 24),

                    const Text(
                      'Forgot password?',
                      textAlign: TextAlign.center,
                      style: TextStyle(
                        color: AppColors.navy,
                        fontSize: 28,
                        fontWeight: FontWeight.bold,
                      ),
                    ),

                    const SizedBox(height: 12),

                    const Text(
                      'Enter your email to request a password reset.',
                      textAlign: TextAlign.center,
                      style: TextStyle(
                        color: AppColors.navy,
                        fontSize: 15,
                        height: 1.5,
                      ),
                    ),

                    const SizedBox(height: 30),

                    TextFormField(
                      keyboardType: TextInputType.emailAddress,
                      autocorrect: false,
                      onChanged: (_) {
                        if (_showConfirmation) {
                          setState(() {
                            _showConfirmation = false;
                          });
                        }
                      },
                      decoration: InputDecoration(
                        labelText: 'Email',
                        hintText: 'user@example.com',
                        prefixIcon: const Icon(Icons.mail_outline),
                        filled: true,
                        fillColor: Colors.white,
                        border: OutlineInputBorder(
                          borderRadius: BorderRadius.circular(16),
                        ),
                      ),
                      validator: (value) {
                        final email = (value ?? '').trim();

                        if (email.isEmpty) {
                          return 'Please enter your email';
                        }

                        if (!RegExp(r'^[^\s@]+@[^\s@]+\.[^\s@]+$')
                            .hasMatch(email)) {
                          return 'Please enter a valid email';
                        }

                        return null;
                      },
                    ),

                    const SizedBox(height: 24),

                    SizedBox(
                      width: double.infinity,
                      height: 54,
                      child: ElevatedButton(
                        onPressed: _previewReset,
                        style: ElevatedButton.styleFrom(
                          backgroundColor: AppColors.turquoise,
                          foregroundColor: AppColors.navy,
                          elevation: 0,
                          shape: RoundedRectangleBorder(
                            borderRadius: BorderRadius.circular(16),
                          ),
                        ),
                        child: const Text(
                          'Preview reset request',
                          style: TextStyle(
                            fontSize: 16,
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                      ),
                    ),

                    if (_showConfirmation) ...[
                      const SizedBox(height: 24),
                      const Icon(
                        Icons.check_circle_outline,
                        color: AppColors.navy,
                        size: 36,
                      ),
                      const SizedBox(height: 10),
                      const Text(
                        'Email format accepted.\n'
                        'This is a preview. No reset email has been sent.',
                        textAlign: TextAlign.center,
                        style: TextStyle(color: AppColors.navy, height: 1.5),
                      ),
                    ],

                    const SizedBox(height: 16),

                    TextButton(
                      onPressed: () => Navigator.pop(context),
                      child: const Text(
                        'Back to sign in',
                        style: TextStyle(color: AppColors.navy),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}
