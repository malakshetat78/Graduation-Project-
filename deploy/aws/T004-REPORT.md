# T-004: FastAPI and trust engine on AWS Lambda

Owner: Nourseen Tarek. Deployment scripts and Lambda adapter: Shahesta (`shahesta/aws-lambda`, `eea83bb`).
Branch: `nourseen/aws-lambda-deploy`. Date: 2026-11-16.

AWS Lambda replaces Cloud Run for this task because Google Cloud billing could not be activated. The backend
code (`backend/`, `engine/`) is unchanged; Cloud Run remains possible with `cloud/Dockerfile` on
`nourseen/cloud-run-deploy`.

## Deployed endpoint

| | |
|---|---|
| Stack | `icvsp-backend`, account 374617280285, `us-east-1` |
| URL | `https://bamrqyu5hizs6q6dy37wxvlebu0xwtlk.lambda-url.us-east-1.on.aws/` (Lambda function URL, AWS-managed TLS) |
| Runtime | Python 3.12, arm64, 512 MB (about 160 MB used), timeout 15 s |
| Database | team Supabase PostgreSQL via the session pooler, role `icvsp_backend` |
| Secrets | SSM SecureString `/icvsp/database_url`, `/icvsp/admin_token`; read by the function at cold start |
| Trust engine | EventBridge Scheduler, once a minute |
| Logs | CloudWatch `icvsp-backend-BackendLogs-*`, 14 days |

Reproduce: `deploy/aws/README.md` (including the Windows section).

## Results

| Part | What | Where it ran | Result | Evidence |
|---|---|---|---|---|
| A | health, ready, 4 calls without credentials | live URL, read only | **6/6 pass** | `results/t004-partA-*.json`, `results/cloudwatch-partA.txt` |
| B | register, valid, duplicate, stale, bad signature, tampered, unregistered, evaluator, revoke | live URL + shared Supabase (approved minimal plan) | **10/10 pass** | `results/t004-partB-20261116T191409Z.json`, `results/cloudwatch-partB.txt`, `results/test-records.md` |
| C | the team's 188 backend tests incl. restarts, database down then retry, two evaluators at once | local `supabase/postgres:17.11.0.003` in Docker, isolated | **188/188 pass, 0 skipped** | `results/partC-postgres-tests.log` |

### Part B, expected vs actual

| Check | Expected | Actual | ms |
|---|---|---|---|
| B1 register test device | 201 | 201 | 686 |
| B2 valid signed report | 201 accepted | 201 accepted | 1246 |
| B3 same report again | 201 duplicate | 201 duplicate | 1041 |
| B4 3-day-old report | 422 stale | 422 stale | 929 |
| B5 invalid signature | 401 | 401 bad_signature | 851 |
| B6 payload changed after signing | 401 | 401 bad_signature | 3921 |
| B7 unregistered device | 403 | 403 unknown_device | 642 |
| B8 evaluator | evaluated within 180 s | evaluated; hazard `suppressed` | accepted 19:13:53, evaluated by 19:14:02 |
| B9 revoke | 200 | 200 | 1008 |
| B10 device REVOKED | REVOKED | REVOKED | |

Latencies are single requests from a laptop in Egypt to us-east-1, not a performance benchmark. First request
after idle (cold start): 2.7 to 3.9 s; warm requests: 0.5 to 1.3 s.

CloudWatch over the test window: evaluator invoked once a minute, **0 errors**. Evidence files were scanned:
no admin token, database string or private key in any of them.

## Problems found and fixed

| Problem | Effect | Fix |
|---|---|---|
| Scripts checked out with CRLF on Windows | bash refused to run them | `.gitattributes`: `*.sh text eol=lf` |
| Git Bash rewrote `/icvsp/...` into `C:/Program Files/Git/icvsp/...` | AWS refused the parameter (safe failure) | `MSYS2_ARG_CONV_EXCL` / `MSYS_NO_PATHCONV`, documented |
| Git Bash `/tmp` invisible to the Windows AWS CLI | secret upload failed | `TMPDIR` set inside bash, documented |
| Generated admin token ended in a newline on Windows | every admin call refused (fail-closed) | `set_secrets.sh` strips `\r`; token rotated (SSM v2), function reloaded |

## Known limitations

1. **Trust engine runs at most once a minute** (EventBridge minimum). A report can wait up to about 60 s before its
   hazard state is decided (the backend's own default is 10 s). Acceptable for this prototype; a lower delay needs an
   always-on worker (for example Cloud Run with instance-based CPU and one minimum instance, or a container service).
2. **No rate limiting** on the public URL yet (`moaz/rate-limits` is not merged); every rejected request writes a
   `rejections` row, so junk traffic grows the database.
3. **Concurrency cap not set**: the account's Lambda limit is 10, so `deploy.sh` cannot reserve 5; under a flood each
   instance may open up to 2 Supabase connections.
4. **Region**: Lambda is in `us-east-1`; the Supabase region was not confirmed, so database latency may include a long
   network hop.
5. **Shared database**: tests ran on the team database under a minimal approved plan; one revoked test device row stays
   (see `results/test-records.md`).
6. **Not tested here**: load and concurrency on the live stack (excluded by agreement), signed backend-to-phone alerts
   (FR-SEC-15 to 17, not implemented), the ECU sending to this URL over Wi-Fi.

## Rollback and recovery

- Redeploy the previous version: check out the previous commit and run `deploy.sh` again (CloudFormation updates the
  function in place).
- Remove everything: `aws cloudformation delete-stack --stack-name icvsp-backend`, then delete `/icvsp/*` parameters
  and the `icvsp-deploy-*` bucket (README).
- Rotate the admin token: `set_secrets.sh --overwrite`, then update the function configuration to reload it.
- Database: nothing in this task changes the schema; Supabase backups and migrations are managed in `database/`.
