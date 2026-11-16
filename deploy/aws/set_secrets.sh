#!/usr/bin/env bash
# Stores the two secrets as encrypted SSM parameters (SecureString). Nothing is printed or written to disk
# for longer than the upload. Run it yourself: the database password never goes into a chat or a file.
#   AWS_PROFILE=icvsp deploy/aws/set_secrets.sh            (add --overwrite to replace existing values)
set -euo pipefail
: "${AWS_PROFILE:?set AWS_PROFILE to the team account profile}"
REGION="${AWS_REGION:-$(aws configure get region || echo us-east-1)}"
OVERWRITE=(); [ "${1:-}" = --overwrite ] && OVERWRITE=(--overwrite)
umask 077
TMP="$(mktemp)"; trap 'rm -f "$TMP"' EXIT

put() {  # name, value file
    aws ssm put-parameter --region "$REGION" --name "$1" --type SecureString --value "file://$2" \
        ${OVERWRITE[@]+"${OVERWRITE[@]}"} --tags Key=project,Value=icvsp >/dev/null 2>&1 \
    || aws ssm put-parameter --region "$REGION" --name "$1" --type SecureString --value "file://$2" \
        ${OVERWRITE[@]+"${OVERWRITE[@]}"} >/dev/null
    echo "stored $1"
}

echo "Paste the Supabase SESSION POOLER connection string for the restricted backend role"
echo "(Supabase: Connect > Session pooler; postgresql://icvsp_backend.<project>:<password>@...pooler.supabase.com:5432/postgres)."
read -r -s -p "connection string (hidden): " DB; echo
case "$DB" in postgresql://*|postgres://*) ;; *) echo "that does not look like a postgresql:// URL"; exit 1;; esac
printf '%s' "$DB" > "$TMP"; unset DB
put /icvsp/database_url "$TMP"

if [ ${#OVERWRITE[@]} -gt 0 ] || ! aws ssm get-parameter --region "$REGION" --name /icvsp/admin_token >/dev/null 2>&1; then
    openssl rand -hex 32 | tr -d '\n' > "$TMP"
    put /icvsp/admin_token "$TMP"
    echo "(admin token generated; read it in the AWS console > Systems Manager > Parameter Store when needed)"
fi
