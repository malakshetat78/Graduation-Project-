#!/usr/bin/env bash
# Builds the Lambda package in build/lambda/: the backend and the trust engine unchanged, plus Linux arm64
# wheels of their dependencies (downloaded for Lambda's platform, so this works from a Mac).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/lambda"
rm -rf "$OUT" && mkdir -p "$OUT"
python3 -m pip install --quiet --target "$OUT" --implementation cp --python-version 3.12 --only-binary=:all: \
    --platform manylinux2014_aarch64 --platform manylinux_2_28_aarch64 \
    -r "$ROOT/backend_sw/requirements.txt" -r "$ROOT/deploy/aws/requirements-lambda.txt"
cp -R "$ROOT/backend_sw/icvsp_backend" "$ROOT/engine/icvsp" "$OUT/"
cp "$ROOT/deploy/aws/lambda_handler.py" "$OUT/"
find "$OUT" -name __pycache__ -type d -prune -exec rm -rf {} +
echo "built $OUT ($(du -sh "$OUT" | cut -f1))"
