import sys
import struct
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from receive_ble import Reassembler, save_report

class ReceiverTests(unittest.TestCase):
    def test_exact_signed_bytes(self):
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        from cryptography.exceptions import InvalidSignature
        key = Ed25519PrivateKey.generate()
        body = b'T' * 642  # Synthetic test fixture, NOT firmware v2 report format.
        signed = body + key.sign(body)
        self.assertEqual(len(signed), 706)
        for room in (16,178,240):
            receiver = Reassembler()
            chunks = [signed[i:i+room] for i in range(0,len(signed),room)]
            result = None
            for i in reversed(range(len(chunks))):
                result = receiver.push(struct.pack('>HBB',1,i,len(chunks))+chunks[i],0)
            self.assertEqual(result,(1,signed))
            key.public_key().verify(result[1][-64:],result[1][:-64])
        changed = bytearray(signed); changed[0] ^= 1
        with self.assertRaises(InvalidSignature):
            key.public_key().verify(bytes(changed[-64:]),bytes(changed[:-64]))
    def test_partial_retries_and_timeout(self):
        r=Reassembler()
        a=struct.pack('>HBB',1,0,2)+b'a'
        b=struct.pack('>HBB',1,1,2)+b'b'
        self.assertIsNone(r.push(a,0))
        self.assertIsNone(r.push(a,1))
        self.assertEqual(r.push(b,2),(1,b'ab'))
        r.push(a,3)
        self.assertIsNone(r.push(b,20))
    def test_invalid_and_conflicting(self):
        r=Reassembler()
        for raw in (b'',struct.pack('>HBB',1,2,2)+b'x',struct.pack('>HBB',1,0,0)+b'x'):
            with self.assertRaises(ValueError): r.push(raw,0)
        r.push(struct.pack('>HBB',2,0,2)+b'a',0)
        with self.assertRaises(ValueError): r.push(struct.pack('>HBB',2,0,2)+b'b',1)
    def test_durable_duplicate(self):
        with tempfile.TemporaryDirectory() as d:
            p,duplicate=save_report(d,b'signed opaque bytes')
            self.assertFalse(duplicate)
            self.assertEqual(p.read_bytes(),b'signed opaque bytes')
            self.assertTrue(save_report(d,b'signed opaque bytes')[1])
            self.assertEqual(len(list(Path(d).glob('*.bin'))),1)
    def test_save_failure(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'not-directory'; p.write_text('x')
            with self.assertRaises(OSError): save_report(p,b'test')
if __name__=='__main__': unittest.main()
