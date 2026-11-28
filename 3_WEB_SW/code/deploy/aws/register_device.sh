#!/usr/bin/env bash
# Registers a device's public key with the deployed backend (the ECU prints both at boot:
# "DEMO device=<id> public_key=<64 hex>"). The admin token is read from SSM and never printed.
#   AWS_PROFILE=icvsp deploy/aws/register_device.sh https://<id>.lambda-url.<region>.on.aws/ <device_id> <public_key_hex>
set -euo pipefail
[ $# -eq 3 ] || { echo "usage: $0 <backend_url> <device_id> <public_key_hex>"; exit 1; }
URL="${1%/}"; ID="$2"; PK="$3"
[[ "$PK" =~ ^[0-9a-f]{64}$ ]] || { echo "public key must be 64 lowercase hex characters"; exit 1; }
[[ "$ID" =~ ^[A-Za-z0-9._-]{1,64}$ ]] || { echo "unexpected device id"; exit 1; }
REGION="${AWS_REGION:-$(aws configure get region || echo us-east-1)}"
TOKEN="$(aws ssm get-parameter --region "$REGION" --name /icvsp/admin_token --with-decryption --query Parameter.Value --output text)"
# The header goes to curl on stdin (printf is a shell builtin), so the token never appears in a process list.
printf 'header = "X-Admin-Token: %s"\n' "$TOKEN" | curl -sS -K - -X POST "$URL/devices" -H 'Content-Type: application/json' \
    -d "{\"device_id\":\"$ID\",\"public_key\":\"$PK\",\"vetted\":false}" | sed 's/"pairing_code":"[^"]*"/"pairing_code":"****"/'
echo
