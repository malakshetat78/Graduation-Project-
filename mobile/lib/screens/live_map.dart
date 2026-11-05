import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:geolocator/geolocator.dart';
import 'package:latlong2/latlong.dart';

import '../core/app_colors.dart';

class LiveMap extends StatefulWidget {
  const LiveMap({super.key});

  @override
  State<LiveMap> createState() => _LiveMapState();
}

class _LiveMapState extends State<LiveMap> {
  static final LatLng _cairo = LatLng(30.0444, 31.2357);
  static const double _zoom = 16;

  final MapController _mapController = MapController();

  StreamSubscription<Position>? _positions;
  LatLng? _current;
  bool _mapReady = false;
  bool _following = true;
  String? _problem;

  @override
  void initState() {
    super.initState();
    _startLocation();
  }

  @override
  void dispose() {
    _positions?.cancel();
    _mapController.dispose();
    super.dispose();
  }

  Future<void> _startLocation() async {
    if (!await Geolocator.isLocationServiceEnabled()) {
      _showProblem('Location is turned off.');
      return;
    }

    var permission = await Geolocator.checkPermission();

    if (permission == LocationPermission.denied) {
      permission = await Geolocator.requestPermission();
    }

    if (permission != LocationPermission.always &&
        permission != LocationPermission.whileInUse) {
      _showProblem('Location permission is needed to show your position.');
      return;
    }

    if (!mounted) return;

    // Same settings as BeaconService, because Geolocator shares one stream.
    _positions =
        Geolocator.getPositionStream(
          locationSettings: AndroidSettings(
            accuracy: LocationAccuracy.high,
            distanceFilter: 0,
            intervalDuration: const Duration(seconds: 1),
            // Keeps working if the user declines Google Location Accuracy.
            forceLocationManager: true,
          ),
        ).listen(
          _updatePosition,
          onError: (_) => _showProblem('Location is unavailable.'),
        );
  }

  void _showProblem(String message) {
    if (!mounted) return;

    setState(() {
      _problem = message;
    });
  }

  void _updatePosition(Position position) {
    if (!mounted) return;

    final point = LatLng(position.latitude, position.longitude);

    setState(() {
      _current = point;
      _problem = null;
    });

    if (_following && _mapReady) {
      _mapController.move(point, _mapController.camera.zoom);
    }
  }

  void _centerOnMe() {
    final point = _current;

    if (point == null || !_mapReady) return;

    setState(() {
      _following = true;
    });

    _mapController.move(point, _zoom);
  }

  @override
  Widget build(BuildContext context) {
    final point = _current;

    return ClipRRect(
      borderRadius: BorderRadius.circular(24),
      child: Stack(
        fit: StackFit.expand,
        children: [
          FlutterMap(
            mapController: _mapController,
            options: MapOptions(
              initialCenter: _cairo,
              initialZoom: _zoom,
              onMapReady: () {
                _mapReady = true;

                if (_current != null) {
                  _mapController.move(_current!, _zoom);
                }
              },
              onPositionChanged: (camera, hasGesture) {
                if (hasGesture && _following) {
                  setState(() {
                    _following = false;
                  });
                }
              },
            ),
            children: [
              TileLayer(
                urlTemplate: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
                userAgentPackageName: 'com.example.rovo_mobile',
              ),
              if (point != null)
                MarkerLayer(
                  markers: [
                    Marker(
                      point: point,
                      width: 44,
                      height: 44,
                      child: Container(
                        decoration: BoxDecoration(
                          color: AppColors.turquoise,
                          shape: BoxShape.circle,
                          border: Border.all(color: Colors.white, width: 3),
                        ),
                        child: const Icon(
                          Icons.navigation,
                          color: AppColors.navy,
                          size: 22,
                        ),
                      ),
                    ),
                  ],
                ),
              const RichAttributionWidget(
                attributions: [
                  TextSourceAttribution('OpenStreetMap contributors'),
                ],
              ),
            ],
          ),
          Positioned(
            left: 16,
            bottom: 16,
            child: FloatingActionButton.small(
              heroTag: 'center-on-me',
              tooltip: 'Show my position',
              backgroundColor: Colors.white,
              foregroundColor: AppColors.navy,
              onPressed: point == null ? null : _centerOnMe,
              child: Icon(
                _following ? Icons.my_location : Icons.location_searching,
              ),
            ),
          ),
          if (_problem != null)
            Positioned(
              top: 12,
              left: 12,
              right: 12,
              child: Container(
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: Colors.white,
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Text(
                  _problem!,
                  textAlign: TextAlign.center,
                  style: const TextStyle(color: AppColors.navy),
                ),
              ),
            ),
        ],
      ),
    );
  }
}
