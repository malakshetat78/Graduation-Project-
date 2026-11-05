import logging

# The security log (failed sign-ins, rate-limit refusals) would print a line to stderr for every refused request in
# the tests and split the "... ok" lines that check_traceability.py reads. Tests that check the log use assertLogs.
logging.getLogger("icvsp.security").addHandler(logging.NullHandler())
