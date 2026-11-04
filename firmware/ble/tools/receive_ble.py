"""Bench receiver: opaque BLE frames -> durable file -> ACK. Not Flutter/backend."""
import argparse
import asyncio
import hashlib
import os
from pathlib import Path
import struct
import time

SERVICE = '7b100001-6a34-4c82-9b51-2f048e9a6300'
REPORT = '7b100002-6a34-4c82-9b51-2f048e9a6300'
ACK = '7b100003-6a34-4c82-9b51-2f048e9a6300'

class Reassembler:
    def __init__(self):
        self.partial = {}
    def push(self, frame, now=None):
        now = time.monotonic() if now is None else now
        self.partial = {k:v for k,v in self.partial.items() if now-v[0] <= 15}
        if len(frame) <= 4 or len(frame) > 244:
            raise ValueError('invalid frame length')
        message, index, total = struct.unpack('>HBB', frame[:4])
        if not message or not total or index >= total or total > 128:
            raise ValueError('invalid header')
        if message not in self.partial:
            if len(self.partial) >= 8:
                raise ValueError('too many partial reports')
            self.partial[message] = (now, total, {})
        _, expected, parts = self.partial[message]
        if expected != total:
            del self.partial[message]
            raise ValueError('fragment geometry changed; reconnect')
        chunk = bytes(frame[4:])
        if index in parts and parts[index] != chunk:
            del self.partial[message]
            raise ValueError('conflicting fragment')
        parts[index] = chunk
        if sum(map(len, parts.values())) > 2048:
            del self.partial[message]
            raise ValueError('report too large')
        if len(parts) != total:
            return None
        del self.partial[message]
        return message, b''.join(parts[i] for i in range(total))

def save_report(directory, payload):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256(payload).hexdigest()
    final = directory / (digest + '.bin')
    if final.exists():
        if final.read_bytes() != payload:
            raise ValueError('existing file content mismatch')
        return final, True
    temporary = directory / (digest + '.tmp')
    with temporary.open('wb') as f:
        f.write(payload)
        f.flush()
        os.fsync(f.fileno())
    os.replace(temporary, final)
    # POSIX directory sync; Windows supports fsync of the file, not this directory open.
    if os.name != 'nt':
        fd = os.open(directory, os.O_RDONLY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)
    return final, False

async def main(args):
    from bleak import BleakClient, BleakScanner
    print('Searching for XIAO advertising ICVSP service...')
    device = await BleakScanner.find_device_by_filter(
        lambda d,a: (d.address.lower() == args.address.lower()) if args.address else
                    SERVICE in [u.lower() for u in a.service_uuids], timeout=20)
    if device is None:
        raise RuntimeError('XIAO not found. Requires flashed board; Galaxy A13 sample is not this firmware.')
    print('Connecting:', device.address)
    disconnected = asyncio.Event()
    frames = asyncio.Queue(maxsize=256)
    receiver = Reassembler()
    def notified(sender, data):
        try:
            frames.put_nowait(bytes(data))
        except asyncio.QueueFull:
            print('Receive buffer full: no ACK; device will retry.')
    async with BleakClient(device, disconnected_callback=lambda c: disconnected.set(),
                           winrt={'use_cached_services': False}) as client:
        async def worker():
            while True:
                frame = await frames.get()
                try:
                    complete = receiver.push(frame)
                    if complete:
                        message, raw = complete
                        path, duplicate = save_report(args.output, raw)
                        # ACK means durable byte receipt, NOT signature/backend acceptance.
                        await client.write_gatt_char(ACK, struct.pack('>H', message), response=True)
                        print(f'SAVED id={message} bytes={len(raw)} duplicate={duplicate} ACK sent: {path}')
                        if raw.startswith(b'UNSIGNED-TRANSPORT-TEST:'):
                            print('UNSIGNED diagnostic only; do not submit as a hazard report.')
                except Exception as error:
                    print('Receive/save/ACK failure (no success claimed):', error)
                finally:
                    frames.task_done()
        task = asyncio.create_task(worker())
        try:
            await client.start_notify(REPORT, notified)
            print('Subscribed. Waiting for reports. Ctrl+C stops.')
            await disconnected.wait()
        finally:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
    print('Disconnected. Restart command to reconnect; saved files remain.')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--address', help='Optional board address, for selecting one of several boards')
    parser.add_argument('--output', default='received')
    args = parser.parse_args()
    try:
        asyncio.run(main(args))
    except KeyboardInterrupt:
        print('Stopped.')
