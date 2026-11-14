import 'package:flutter/material.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

import '../core/app_colors.dart';
import 'home_screen.dart';

class NewPasswordScreen extends StatefulWidget {
  const NewPasswordScreen({super.key});

  @override
  State<NewPasswordScreen> createState() => _NewPasswordScreenState();
}

class _NewPasswordScreenState extends State<NewPasswordScreen> {
  final _formKey = GlobalKey<FormState>();
  final _passwordController = TextEditingController();

  bool _loading = false;
  String? _error;

  @override
  void dispose() {
    _passwordController.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    if (_loading || !_formKey.currentState!.validate()) return;

    FocusScope.of(context).unfocus();

    setState(() {
      _loading = true;
      _error = null;
    });

    try {
      await Supabase.instance.client.auth.updateUser(
        UserAttributes(password: _passwordController.text),
      );

      if (!mounted) return;

      Navigator.pushAndRemoveUntil(
        context,
        MaterialPageRoute(builder: (context) => const HomeScreen()),
        (route) => false,
      );
    } on AuthException catch (error) {
      if (!mounted) return;

      setState(() {
        _error = error.message;
      });
    } on Exception {
      if (!mounted) return;

      setState(() {
        _error = 'Could not save your password. Try again.';
      });
    } finally {
      if (mounted) {
        setState(() {
          _loading = false;
        });
      }
    }
  }

  InputDecoration _inputStyle(String label) {
    return InputDecoration(
      labelText: label,
      prefixIcon: const Icon(Icons.lock_outline_rounded),
      filled: true,
      fillColor: Colors.white,
      border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
    );
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
                    const Text(
                      'New password',
                      style: TextStyle(
                        color: AppColors.navy,
                        fontSize: 28,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const SizedBox(height: 24),
                    TextFormField(
                      controller: _passwordController,
                      obscureText: true,
                      autocorrect: false,
                      enableSuggestions: false,
                      decoration: _inputStyle('New password'),
                      validator: (value) {
                        return (value == null || value.length < 8)
                            ? 'Use at least 8 characters'
                            : null;
                      },
                    ),
                    const SizedBox(height: 16),
                    TextFormField(
                      obscureText: true,
                      autocorrect: false,
                      enableSuggestions: false,
                      onFieldSubmitted: (_) => _save(),
                      decoration: _inputStyle('Confirm password'),
                      validator: (value) {
                        return value == _passwordController.text
                            ? null
                            : 'Passwords do not match';
                      },
                    ),
                    const SizedBox(height: 20),
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
                        onPressed: _loading ? null : _save,
                        style: ElevatedButton.styleFrom(
                          backgroundColor: AppColors.turquoise,
                          foregroundColor: AppColors.navy,
                          elevation: 0,
                          shape: RoundedRectangleBorder(
                            borderRadius: BorderRadius.circular(16),
                          ),
                        ),
                        child: Text(_loading ? 'Saving...' : 'Save password'),
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
