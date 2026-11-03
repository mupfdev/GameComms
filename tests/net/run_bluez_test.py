#!/usr/bin/env python3
"""PC bridge on Linux: Bluetooth through BlueZ over D-Bus, tested against
tests/net/fake_bluez.py on a private D-Bus daemon (no Bluetooth hardware).

    run_bluez_test.py GAMECOMMSD GCBRIDGE GCSIM

Exit status 77 (skipped) when dbus-daemon or a Python with the dbus and gi
modules is not available."""
import os, shutil, signal, socket, subprocess, sys, tempfile, time

SKIP = 77
HERE = os.path.dirname(os.path.abspath(__file__))
server, bridge, gcsim = sys.argv[1], sys.argv[2], sys.argv[3]


def find_python():
    for py in (sys.executable, '/usr/bin/python3', '/usr/bin/python3.12'):
        if py and os.path.exists(py) and subprocess.run(
                [py, '-c', 'import dbus, dbus.service; from gi.repository import GLib'],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
            return py
    return None


def free_port():
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    p = s.getsockname()[1]
    s.close()
    return p


py = find_python()
daemon = shutil.which('dbus-daemon')
if not py or not daemon:
    print('skipped: needs dbus-daemon and Python with dbus + gi')
    sys.exit(SKIP)

tmp = tempfile.mkdtemp()
sock = os.path.join(tmp, 'bus')
cfg = os.path.join(tmp, 'bus.conf')
with open(cfg, 'w') as f:
    f.write(f'''<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>custom</type>
  <listen>unix:path={sock}</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
''')

procs = []
lines = []
rc = 0


def start(args, env=None, capture=False):
    p = subprocess.Popen(args, env=env, stdout=subprocess.PIPE if capture else None,
                         stderr=subprocess.STDOUT if capture else None, text=True)
    procs.append(p)
    return p


def expect(p, word, timeout=10):
    """read fake_bluez output until a line starting with word"""
    end = time.time() + timeout
    while time.time() < end:
        line = p.stdout.readline()
        if not line:
            break
        line = line.strip()
        lines.append(line)
        print('fake_bluez:', line, flush=True)
        if line.startswith(word):
            return line
    return None


try:
    bus = start([daemon, f'--config-file={cfg}', '--nofork'])
    for _ in range(50):
        if os.path.exists(sock):
            break
        time.sleep(0.1)
    env = dict(os.environ, DBUS_SYSTEM_BUS_ADDRESS=f'unix:path={sock}')

    sport, bport = free_port(), free_port()
    start([server, f'--port={sport}', '--bind=127.0.0.1', '--discovery=0', '--log_level=1'])
    fake = start([py, os.path.join(HERE, 'fake_bluez.py'), str(bport)], env=env, capture=True)
    if not expect(fake, 'READY'):
        print('FAIL: fake BlueZ did not start')
        sys.exit(1)
    time.sleep(0.3)
    br = start([bridge, f'--server=127.0.0.1', f'--port={sport}', '--pin=4321', '--log_level=2'], env=env)

    checks = []
    checks.append(('adapter made discoverable', expect(fake, 'SET org.bluez.Adapter1 Discoverable 1')))
    checks.append(('pairing agent registered', expect(fake, 'AGENT')))
    prof = expect(fake, 'PROFILE')
    checks.append(('Serial Port profile registered as server',
                   prof and 'role=server' in prof and 'auth=False' in prof))

    for args in (['--api=1', '--players=3'], ['--api=2', '--players=2', '--messages=200', '--reconnect']):
        r = subprocess.run([gcsim, f'--bridge=127.0.0.1:{bport}'] + args, timeout=120)
        print('gcbridge (BlueZ) gcsim', ' '.join(args), '->', r.returncode, flush=True)
        rc |= r.returncode

    # collect what the fake printed meanwhile
    fake.stdout.flush()
    br.send_signal(signal.SIGTERM)
    br.wait(timeout=10)
    expect(fake, 'UNREGISTER agent', timeout=5)
    out = '\n'.join(lines)
    checks.append(('PIN answered with the configured PIN', 'PIN 4321' in out))
    checks.append(('connections handed over by BlueZ', out.count('CONNECTION') >= 5))
    checks.append(('profile unregistered on exit', 'UNREGISTER profile' in out))
    for what, ok in checks:
        print(('ok   ' if ok else 'FAIL ') + what, flush=True)
        if not ok:
            rc = 1
finally:
    for p in procs:
        if p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()
    shutil.rmtree(tmp, ignore_errors=True)
sys.exit(rc)
