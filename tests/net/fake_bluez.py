#!/usr/bin/env python3
"""Minimal stand-in for BlueZ, for testing the PC bridge's Linux Bluetooth
code without Bluetooth hardware.

Owns org.bluez on the bus given in DBUS_SYSTEM_BUS_ADDRESS and implements
the parts the bridge uses: ProfileManager1, AgentManager1 and the adapter's
properties.  Once the bridge has registered its Serial Port profile, TCP
connections to 127.0.0.1:PORT are handed to the bridge as connected sockets
through Profile1.NewConnection, exactly like BlueZ does with RFCOMM
connections.  Before the first connection of each device the agent is asked
for the PIN.

    fake_bluez.py PORT

Prints one line per event (READY, SET, AGENT, PROFILE, PIN, CONNECTION,
UNREGISTER) for the test script to check.
"""
import os
import select
import socket
import sys
import threading

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

SPP = '00001101-0000-1000-8000-00805f9b34fb'


def say(*a):
    print(*a, flush=True)


class Root(dbus.service.Object):
    def __init__(self, bus):
        super().__init__(bus, '/org/bluez')
        self.profile = None   # (sender, path)
        self.agent = None
        self.paired = set()

    @dbus.service.method('org.bluez.ProfileManager1', in_signature='osa{sv}', sender_keyword='sender')
    def RegisterProfile(self, path, uuid, opts, sender=None):
        if uuid.lower() != SPP:
            raise dbus.exceptions.DBusException('wrong UUID', name='org.bluez.Error.InvalidArguments')
        if self.profile:
            raise dbus.exceptions.DBusException('UUID already registered', name='org.bluez.Error.NotPermitted')
        self.profile = (sender, str(path))
        say('PROFILE', uuid, 'role=%s' % opts.get('Role'), 'name=%s' % opts.get('Name'),
            'auth=%s' % bool(opts.get('RequireAuthentication', True)))

    @dbus.service.method('org.bluez.ProfileManager1', in_signature='o', sender_keyword='sender')
    def UnregisterProfile(self, path, sender=None):
        self.profile = None
        say('UNREGISTER profile')

    @dbus.service.method('org.bluez.AgentManager1', in_signature='os', sender_keyword='sender')
    def RegisterAgent(self, path, capability, sender=None):
        self.agent = (sender, str(path))
        say('AGENT', capability)

    @dbus.service.method('org.bluez.AgentManager1', in_signature='o')
    def RequestDefaultAgent(self, path):
        say('DEFAULT_AGENT')

    @dbus.service.method('org.bluez.AgentManager1', in_signature='o')
    def UnregisterAgent(self, path):
        say('UNREGISTER agent')


class Adapter(dbus.service.Object):
    def __init__(self, bus):
        super().__init__(bus, '/org/bluez/hci0')

    @dbus.service.method('org.freedesktop.DBus.Properties', in_signature='ssv')
    def Set(self, iface, prop, value):
        say('SET', iface, prop, int(value))


def relay(a, b):
    try:
        while True:
            r, _, _ = select.select([a, b], [], [], 1.0)
            for s in r:
                data = s.recv(4096)
                if not data:
                    return
                (b if s is a else a).sendall(data)
    except OSError:
        pass
    finally:
        a.close()
        b.close()


def main():
    port = int(sys.argv[1])
    DBusGMainLoop(set_as_default=True)
    bus = dbus.bus.BusConnection(os.environ['DBUS_SYSTEM_BUS_ADDRESS'])
    name = dbus.service.BusName('org.bluez', bus)  # noqa: F841 (keep the name)
    root = Root(bus)
    Adapter(bus)

    lsock = socket.socket()
    lsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    lsock.bind(('127.0.0.1', port))
    lsock.listen(8)

    def on_accept(src, cond):
        conn, (_, sport) = lsock.accept()
        if not root.profile:
            conn.close()
            return True
        dev = dbus.ObjectPath('/org/bluez/hci0/dev_02_47_43_42_%02X_%02X' % (sport >> 8, sport & 0xFF))
        if root.agent and dev not in root.paired:
            agent = bus.get_object(root.agent[0], root.agent[1], introspect=False)
            pin = agent.RequestPinCode(dev, dbus_interface='org.bluez.Agent1')
            say('PIN', pin)
            root.paired.add(dev)
        mine, theirs = socket.socketpair()
        profile = bus.get_object(root.profile[0], root.profile[1], introspect=False)
        profile.NewConnection(dev, dbus.types.UnixFd(theirs), dbus.Dictionary({}, signature='sv'),
                              dbus_interface='org.bluez.Profile1')
        theirs.close()  # the bridge has its own copy now
        say('CONNECTION', dev)
        threading.Thread(target=relay, args=(conn, mine), daemon=True).start()
        return True

    GLib.io_add_watch(lsock.fileno(), GLib.IO_IN, on_accept)
    say('READY')
    GLib.MainLoop().run()


if __name__ == '__main__':
    main()
