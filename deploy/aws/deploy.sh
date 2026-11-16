#!/usr/bin/env bash
# Builds the package and deploys the ICVSP backend stack (Lambda + function URL + 1-minute schedule).
#   AWS_PROFILE=icvsp SUPABASE_URL=https://<project>.supabase.co deploy/aws/deploy.sh
# Needs: AWS CLI v2 signed in to the TEAM account, python3, and the two secrets from set_secrets.sh.
set -euo pipefail
: "${AWS_PROFILE:?set AWS_PROFILE to the team account profile (aws configure --profile icvsp)}"
: "${SUPABASE_URL:?set SUPABASE_URL=https://<project>.supabase.co}"
REGION="${AWS_REGION:-$(aws configure get region || echo us-east-1)}"
STACK="${STACK:-icvsp-backend}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

ACCOUNT="$(aws sts get-caller-identity --query Account --output text)"
echo "Deploying stack $STACK to account $ACCOUNT, region $REGION (profile $AWS_PROFILE)."
read -r -p "Is this the ICVSP team account? [y/N] " ok; [ "$ok" = y ] || exit 1

for p in /icvsp/database_url /icvsp/admin_token; do
    aws ssm describe-parameters --region "$REGION" --parameter-filters "Key=Name,Values=$p" \
        --query 'length(Parameters)' --output text | grep -q '^1$' || { echo "missing $p: run set_secrets.sh first"; exit 1; }
done

"$ROOT/deploy/aws/build.sh"

BUCKET="icvsp-deploy-$ACCOUNT-$REGION"  # holds only the code package
if ! aws s3api head-bucket --bucket "$BUCKET" 2>/dev/null; then
    if [ "$REGION" = us-east-1 ]; then aws s3api create-bucket --bucket "$BUCKET" --region "$REGION" >/dev/null
    else aws s3api create-bucket --bucket "$BUCKET" --region "$REGION" \
        --create-bucket-configuration "LocationConstraint=$REGION" >/dev/null; fi
    aws s3api put-public-access-block --bucket "$BUCKET" --public-access-block-configuration \
        BlockPublicAcls=true,IgnorePublicAcls=true,BlockPublicPolicy=true,RestrictPublicBuckets=true
    aws s3api put-bucket-lifecycle-configuration --bucket "$BUCKET" --lifecycle-configuration \
        '{"Rules":[{"ID":"old-packages","Status":"Enabled","Filter":{},"Expiration":{"Days":30}}]}'
fi

# Cap parallel instances (and so database connections, and cost under a request flood) where the account allows:
# AWS keeps at least 10 unreserved, so an account limited to 10 cannot reserve any.
LIMIT="$(aws lambda get-account-settings --region "$REGION" --query AccountLimit.ConcurrentExecutions --output text)"
RESERVED=0; [ "$LIMIT" -ge 15 ] && RESERVED=5
[ "$RESERVED" = 0 ] && echo "note: account Lambda concurrency limit is $LIMIT, so no reserved concurrency is set"

aws cloudformation package --region "$REGION" --template-file "$ROOT/deploy/aws/template.yaml" \
    --s3-bucket "$BUCKET" --output-template-file "$ROOT/build/packaged.yaml" >/dev/null
aws cloudformation deploy --region "$REGION" --stack-name "$STACK" --template-file "$ROOT/build/packaged.yaml" \
    --capabilities CAPABILITY_IAM CAPABILITY_AUTO_EXPAND --parameter-overrides "SupabaseUrl=$SUPABASE_URL" "ReservedConcurrency=$RESERVED" \
    --tags project=icvsp
URL="$(aws cloudformation describe-stacks --region "$REGION" --stack-name "$STACK" \
    --query "Stacks[0].Outputs[?OutputKey=='BackendUrl'].OutputValue" --output text)"
echo "Backend URL: $URL"
echo "Check: curl ${URL}ready"
