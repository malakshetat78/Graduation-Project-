import 'package:flutter/material.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

import '../core/app_colors.dart';

class ForgotPasswordScreen extends StatefulWidget {
  const ForgotPasswordScreen({super.key});

  @override
  State<ForgotPasswordScreen> createState() => _ForgotPasswordScreenState();
}

class _ForgotPasswordScreenState extends State<ForgotPasswordScreen> {
  final _formKey = GlobalKey<FormState>();
  final _emailController = TextEditingController();

  bool _loading = false;
  bool _showConfirmation = false;
  String? _error;

  @override
  void dispose() {
    _emailController.dispose();
    super.dispose();
  }

  Future<void> _sendReset() async {
    if (_loading || !_formKey.currentState!.validate()) return;

    FocusScope.of(context).unfocus();

    setState(() {
      _loading = true;
      _error = null;
    });

    try {
      await Supabase.instance.client.auth.resetPasswordForEmail(
        _emailController.text.trim(),
        redirectTo: 'rovo://reset-password',
      );

      if (!mounted) return;

      setState(() {
        _showConfirmation = true;
      });
    } on AuthException catch (error) {
      if (!mounted) return;

      setState(() {
        _error = error.message;
      });
    } on Exception {
      if (!mounted) return;

      setState(() {
        _error = 'Could not send the email. Try again.';
      });
    } finally {
      if (mounted) {
        setState(() {
          _loading = false;
        });
      }
    }
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
                      controller: _emailController,
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

                    if (_error != null)
                      Padding(
                        padding: const EdgeInsets.only(bottom: 12),
                        child: Text(
                          _error!,
                          textAlign: TextAlign.center,
                          style: const TextStyle(color: AppColors.coral),
                        ),
                      ),

                    SizedBox(
                      width: double.infinity,
                      height: 54,
                      child: ElevatedButton(
                        onPressed: _loading ? null : _sendReset,
                        style: ElevatedButton.styleFrom(
                          backgroundColor: AppColors.turquoise,
                          foregroundColor: AppColors.navy,
                          elevation: 0,
                          shape: RoundedRectangleBorder(
                            borderRadius: BorderRadius.circular(16),
                          ),
                        ),
                        child: _loading
                            ? const SizedBox(
                                width: 22,
                                height: 22,
                                child: CircularProgressIndicator(
                                  strokeWidth: 2,
                                  color: AppColors.navy,
                                ),
                              )
                            : const Text(
                                'Send reset email',
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
                        'If this email has an account, '
                        'a reset link is on its way.',
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
