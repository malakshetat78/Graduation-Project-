"""Supabase Auth tokens. Sign-up, sign-in, sign-out, e-mail confirmation and password reset happen between the app
and Supabase; the backend only checks the token the app sends with each call. The role is read from public.profiles
on every call, never from the token, so a role change or a revoked admin takes effect at once."""
from __future__ import annotations

import ssl
from dataclasses import dataclass

import certifi
import jwt


class InvalidToken(Exception):
    pass


class AuthUnavailable(Exception):
    """The signing keys could not be fetched: not the user's fault, so no 401 (which would sign them out)."""


@dataclass(frozen=True)
class User:
    id: str
    role: str


class SupabaseVerifier:
    """Checks signature, expiry, audience and issuer. Asymmetric keys from the project's JWKS endpoint, or the
    legacy shared secret if that is what the project still uses. One method per verifier, so the token's own
    `alg` header can never choose a weaker one."""

    def __init__(self, supabase_url: str, jwt_secret: str | None = None, audience: str = "authenticated"):
        base = supabase_url.rstrip("/")
        self.issuer, self.audience, self.secret = f"{base}/auth/v1", audience, jwt_secret
        self.jwks = None if jwt_secret else jwt.PyJWKClient(f"{self.issuer}/.well-known/jwks.json", timeout=5,
                                                          # Python on macOS has no system certificates of its own
                                                          ssl_context=ssl.create_default_context(cafile=certifi.where()))

    def verify(self, token: str) -> str:
        """Returns the user's ID."""
        try:
            if self.secret:
                key, algorithms = self.secret, ["HS256"]
            else:
                key, algorithms = self.jwks.get_signing_key_from_jwt(token).key, ["ES256", "RS256"]
            claims = jwt.decode(token, key, algorithms=algorithms, audience=self.audience, issuer=self.issuer,
                                options={"require": ["exp", "sub", "aud", "iss"]})
        except jwt.PyJWKClientConnectionError as e:
            raise AuthUnavailable(str(e)) from e
        except jwt.PyJWTError as e:
            raise InvalidToken(str(e)) from e
        return claims["sub"]
