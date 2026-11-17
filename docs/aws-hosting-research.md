# Research & Investigation – Cloud Hosting Alternatives to Google Cloud Run (AWS)

**Project:** Trust-Aware Cooperative Vehicle Safety Platform (ICVSP)
**Researcher:** Moaz
**Candidate platform:** AWS (Lambda + Function URL)
**Date of research / sources accessed:** 2026-11-17
**Status:** Documentation research and AWS account setup completed. The project backend is already deployed on AWS Lambda with the team Supabase database (Nourseen's T-004, PR 18, 17/17 live checks, per the project README), which provides practical evidence for most requirements. A cold-start measurement and the cost after the Free Plan ends are still **pending**.

---

## 1. Task

Compare suitable compute hosting alternatives to Cloud Run. Separate ongoing free quotas from trials. Check Python/container support, background execution, security, latency and cost.

## 2. Requirements (ICVSP Backend)

| # | Requirement | Description |
|---|---|---|
| R1 | Python / FastAPI | Run the existing FastAPI backend |
| R2 | Container support | Optional, but preferred for portability |
| R3 | HTTPS | Secure public endpoint |
| R4 | Supabase integration | Outbound connection to Supabase |
| R5 | Env vars / Secrets | Protect DB credentials and API keys |
| R6 | Background execution | Support the background tasks the system needs |
| R7 | Scalability | Handle growth in requests |
| R8 | Monitoring | Logs, errors, resource usage |
| R9 | Latency | Acceptable response time (including cold start) |
| R10 | Cost | Low cost during test/dev |

## 3. Account setup result (AWS)

- A new AWS account was created for this research and the **Free Plan** is active.
- Initial credits: **$100**, with up to **$100 more** from eligible onboarding activities (Lambda web app, Bedrock Playground, RDS/Aurora, DynamoDB, AWS Budgets, $20 each), so up to **$200** in total.
- At the time of inspection: **182 days** remaining, **$0.00** usage for the month, displayed end date **May 16, 2027**.
- **Note:** the live ICVSP backend runs in the team's deployment account, not in this new account. The credits and the 6-month Free Plan described here apply to the new account only; they do not cover the live deployment.

## 4. Ongoing free quotas vs. trials (required separation)

| Type | What | Details | Source |
|---|---|---|---|
| **Ongoing (Always Free)** | AWS Lambda | 1M requests/month + 400,000 GB-seconds/month, does not expire after 6 or 12 months | [AWS Lambda pricing](https://aws.amazon.com/lambda/pricing/) (accessed 2026-11-17) |
| **Ongoing (Always Free)** | Amazon EventBridge Scheduler | 14 million scheduled invocations/month | [EventBridge pricing](https://aws.amazon.com/eventbridge/pricing/) (accessed 2026-11-17) |
| **Trial / credits** | AWS Free Plan credits | $100 at sign-up + up to $100 earned. Plan lasts up to **6 months or until credits run out**, whichever comes first | [AWS Free Tier announcement, Jul 16 2025](https://aws.amazon.com/about-aws/whats-new/2025/07/aws-free-tier-credits-month-free-plan) · [AWS Free Tier](https://aws.amazon.com/free) (accessed 2026-11-17) |
| **Ongoing (baseline)** | Google Cloud Run | 2M requests, 180,000 vCPU-seconds, 360,000 GiB-seconds per month (request-based billing, us-central1 / Tier 1) | [Cloud Run pricing](https://cloud.google.com/run/pricing) (accessed 2026-11-17) |
| **Trial (baseline)** | Google Cloud | $300 free trial credits, valid for 90 days | [Google Cloud free program](https://cloud.google.com/free) (accessed 2026-11-17) |

**Important distinctions**

- The Lambda free tier is **permanent**. The $100–$200 credits are a **trial** that ends at 6 months (the Free Plan expires, or earlier when credits are used up). After that the account must be upgraded to the Paid Plan to keep running.
- Free Plan credits and the 6-month plan apply to **new accounts only** (created on or after July 15, 2025).
- Budget Alerts only notify, they do **not** stop services automatically.

## 5. Comparison

| Criterion | AWS Lambda (+ Function URL) | Google Cloud Run (baseline) |
|---|---|---|
| Python / FastAPI | Yes, through an adapter. **The project uses Mangum** (`deploy/aws/lambda_handler.py`), which turns the Lambda event into a normal web request for FastAPI. The AWS Lambda Web Adapter is an alternative | Yes, runs as a normal Uvicorn container |
| Container support | Yes (container images supported, to be confirmed in practical test; the project deploys a zip package with AWS SAM) | Native |
| HTTPS endpoint | Lambda Function URL (no API Gateway needed) | Built in |
| Scale to zero | Yes | Yes |
| Ongoing free quota | 1M req + 400k GB-s / month | 2M req + 180k vCPU-s + 360k GiB-s / month |
| Trial credits | $100–$200, up to 6 months | $300, 90 days |
| Pricing beyond free | $0.20 / 1M requests + per GB-s. **The project's function runs on Arm (arm64, 512 MB)**: $0.0000133334 per GB-s (x86 is $0.0000166667) | $0.40 / 1M requests + per vCPU-s and GiB-s (Tier 1) |
| Background execution | Work after the response is returned is not reliable on Lambda. **The project does not rely on it**: EventBridge Scheduler invokes the same function every minute to run the trust engine (see R6) | Limited with request-based billing (CPU is throttled outside requests); better with instance-based billing or Cloud Run Jobs / Cloud Scheduler |
| Secrets management | **The project uses SSM Parameter Store SecureStrings** (`/icvsp/database_url`, `/icvsp/admin_token`), read by the function at run time; Secrets Manager is an alternative | Secret Manager |
| Monitoring | CloudWatch | Cloud Logging / Monitoring |
| Latency / cold start | First warm numbers in R9; cold start not measured yet | Not measured |

## 6. Cost estimate (assumption-based, not a bill)

Scenario: 100,000 report requests/month, 512 MB, 200 ms average duration, plus the trust engine run once a minute.

```
Report requests: 100,000 × 0.2 s × 0.5 GB          = 10,000 GB-s
Trust engine:     43,200 runs/month (1 per minute)
                  assume ≤ 1 s each: 43,200 × 1 s × 0.5 GB ≤ 21,600 GB-s
Total compute                                       ≤ 31,600 GB-s   (free: 400,000)
Total Lambda requests                               = 143,200        (free: 1,000,000)
Scheduler invocations                               = 43,200         (free: 14,000,000)
```

- Compute, requests and scheduler invocations are all **within the permanent free quotas**.
- Estimated base cost for this scenario: **$0**.
- **Actual cost so far (deployment account, AWS Cost Explorer, 1–17 Nov 2026):** **$0** for the ICVSP Lambda function, EventBridge Scheduler and CloudWatch.
- **Not included:** API Gateway (not used), data transfer out above the free amount, KMS requests for decrypting the SSM SecureStrings (**check**: covered by the KMS free tier at this volume), region differences. Real cost must keep being confirmed from AWS Billing.

## 7. Requirements-validation checklist

Legend: ✅ validated (documentation and/or project evidence) · 🔶 expected but needs a practical test · ⏳ not started · ⚠️ risk

Project evidence = `deploy/aws/` (template and handler), the project README, `firmware/results/bringup/TEST_CASES.md` and `screenshots/aws_console/`, and the work done by Nourseen (T-004).

| # | Requirement | Result | Evidence / note |
|---|---|---|---|
| R1 | Python / FastAPI | ✅ | FastAPI backend deployed and running on AWS Lambda through Mangum (T-004, PR 18; `deploy/aws/lambda_handler.py`) |
| R2 | Container support | 🔶 | Documented as supported; not tested by us (the project deploys a zip package) |
| R3 | HTTPS | ✅ | Live backend reached over HTTPS with TLS 1.3 and AWS's real certificate (ECU emulator test, TC-15) |
| R4 | Supabase integration | ✅ | Lambda backend uses the team Supabase database; report stored (201) and evaluated by the trust engine |
| R5 | Env vars / Secrets | ✅ | Database URL and admin token are SSM Parameter Store SecureStrings, read by the function with `ssm:GetParameters`; only their names are in environment variables (`deploy/aws/template.yaml`). Standard parameters have no charge |
| R6 | Background execution | ✅ | The trust engine is not post-response work: EventBridge Scheduler invokes the function every minute with `{"icvsp_task": "evaluate"}` (`deploy/aws/template.yaml`). CloudWatch shows it running at 19:50:56, 19:51:58 and 19:52:56 UTC on 16 Nov, and the ECU's report was evaluated (TC-17) |
| R7 | Scalability | ✅ | Lambda scales automatically (documentation) |
| R8 | Monitoring | ✅ | CloudWatch log of `POST /reports 201` captured (`screenshots/aws_console/`) |
| R9 | Latency / cold start | 🔶 | First warm numbers: the ECU's report request took 683 ms end to end (CloudWatch, 16 Nov); `/health` answered in 0.63–0.76 s from a laptop (17 Nov). Cold start not measured yet |
| R10 | Cost | ✅ | Estimated $0 in the test scenario (section 6); actual $0 for the ICVSP services, 1–17 Nov (Cost Explorer) |
| — | Security checks on live backend | ✅ | Valid / duplicate / stale / bad signature / tampered / unregistered / revoked reports handled correctly (17/17) |
| — | Separate ongoing quota vs trial | ✅ | Section 4 |
| — | Dated source links | ✅ | Section 9 |

## 8. Outputs so far

1. Hosting requirements for ICVSP defined.
2. AWS services reviewed: Lambda, Function URL, API Gateway, ECS/Fargate, EventBridge Scheduler, CloudWatch, SSM Parameter Store, Secrets Manager.
3. Architecture as deployed: Client → Lambda Function URL → Lambda (FastAPI via Mangum) → Supabase; EventBridge Scheduler → same Lambda every minute (trust engine); secrets in SSM Parameter Store; logs in CloudWatch; the function's IAM policy allows reading only the two parameters it needs.
4. AWS account created, Free Plan and $100 initial credit confirmed.
5. Ongoing free quota separated from trial credits.
6. Cost scenario calculated (≈ $0 in free tier), and actual cost so far checked ($0 for the ICVSP services).
7. Cost control plan: Budgets alert at $5, Cost Explorer, Credits dashboard.

## 9. Sources (all accessed 2026-11-17)

- AWS Free Tier: https://aws.amazon.com/free
- AWS Free Tier announcement (Jul 16, 2025): https://aws.amazon.com/about-aws/whats-new/2025/07/aws-free-tier-credits-month-free-plan
- AWS News Blog, Free Tier update: https://aws.amazon.com/blogs/aws/aws-free-tier-update-new-customers-can-get-started-and-explore-aws-with-up-to-200-in-credits/
- AWS hands-on tutorial, control costs: https://docs.aws.amazon.com/hands-on/latest/control-your-costs-free-tier-budgets/control-your-costs-free-tier-budgets.md
- AWS Lambda pricing: https://aws.amazon.com/lambda/pricing/
- Amazon EventBridge pricing (Scheduler): https://aws.amazon.com/eventbridge/pricing/
- Google Cloud Run pricing: https://cloud.google.com/run/pricing
- Google Cloud Run overview: https://cloud.google.com/run
- Google Cloud free program: https://cloud.google.com/free
- Project deployment: `deploy/aws/template.yaml`, `deploy/aws/lambda_handler.py`

## 10. Conclusion

- **AWS Lambda + Function URL is a viable low-cost option** for the ICVSP backend in the test phase, and it is the one in use. It has a **permanent** free quota (1M requests, 400k GB-s; Scheduler 14M invocations) and, for new accounts, a **6-month credit trial** ($100–$200) on top.
- The two must not be confused: after a Free Plan ends, only the permanent quotas remain, and other services (CloudWatch above its free amount, Secrets Manager if used, etc.) may start billing.
- **Background work is handled** without relying on post-response execution: the trust engine runs every minute through EventBridge Scheduler on the same function. This only needs revisiting if evaluation must happen faster than once a minute or take longer than the function's timeout.
- **Status:** the research, the free-quota/trial separation and the requirements checklist are **Passed**. The project already runs on AWS Lambda + Supabase with live checks passing (HTTPS, signed-report security, trust-engine evaluation, CloudWatch). **Still pending:** a cold-start measurement.

### Next steps
1. Configure an AWS Budget ($5 alert) on the deployment account and keep recording the actual cost from Billing.
2. Measure cold start (first request after the function has been idle) and repeat the latency numbers.
3. Confirm the KMS cost of decrypting the SSM SecureStrings at this volume.
4. Add rate limiting (listed as a roadmap item in the project README).
5. Update this file with the measured numbers.

> Note: the project README roadmap still lists "deploy to AWS" while the status table says it is deployed; worth aligning before submission.
