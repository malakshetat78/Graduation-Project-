import 'package:flutter/material.dart';

import '../core/app_colors.dart';
import '../services/api_service.dart';

class HazardsScreen extends StatefulWidget {
  const HazardsScreen({super.key});

  @override
  State<HazardsScreen> createState() => _HazardsScreenState();
}

class _HazardsScreenState extends State<HazardsScreen> {
  final ApiService _apiService = ApiService();

  String _selectedFilter = 'All';
  bool _loading = true;
  String? _error;
  List<Map<String, String>> _hazards = [];

  @override
  void initState() {
    super.initState();
    _loadHazards();
  }

  Future<void> _loadHazards() async {
    setState(() {
      _loading = true;
      _error = null;
    });

    try {
      final hazards = await _apiService.getHazards();

      if (!mounted) return;

      setState(() {
        _hazards = hazards;
      });
    } catch (_) {
      if (!mounted) return;

      setState(() {
        _error = 'Could not load hazards. Check your connection and try again.';
      });
    } finally {
      if (mounted) {
        setState(() {
          _loading = false;
        });
      }
    }
  }

  Widget _statusBadge(String status) {
    final underReview = status == 'Under review';

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 7),
      decoration: BoxDecoration(
        color: underReview ? const Color(0xFFFFEAD0) : const Color(0xFFD8F8EF),
        borderRadius: BorderRadius.circular(20),
      ),
      child: Text(
        status,
        style: const TextStyle(color: AppColors.navy, fontSize: 12),
      ),
    );
  }

  void _showDetails(Map<String, String> hazard) {
    showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      showDragHandle: true,
      backgroundColor: AppColors.cream,
      builder: (context) {
        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(24, 8, 24, 28),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Icon(
                  Icons.warning_amber_rounded,
                  color: AppColors.coral,
                  size: 52,
                ),
                const SizedBox(height: 16),
                Text(
                  hazard['title']!,
                  style: const TextStyle(
                    color: AppColors.navy,
                    fontSize: 26,
                    fontWeight: FontWeight.bold,
                  ),
                ),
                const SizedBox(height: 12),
                Text(
                  'Example distance: ${hazard['distance']}',
                  style: const TextStyle(color: AppColors.navy, fontSize: 16),
                ),
                const SizedBox(height: 12),
                _statusBadge(hazard['status']!),
                const SizedBox(height: 20),
                const Text(
                  'Demo data received from FastAPI. '
                  'Distances and verification statuses are examples.',
                  style: TextStyle(color: Color(0xFF65758A), height: 1.5),
                ),
                const SizedBox(height: 24),
                SizedBox(
                  width: double.infinity,
                  child: FilledButton(
                    onPressed: () => Navigator.pop(context),
                    style: FilledButton.styleFrom(
                      backgroundColor: AppColors.turquoise,
                      foregroundColor: AppColors.navy,
                      padding: const EdgeInsets.all(16),
                    ),
                    child: const Text('Close'),
                  ),
                ),
              ],
            ),
          ),
        );
      },
    );
  }

  Widget _messageCard({
    required IconData icon,
    required String message,
    bool showRetry = false,
  }) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 36),
      child: Column(
        children: [
          Icon(icon, size: 52, color: AppColors.navy),
          const SizedBox(height: 16),
          Text(
            message,
            textAlign: TextAlign.center,
            style: const TextStyle(color: AppColors.navy, height: 1.5),
          ),
          if (showRetry) ...[
            const SizedBox(height: 16),
            FilledButton(
              onPressed: _loadHazards,
              style: FilledButton.styleFrom(
                backgroundColor: AppColors.turquoise,
                foregroundColor: AppColors.navy,
              ),
              child: const Text('Try again'),
            ),
          ],
        ],
      ),
    );
  }

  Widget _hazardCard(Map<String, String> hazard) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 16),
      child: Material(
        color: Colors.white,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(20),
          side: const BorderSide(color: Color(0xFFE1E5EA)),
        ),
        clipBehavior: Clip.antiAlias,
        child: InkWell(
          onTap: () => _showDetails(hazard),
          child: Padding(
            padding: const EdgeInsets.all(18),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Icon(
                  Icons.warning_amber_rounded,
                  color: AppColors.coral,
                  size: 36,
                ),
                const SizedBox(width: 14),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        hazard['title']!,
                        style: const TextStyle(
                          color: AppColors.navy,
                          fontSize: 18,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const SizedBox(height: 6),
                      Text(
                        '${hazard['distance']} · Demo distance',
                        style: const TextStyle(color: AppColors.navy),
                      ),
                      const SizedBox(height: 12),
                      _statusBadge(hazard['status']!),
                    ],
                  ),
                ),
                const Icon(Icons.chevron_right, color: AppColors.navy),
              ],
            ),
          ),
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final visibleHazards = _hazards.where((hazard) {
      return _selectedFilter == 'All' || hazard['type'] == _selectedFilter;
    }).toList();

    return RefreshIndicator(
      onRefresh: _loading ? () async {} : _loadHazards,
      color: AppColors.navy,
      child: ListView(
        physics: const AlwaysScrollableScrollPhysics(),
        padding: const EdgeInsets.all(24),
        children: [
          Row(
            children: [
              const Expanded(
                child: Text(
                  'Nearby hazards',
                  style: TextStyle(
                    color: AppColors.navy,
                    fontSize: 28,
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ),
              IconButton(
                tooltip: 'Refresh hazards',
                onPressed: _loading ? null : _loadHazards,
                icon: const Icon(Icons.refresh),
                color: AppColors.navy,
              ),
            ],
          ),
          const SizedBox(height: 8),
          const Text(
            'Stay aware of the road ahead.',
            style: TextStyle(color: AppColors.navy),
          ),
          const SizedBox(height: 8),
          const Text(
            'Demo mode · Sample data from FastAPI',
            style: TextStyle(color: Color(0xFF65758A), fontSize: 12),
          ),
          const SizedBox(height: 22),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: ['All', 'Potholes', 'Obstacles'].map((filter) {
              return ChoiceChip(
                label: Text(filter),
                selected: _selectedFilter == filter,
                selectedColor: AppColors.turquoise,
                backgroundColor: Colors.white,
                labelStyle: const TextStyle(color: AppColors.navy),
                onSelected: (selected) {
                  if (!selected) return;

                  setState(() {
                    _selectedFilter = filter;
                  });
                },
              );
            }).toList(),
          ),
          const SizedBox(height: 24),
          if (_loading)
            const Padding(
              padding: EdgeInsets.all(40),
              child: Center(
                child: CircularProgressIndicator(color: AppColors.navy),
              ),
            )
          else if (_error != null)
            _messageCard(
              icon: Icons.cloud_off_outlined,
              message: _error!,
              showRetry: true,
            )
          else if (visibleHazards.isEmpty)
            _messageCard(
              icon: Icons.map_outlined,
              message: _hazards.isEmpty
                  ? 'No hazards returned by the server.'
                  : 'No hazards match this filter.',
            )
          else
            ...visibleHazards.map(_hazardCard),
        ],
      ),
    );
  }
}
