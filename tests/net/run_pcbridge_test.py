#!/usr/bin/env python3
"""PC bridge test: gamecommsd + gcbridge (TCP test devices instead of
Bluetooth) + gcsim --bridge, with an explicit server address and with LAN
discovery, for both API generations and with a dropped device connection."""
import socket, subprocess, sys, time

server, bridge, gcsim = sys.argv[1], sys.argv[2], sys.argv[3]


def free_port():
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    p = s.getsockname()[1]
    s.close()
    return p


sport = free_port()
srv = subprocess.Popen([server, f'--port={sport}', '--log_level=1', '--access_key=test-key'])
procs = [srv]
rc = 0
try:
    time.sleep(0.5)
    for mode in ('explicit', 'auto'):
        dport = free_port()
        host = '127.0.0.1' if mode == 'explicit' else 'auto'
        br = subprocess.Popen([bridge, '--bluetooth=0', f'--device_tcp={dport}', f'--server={host}',
                               f'--port={sport}', '--key=test-key', '--log_level=1'])
        procs.append(br)
        time.sleep(0.5)
        cases = [['--api=1', '--players=4'], ['--api=2', '--players=4']]
        if mode == 'explicit':
            cases.append(['--api=1', '--players=3', '--messages=200', '--reconnect'])
            cases.append(['--api=2', '--players=2', '--messages=200', '--reconnect'])
        for args in cases:
            r = subprocess.run([gcsim, f'--bridge=127.0.0.1:{dport}'] + args, timeout=120)
            print(f'gcbridge ({mode} server) gcsim', ' '.join(args), '->', r.returncode, flush=True)
            rc |= r.returncode
        br.terminate()
        br.wait(timeout=10)
        procs.remove(br)
finally:
    for p in procs:
        p.terminate()
        p.wait()
sys.exit(rc)
