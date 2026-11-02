#!/usr/bin/env python3
"""Starts gamecommsd on a free port and runs gcsim scenarios against it."""
import socket, subprocess, sys, time
server, gcsim = sys.argv[1], sys.argv[2]
s = socket.socket(); s.bind(('127.0.0.1', 0)); port = s.getsockname()[1]; s.close()
srv = subprocess.Popen([server, f'--port={port}', '--bind=127.0.0.1', '--log_level=1', '--access_key=test-key'])
try:
    time.sleep(0.5)
    rc = 0
    for args in (['--api=1', '--players=4'], ['--api=2', '--players=4'], ['--api=1', '--players=2', '--messages=300']):
        r = subprocess.run([gcsim, f'--port={port}', '--key=test-key'] + args)
        print('gcsim', ' '.join(args), '->', r.returncode, flush=True)
        rc |= r.returncode
    # a wrong access key must be refused
    r = subprocess.run([gcsim, f'--port={port}', '--key=wrong', '--api=1'], stdout=subprocess.DEVNULL)
    print('gcsim wrong key ->', r.returncode, '(expected failure)')
    rc |= 0 if r.returncode != 0 else 1
    sys.exit(rc)
finally:
    srv.terminate(); srv.wait()
