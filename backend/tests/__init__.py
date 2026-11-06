import logging
import os

# The tests build the app themselves. Settings the shell happens to have (a real SUPABASE_URL for local work, a rate
# limit tuned for a demo, ...) would change what they test, or make create_app refuse to start (sign-in together
# with anonymous mode), so they are removed here. The checks in create_app are not touched.
for _name in list(os.environ):
    if _name in {"SUPABASE_URL", "SUPABASE_JWT_SECRET", "ICVSP_ALLOW_ANONYMOUS", "ICVSP_ADMIN_TOKEN",
                 "ICVSP_TRUST_PROXY_HEADERS", "ICVSP_TRUSTED_PROXY_HOPS"} or _name.startswith("ICVSP_RATE_"):
        del os.environ[_name]

# The security log (failed sign-ins, rate-limit refusals) would print a line to stderr for every refused request in
# the tests and split the "... ok" lines that check_traceability.py reads. Tests that check the log use assertLogs.
logging.getLogger("icvsp.security").addHandler(logging.NullHandler())
