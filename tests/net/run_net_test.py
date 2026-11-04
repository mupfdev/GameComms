#!/usr/bin/env python3
"""Starts gamecommsd on free ports and runs gcsim scenarios against it."""
import socket, subprocess, sys, time
server, gcsim = sys.argv[1], sys.argv[2]

def free_port():
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port

# Non-loopback plaintext listeners require explicit operator acknowledgement.
port = free_port()
r = subprocess.run([server, f'--port={port}', '--bind=0.0.0.0'], capture_output=True, text=True)
if r.returncode == 0 or 'allow_plaintext_network=1' not in r.stderr:
    raise SystemExit('server did not refuse an unacknowledged non-loopback listener')
r = subprocess.run([server, f'--port={port}', '--bind=0.0.0.0', '--allow_plaintext_network=1'],
                   capture_output=True, text=True)
if r.returncode == 0 or 'without an access_key' not in r.stderr:
    raise SystemExit('server did not require a key for a non-loopback listener')

rc = 0
for args in (['--api=1', '--players=4'], ['--api=2', '--players=4'], ['--api=1', '--players=2', '--messages=300']):
    port = free_port()
    srv = subprocess.Popen([server, f'--port={port}', '--bind=127.0.0.1', '--log_level=1', '--access_key=test-key'])
    try:
        time.sleep(0.5)
        r = subprocess.run([gcsim, f'--port={port}', '--key=test-key'] + args)
        print('gcsim', ' '.join(args), '->', r.returncode, flush=True)
        rc |= r.returncode
    finally:
        srv.terminate()
        srv.wait()

# A wrong access key must be refused by a fresh server.
port = free_port()
srv = subprocess.Popen([server, f'--port={port}', '--bind=127.0.0.1', '--log_level=1', '--access_key=test-key'])
try:
    time.sleep(0.5)
    r = subprocess.run([gcsim, f'--port={port}', '--key=wrong', '--api=1'], stdout=subprocess.DEVNULL)
    print('gcsim wrong key ->', r.returncode, '(expected failure)')
    rc |= 0 if r.returncode != 0 else 1
finally:
    srv.terminate()
    srv.wait()
sys.exit(rc)
