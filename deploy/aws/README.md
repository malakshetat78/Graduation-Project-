# ICVSP backend on AWS Lambda

Proposal for T-004 (backend deployment, owned by Nourseen), with AWS in place of Cloud Run. Tested on a laptop
in AWS's Lambda container image; **not deployed to AWS yet**.

The backend in `backend/` (FastAPI) runs on AWS Lambda with a public HTTPS address (a Lambda function URL).
The code in `backend/` and `engine/` is not changed: `lambda_handler.py` passes each web request to the same
FastAPI app that runs on a laptop with uvicorn. The database stays the team's Supabase PostgreSQL.

```
ECU / phone --HTTPS--> Lambda function URL --> FastAPI app (backend/) --> Supabase PostgreSQL
                       EventBridge Scheduler, every minute --> trust engine on pending reports
```

| Part | AWS service | Cost at our traffic |
|---|---|---|
| Backend code | Lambda, Python 3.12, arm64, 512 MB | free tier: 1 million requests and 400,000 GB-seconds a month |
| Public HTTPS address | Lambda function URL | no extra charge |
| Trust engine every minute | EventBridge Scheduler | free tier: 14 million calls a month |
| Database password, admin token | SSM Parameter Store, SecureString | standard parameters are free |
| Logs | CloudWatch Logs, kept 14 days | free tier: 5 GB a month |
| Code package | S3 bucket (deleted after 30 days) | a few cents at most |

Check the current prices and free-tier terms for your account in the AWS console; they change over time.
Set a billing alert (Billing > Budgets, e.g. $1) before deploying.

## What you need

- An AWS **account** for the team. An AWS Builder ID is only a sign-in for developer tools (Amazon Q,
  CodeCatalyst); it cannot create Lambda functions.
- AWS CLI v2 signed in to that account: `aws configure --profile icvsp` (or `aws configure sso`).
- The `icvsp_backend` database role on Supabase (`database/README.md`) and its **session pooler** connection
  string (Supabase: Connect > Session pooler). Lambda connects over IPv4, which the pooler supports.

## Deploy

```bash
export AWS_PROFILE=icvsp
deploy/aws/set_secrets.sh                                         # 1. paste the connection string (hidden)
SUPABASE_URL=https://<project>.supabase.co deploy/aws/deploy.sh   # 2. build and deploy, prints the URL
curl https://<id>.lambda-url.<region>.on.aws/ready                # 3. {"status":"ready",...}
deploy/aws/register_device.sh https://<id>.lambda-url.<region>.on.aws <device_id> <public_key>   # 4.
```

Then set the ECU's report URL to `https://<id>.lambda-url.<region>.on.aws/reports`
(`CONFIG_ICVSP_REPORT_URL` in `firmware/ecu/sdkconfig.local`), rebuild and flash.

Delete everything again: `aws cloudformation delete-stack --stack-name icvsp-backend`, then the
`/icvsp/*` parameters and the `icvsp-deploy-*` bucket.

## Safety

- Without a database the function refuses to start: Lambda's disk is temporary, so a report the backend
  has acknowledged as stored must be in PostgreSQL, never in a local file.
- `ICVSP_ALLOW_ANONYMOUS` is never set here; user sign-in is checked against `SUPABASE_URL`.
- The function URL is public, like any web API; reports are only stored when the signature matches a
  registered device key, and device registration needs the admin token.
- Each Lambda instance opens at most two database connections; `deploy.sh` caps the function at 5 instances
  (10 connections) when the account's concurrency limit allows it. On a new account limited to 10 it cannot,
  so a request flood could use more connections: ask AWS for a higher limit, or watch the Supabase connection count.
- Known backend gap (not changed here): every rejected report, even an unsigned one, is logged as a database
  row for 90 days, so junk traffic grows the database. Rate limiting is on the backend roadmap
  (branch `moaz/rate-limits`).

## Test without AWS

The package runs in AWS's own Lambda container image on a laptop (Docker), with a throw-away SQLite file:

```bash
deploy/aws/build.sh
docker run --rm --platform linux/arm64 -p 127.0.0.1:9011:8080 -v "$PWD/build/lambda:/var/task:ro" \
  -e ICVSP_LOCAL_TEST=1 -e ICVSP_ALLOW_ANONYMOUS=1 -e ICVSP_ADMIN_TOKEN=localtest \
  public.ecr.aws/lambda/python:3.12 lambda_handler.handler
```

Requests go to `http://127.0.0.1:9011/2015-03-31/functions/function/invocations` as function-URL events.

Then, in a second terminal: `python3 deploy/aws/local_test.py` (needs `pip install cryptography`).
