"""Run after C++ writes packet.bin; confirms that tampered packets are rejected."""
from pathlib import Path
from verify_packet import verify_and_decrypt, PacketRejected


keys = Path('keys')
original = Path('packet.bin').read_bytes()
report = verify_and_decrypt(original, keys)
assert report['packet_bytes'] == 160
assert report['hazard_type'] == 'Pothole'
print('PASS: original 160-byte C++ packet verified + decrypted')

for name, byte_index in [
    ('header (nonce)', 22),
    ('ciphertext', 48),
    ('authentication tag', 81),
    ('signature', 100),
]:
    changed = bytearray(original)
    changed[byte_index] ^= 1
    try:
        verify_and_decrypt(bytes(changed), keys)
    except PacketRejected:
        print(f'PASS: changed {name} rejected')
    else:
        raise AssertionError(f'FAIL: tampered {name} was accepted')
print('PASS: all Phase 1 checks successful')
