#!/usr/bin/env python3
# See LICENSE file in the project root for license information.
"""Exercise the actual Asio example programs against a bounded loopback engine.

The small encoder implements only the length-delimited/varint fields used by
these fixtures. SDK protobuf parsing and TLS have their own library tests.
No credentials, external engine or third-party Python modules are used.
"""
from pathlib import Path
import argparse
import base64
import hashlib
import os
import queue
import threading
import signal
import socket
import struct
import subprocess
import tempfile



def varint(value):
    result = bytearray()
    while value > 127:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return bytes(result)


def field(number, value):
    if isinstance(value, str):
        value = value.encode()
    return varint(number * 8 + 2) + varint(len(value)) + value


def integer(number, value):
    return varint(number * 8) + varint(value)


def fields(data):
    result = {}
    offset = 0

    def take():
        nonlocal offset
        value = shift = 0
        while True:
            byte = data[offset]
            offset += 1
            value |= (byte & 127) << shift
            if byte < 128:
                return value
            shift += 7
            assert shift < 64

    while offset < len(data):
        key = take()
        if key & 7 == 2:
            size = take()
            value = data[offset:offset + size]
            offset += size
        else:
            assert key & 7 == 0
            value = take()
        result[key >> 3] = value
    return result


def exact(stream, size):
    data = b''
    while len(data) < size:
        part = stream.recv(size - len(data))
        assert part, 'unexpected EOF'
        data += part
    return data


def receive(stream):
    size, = struct.unpack('!I', exact(stream, 4))
    assert size < 65536
    message = fields(exact(stream, size))
    number, payload = next(iter(message.items()))
    return number, fields(payload)


def send(stream, number, payload=b''):
    data = field(number, payload)
    stream.sendall(struct.pack('!I', len(data)) + data)


def accept(listener):
    stream, _ = listener.accept()
    stream.settimeout(5)
    return stream


def finish_control(control):
    while True:
        number, payload = receive(control)
        if number == 7:
            send(control, 8, field(1, payload[1]))
        elif number == 3:
            send(control, 4)
            return
        else:
            assert number in (10, 15), number


def zero_rtt(request):
    return bool(fields(request.get(3, b'')).get(1, 0))


def proxy(listener, control):
    send(control, 9, field(1, 'tunnel-1') + field(2, 'stream-1'))
    stream = accept(listener)
    number, request = receive(stream)
    assert number == 11 and request[2] == b'stream-1'
    if not zero_rtt(request):
        send(stream, 12)
    return stream


def http_response(stream):
    stream.sendall(b'GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')
    response = b''
    while b'\r\n\r\n' not in response:
        response += exact(stream, 1)
        assert len(response) < 65536
    header, body = response.split(b'\r\n\r\n', 1)
    assert b'200 OK' in header and b'Connection: close' in header
    size = next(int(line.split(b':')[1]) for line in header.split(b'\r\n') if line.lower().startswith(b'content-length:'))
    body += exact(stream, size - len(body))
    assert body
    return body.decode()


def websocket(stream):
    key = base64.b64encode(b'local-test-key!!')
    stream.sendall(b'GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: ' + key + b'\r\n\r\n')
    header = b''
    while b'\r\n\r\n' not in header:
        header += exact(stream, 1)
    assert b'101 Switching Protocols' in header
    expected = base64.b64encode(hashlib.sha1(key + b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
    assert expected in header
    stream.sendall(b'\x81\x85\0\0\0\0hello')
    assert exact(stream, 7) == b'\x81\x05hello'
    stream.sendall(b'\x88\x82\0\0\0\0\x03\xe8')
    assert exact(stream, 4) == b'\x88\x02\x03\xe8'
    assert stream.recv(1) == b'', 'explicit transport close did not arrive'


def run(name, reject=False, bad_echo=False, reject_creation=False):
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(4)
        listener.settimeout(5)
        environment = {k: v for k, v in os.environ.items() if not k.startswith('RSTREAM_')}
        environment['RSTREAM_CONFIG'] = str(OUT / 'empty-config.yaml')
        environment['RSTREAM_ENGINE_ADDRESS'] = f'tcp://127.0.0.1:{listener.getsockname()[1]}'
        args = [str(PROGRAMS[name])]
        process = subprocess.Popen(args, env=environment, cwd=OUT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            with accept(listener) as control:
                assert receive(control)[0] == 1
                if reject:
                    send(control, 2, field(2, integer(1, 1000) + field(2, field(1, 'local rejection'))))
                else:
                    send(control, 2, field(1, field(1, 'client-1')))
                    number, request = receive(control)
                    assert number == 5
                    if name in ('callback', 'future', 'coroutine'):
                        requested = fields(request[2])
                        assert fields(requested[5]).get(1, 0) == 0
                        assert fields(requested[6])[1] == b'tls'
                    if reject_creation:
                        send(control, 6, field(1, request[1]) + field(3, integer(1, 1000) + field(2, field(1, 'creation rejected'))))
                        finish_control(control)
                    else:
                        properties = request[2] + field(1, field(1, 'tunnel-1')) + field(4, field(1, 'bytestream'))
                        properties += field(22, field(1, 'localhost')) + field(23, integer(1, 443))
                        send(control, 6, field(1, request[1]) + field(2, properties))
                        if name == 'private-echo':
                            with accept(listener) as dial:
                                number, request = receive(dial)
                                assert number == 13 and request[2] == b'echo'
                                if not zero_rtt(request):
                                    send(dial, 14, field(1, 'stream-1'))
                                with proxy(listener, control) as upstream:
                                    upstream.sendall(exact(dial, 5))
                                    reply = exact(upstream, 5)
                                    dial.sendall(b"WRONG" if bad_echo else reply)
                            finish_control(control)
                        elif name.endswith('server') or name == 'beast-tunnel':
                            with proxy(listener, control) as stream:
                                if name == 'websocket-server':
                                    websocket(stream)
                                else:
                                    body = http_response(stream)
                                    if name == 'beast-tunnel':
                                        assert body == 'Hello from the shared Beast handler'
                            process.send_signal(signal.SIGTERM)
                        else:
                            finish_control(control)
                stdout, stderr = process.communicate(timeout=5)
            assert (os.name == 'nt' and (name.endswith('server') or name == 'beast-tunnel')) or process.returncode == (1 if reject or bad_echo or reject_creation else 0), (name, process.returncode, stdout, stderr)
            if name in ('callback', 'future', 'coroutine') and not reject and not reject_creation:
                assert 'Private tunnel: ' in stdout
            if name == 'private-echo' and not reject and not bad_echo:
                assert 'Echo: hello' in stdout
            return {'program': name, 'case': 'creation-rejection' if reject_creation else 'engine-rejection' if reject else 'bad-echo' if bad_echo else 'success', 'exit': process.returncode, 'stdout': stdout, 'stderr': stderr}
        finally:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=5)



def run_local_migration():
    process = subprocess.Popen([str(PROGRAMS["beast-local"]), "0"],
                               cwd=OUT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    ready = queue.Queue()
    reader = threading.Thread(target=lambda: ready.put(process.stdout.readline()), daemon=True)
    reader.start()
    try:
        line = ready.get(timeout=5)
        reader.join(timeout=5)
        assert line.startswith("Local address: 127.0.0.1:"), line
        port = int(line.rsplit(":", 1)[1])
        for _ in range(2):
            with socket.create_connection(("127.0.0.1", port), timeout=5) as stream:
                assert http_response(stream) == "Hello from the shared Beast handler"
        process.send_signal(signal.SIGTERM)
        stdout, stderr = process.communicate(timeout=5)
        assert os.name == "nt" or process.returncode == 0, (stdout, stderr)
        assert not stderr, stderr
        print("beast-local", "same HTTP requests as tunnel", flush=True)
    finally:
        if process.poll() is None:
            process.kill()
        process.wait(timeout=5)


if __name__ == "__main__":
    names = ("callback", "future", "coroutine", "private-echo", "http-server", "websocket-server", "beast-tunnel", "beast-local")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("programs", nargs=8, type=Path, metavar="BINARY")
    options = parser.parse_args()
    PROGRAMS = dict(zip(names, (path.resolve() for path in options.programs)))
    with tempfile.TemporaryDirectory(prefix="rstream-example-test-") as directory:
        OUT = Path(directory)
        (OUT / "empty-config.yaml").write_text("{}\n")
        for name in names[:-1]:
            result = run(name)
            print(name, result["case"], result["exit"], flush=True)
        run_local_migration()
        for name in ("callback", "future", "coroutine", "private-echo"):
            result = run(name, reject=True)
            print(name, result["case"], result["exit"], flush=True)
        for name in ("callback", "future", "coroutine"):
            result = run(name, reject_creation=True)
            print(name, result["case"], result["exit"], flush=True)
        result = run("private-echo", bad_echo=True)
        print("private-echo", result["case"], result["exit"], flush=True)
