"""Raised by a store when the database cannot be reached: the phone should keep the report and retry (HTTP 503)."""


class StoreUnavailable(Exception):
    pass
