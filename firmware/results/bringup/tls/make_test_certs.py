"""Makes throw-away TLS test certificates for the ECU's HTTPS tests (BRINGUP_CHECKLIST.md section 4c).

usage: python make_test_certs.py <out dir, normally firmware/ecu/tls_test>     (needs the `cryptography` package,
       which the backend already uses)

Writes (all git-ignored, test material only, never used outside the laptop):
  ca.pem                          the test CA; the HTTPS test firmware trusts only this
  good.{crt,key}                  signed by the test CA, for 10.0.2.2 (the laptop as seen from QEMU) and 127.0.0.1
  wrong_host.{crt,key}            signed by the test CA, but for wrong-host.example only     -> must be refused
  expired.{crt,key}               signed by the test CA, for 10.0.2.2, valid 2020-2021 only  -> must be refused
  untrusted.{crt,key}             for 10.0.2.2, signed by another CA the firmware does not know -> must be refused
"""
import datetime as dt
import ipaddress
import sys
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
now = dt.datetime.now(dt.timezone.utc)


def name(cn):
    return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn)])


def make_ca(cn):
    key = ec.generate_private_key(ec.SECP256R1())
    cert = (x509.CertificateBuilder().subject_name(name(cn)).issuer_name(name(cn)).public_key(key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now - dt.timedelta(days=1))
            .not_valid_after(now + dt.timedelta(days=30))
            .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
            .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
            .add_extension(x509.KeyUsage(digital_signature=False, content_commitment=False, key_encipherment=False,
                                         data_encipherment=False, key_agreement=False, key_cert_sign=True,
                                         crl_sign=True, encipher_only=False, decipher_only=False), critical=True)
            .sign(key, hashes.SHA256()))
    return key, cert


def make_server(stem, ca_key, ca_cert, sans, start, end):
    key = ec.generate_private_key(ec.SECP256R1())
    cert = (x509.CertificateBuilder().subject_name(name("icvsp-test-server")).issuer_name(ca_cert.subject)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(start).not_valid_after(end)
            .add_extension(x509.SubjectAlternativeName(sans), critical=False)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
            .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
            .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_key.public_key()), critical=False)
            .sign(ca_key, hashes.SHA256()))
    (out / f"{stem}.crt").write_bytes(cert.public_bytes(serialization.Encoding.PEM)
                                      + ca_cert.public_bytes(serialization.Encoding.PEM))
    (out / f"{stem}.key").write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                                        serialization.NoEncryption()))
    (out / f"{stem}.key").chmod(0o600)


lan = [x509.IPAddress(ipaddress.ip_address("10.0.2.2")), x509.IPAddress(ipaddress.ip_address("127.0.0.1"))]
ca_key, ca = make_ca("ICVSP TEST CA (laptop only)")
other_key, other = make_ca("Some other CA")
(out / "ca.pem").write_bytes(ca.public_bytes(serialization.Encoding.PEM))
valid = (now - dt.timedelta(days=1), now + dt.timedelta(days=30))
make_server("good", ca_key, ca, lan, *valid)
make_server("wrong_host", ca_key, ca, [x509.DNSName("wrong-host.example")], *valid)
make_server("expired", ca_key, ca, lan, dt.datetime(2020, 1, 1, tzinfo=dt.timezone.utc),
            dt.datetime(2021, 1, 1, tzinfo=dt.timezone.utc))
make_server("untrusted", other_key, other, lan, *valid)
print("wrote test CA and 4 server certificates to", out)
