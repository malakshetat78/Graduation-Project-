import 'package:flutter/material.dart';

import '../core/app_colors.dart';
import '../services/device_service.dart';

class DevicesScreen extends StatefulWidget {
  const DevicesScreen({super.key});

  @override
  State<DevicesScreen> createState() => _DevicesScreenState();
}

class _DevicesScreenState extends State<DevicesScreen> {
  final _formKey = GlobalKey<FormState>();
  final _deviceIdController = TextEditingController();
  final _codeController = TextEditingController();
  final DeviceService _service = DeviceService.instance;

  bool _linking = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    _service.load();
  }

  @override
  void dispose() {
    _deviceIdController.dispose();
    _codeController.dispose();
    super.dispose();
  }

  Future<void> _link() async {
    if (_linking || !_formKey.currentState!.validate()) return;

    FocusScope.of(context).unfocus();

    setState(() {
      _linking = true;
      _error = null;
    });

    final problem = await _service.link(
      _deviceIdController.text.trim(),
      _codeController.text.trim(),
    );

    if (!mounted) return;

    setState(() {
      _linking = false;
      _error = problem;
    });

    if (problem == null) {
      _deviceIdController.clear();
      _codeController.clear();
    }
  }

  InputDecoration _inputStyle(String label, IconData icon) {
    return InputDecoration(
      labelText: label,
      prefixIcon: Icon(icon, color: AppColors.navy),
      filled: true,
      fillColor: Colors.white,
      border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
    );
  }

  Widget _deviceCard(String deviceId) {
    return Card(
      color: Colors.white,
      elevation: 0,
      margin: const EdgeInsets.only(bottom: 12),
      child: ListTile(
        leading: const Icon(Icons.sensors, color: AppColors.navy),
        title: Text(
          deviceId,
          style: const TextStyle(
            color: AppColors.navy,
            fontWeight: FontWeight.bold,
          ),
        ),
        subtitle: const Text('Linked to your account'),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: _service,
      builder: (context, _) {
        return ListView(
          padding: const EdgeInsets.all(24),
          children: [
            const Text(
              'My devices',
              style: TextStyle(
                color: AppColors.navy,
                fontSize: 28,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 8),
            const Text(
              'Your vehicle, connected.',
              style: TextStyle(color: AppColors.navy, fontSize: 15),
            ),
            const SizedBox(height: 24),
            if (_service.loading)
              const Center(
                child: CircularProgressIndicator(color: AppColors.navy),
              )
            else if (_service.devices.isEmpty)
              Text(
                _service.lastProblem ?? 'No devices linked yet.',
                style: const TextStyle(color: AppColors.navy),
              )
            else
              ..._service.devices.map(_deviceCard),
            const SizedBox(height: 28),
            const Text(
              'Link a device',
              style: TextStyle(
                color: AppColors.navy,
                fontSize: 20,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 14),
            Form(
              key: _formKey,
              child: Column(
                children: [
                  TextFormField(
                    controller: _deviceIdController,
                    autocorrect: false,
                    textInputAction: TextInputAction.next,
                    decoration: _inputStyle('Device ID', Icons.sensors),
                    validator: (value) {
                      return (value ?? '').trim().isEmpty
                          ? 'Enter the device ID'
                          : null;
                    },
                  ),
                  const SizedBox(height: 14),
                  TextFormField(
                    controller: _codeController,
                    autocorrect: false,
                    enableSuggestions: false,
                    textInputAction: TextInputAction.done,
                    onFieldSubmitted: (_) => _link(),
                    decoration: _inputStyle('Pairing code', Icons.pin_outlined),
                    validator: (value) {
                      return (value ?? '').trim().isEmpty
                          ? 'Enter the pairing code'
                          : null;
                    },
                  ),
                ],
              ),
            ),
            const SizedBox(height: 16),
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
              height: 54,
              child: ElevatedButton(
                onPressed: _linking ? null : _link,
                style: ElevatedButton.styleFrom(
                  backgroundColor: AppColors.turquoise,
                  foregroundColor: AppColors.navy,
                  elevation: 0,
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(16),
                  ),
                ),
                child: Text(_linking ? 'Linking...' : 'Link device'),
              ),
            ),
            const SizedBox(height: 14),
            const Text(
              'Bluetooth connection is not active yet.',
              textAlign: TextAlign.center,
              style: TextStyle(color: Color(0xFF65758A), fontSize: 12),
            ),
          ],
        );
      },
    );
  }
}
