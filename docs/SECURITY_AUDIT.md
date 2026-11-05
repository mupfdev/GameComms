# Security audit: gamecommsd (endpoint server)

Scope: `server/` (session manager and network front end), the shared
protocol library in `protocol/` that the server parses untrusted input with,
and the PC bridge's link id generation (`pcbridge/src/pcb_main.c`), which
determines how well the server's session resumption can be protected.
The N-Gage DLL and ESP32 firmware were not audited beyond the parts they
share with the server.

Every finding below was reproduced against the original code with a
proof-of-concept test under AddressSanitizer/UndefinedBehaviorSanitizer
before it was fixed. The tests live in `server/tests/test_core.c`
(`test_sec_*`) and now serve as regression tests; each can be run on its own
with `test_server_core <test name>`.

## Findings

| # | Severity | Finding | Precondition |
|---|---|---|---|
| 1 | Critical | Heap use-after-free via a second HELLO on an established connection | any registered client |
| 2 | Critical | Heap use-after-free via malformed HELLO, then transport close | any registered client |
| 3 | High | NULL pointer write (crash) via CLOSE + HELLO in one read | network access only |
| 4 | High | NULL dereference (crash) in `session_end()` via re-entrant peer destruction | host + stalled client |
| 5 | High | NULL dereference (crash) in `h_kick()`, same root cause as 4 | host + stalled client |
| 6 | High | Session hijack of a live player by presenting its link id | knowledge of a 32-bit link id |
| 7 | Medium | Unbounded unauthenticated connections lock out all players | network access only |
| 8 | Medium | Unlimited online guessing of the access key, non-constant-time compare | network access only |
| 9 | Medium | Predictable session ids defeat hidden sessions | any registered client |
| 10 | Low | Log forging and terminal escape injection through peer strings | network access only |
| 11 | Medium | PC bridge link ids from clock-seeded `rand()` (resumption credential) | knowledge of bridge start time |

### 1, 2: use-after-free of a peer still referenced by its session

The server sometimes marked a peer dead with a bare `p->dead = 1` instead of
going through `peer_kill()`, so the peer was freed while its session still
pointed at it. Two routes reached this:

1. `peer_on_hello()` treated any HELLO naming a known link id as a
   resumption, even on a connection that was already established and
   registered. The established peer was retired without leaving its session.
2. `gc_link_reject()` (malformed HELLO) closed the link without telling the
   owner. On the next transport close the registered peer looked like it had
   never connected, and `gcs_core_conn_closed()` flagged it dead directly.

The next message routed to that session (for example a JOIN, which notifies
the host) wrote through the dangling pointer.

### 3: frames processed after the link was closed

The deframer keeps decoding frames from the same read after a CLOSE or
REJECT. A HELLO following a CLOSE reached `peer_on_hello()` for a peer
`peer_kill()` had already detached, which then executed
`o->conn = p->conn; o->conn->peer = o;` with a NULL connection. Two
unauthenticated TCP connections were enough to crash the server.

### 4, 5: re-entrant peer destruction

`peer_send_raw()` killed a peer synchronously when its outbound queue
overflowed. `session_end()` and `h_kick()` sent a notification and then
dereferenced the member pointer, which the nested kill had already cleared.
A host and one client that stops reading could crash the server at will.

### 6: session hijack

The 32-bit link id was accepted as sufficient proof to take over any peer,
including one whose connection was alive. The victim was disconnected and
its traffic was routed to the attacker. Combined with finding 11, link ids
from the PC bridge were guessable.

### 7 to 10

- Connections in `WAIT_HELLO` never timed out, and each held a peer slot.
- `REGISTER` with a wrong access key could be repeated without limit on one
  connection, and the key was compared with `strcmp`.
- Session ids came from xorshift32, whose output equals its state. One
  observed id predicts all later ids, so hidden sessions are joinable by id.
- Registration names and ERROR texts were logged before sanitisation.

## Fixes

### Session manager (`server/src/gcs_core.c`)

- **Deferred destruction.** Sends never destroy a peer; an overflowing peer
  is marked `doomed` and killed at defined safe points (end of input
  processing, transport close, timer poll). This removes the re-entrancy
  class behind findings 4 and 5 instead of patching individual call sites.
- **One cleanup path.** Every peer death goes through `peer_kill()`, which
  leaves sessions and clears reservations. Before memory is released,
  `peer_scrub_refs()` verifies no session still references the peer, and
  dead sessions are unlinked from all peers (defence in depth; logged as an
  internal error if it ever triggers).
- **State before notification.** `session_end()`, `h_kick()` and
  `h_peer_accept()` update all membership state before sending anything.
- **Handshake rules.** One HELLO per connection; a different second HELLO
  closes the connection. HELLOs on dead or detached peers are ignored.
  A link whose connection is still alive is only resumed from the same
  source address; otherwise the resume waits until the old connection is
  declared dead (at most the link's 20 s dead time).
- **Limits.** Connections without a completed handshake are closed after
  `hello_timeout_ms`. A connection is closed after `max_auth_failures` wrong
  keys; keys are compared in constant time. Lingering connections whose link
  was closed are released.
- **Unpredictable ids** through `gcs_core_set_random()`, backed by the OS
  CSPRNG in `gcs_main.c`.
- **Input hygiene.** All log lines are stripped of control characters; all
  `strcpy`/`strncpy` replaced with a bounded copy; `SESSION_STATE` values are
  validated; slot indices are range checked before use.

### Link layer (`protocol/src/gc_link.c`)

- Frames after a CLOSE, REJECT or drop in the same input are ignored.
- Rejecting an established link reports `GC_LE_CLOSED` to the owner.

Both changes apply to all users of the library and match the documented
semantics; the bridge and DLL test suites pass unchanged.

### Network front end (`server/src/gcs_main.c`)

- OS CSPRNG (`/dev/urandom`, `BCryptGenRandom`); the server refuses to start
  without one.
- Per source address: concurrent connection limit and a token-bucket rate
  limit for new connections (loopback exempt by default, see below).
- Accept and UDP processing bounded per loop pass, so floods cannot starve
  established connections.
- UDP discovery answers are globally rate limited (the answer is larger than
  the request).
- Output buffer per connection reduced to 256 KB.
- Strict configuration parsing: range-checked integers, over-long strings
  are errors rather than silent truncation, over-long config lines rejected.
- `sigaction`, `sig_atomic_t` flags, Windows console handler, thread-safe and
  failure-safe timestamp formatting, `MSG_NOSIGNAL`.

### PC bridge (`pcbridge/src/pcb_main.c`)

- Link ids from the OS CSPRNG; a device is refused if none is available.

### Build and deployment

- Server binaries: stack protector, stack clash protection, CET where
  supported, `_FORTIFY_SOURCE=2`, PIE, full RELRO, `BIND_NOW`, non-executable
  stack; `/GS /sdl /guard:cf` on MSVC. Flags are probed per compiler.
- systemd unit: device, kernel, namespace and system call restrictions,
  empty capability set, `MemoryDenyWriteExecute`, restricted address
  families.
- CI: new `fuzz` job runs both libFuzzer harnesses on every push.

## Verification

- All 9 existing test suites pass under ASan + UBSan, including the
  real-socket end-to-end tests with `gamecommsd`, the PC bridge and the
  simulated BlueZ stack.
- 13 security regression tests, plus tests proving that legitimate
  resumption still works (half-open connection from the same address, and a
  detached link resuming from a new address).
- libFuzzer (ASan + UBSan) on the hardened code: the protocol decoders and
  the stateful core harness (8 simulated connections, correctly sequenced
  link frames, invariant checks for writes to closed transports and log
  injection) ran several million executions without findings. Harnesses:
  `server/fuzz/`.
- cppcheck: two reports, both reviewed as false positives.
- Release builds with no compiler warnings on GCC/Clang (Linux) and MinGW
  (Windows cross build). MSVC builds and running the Windows binaries were
  not tested.

## Residual risks

- **Resumption credential.** A detached link can be resumed by anyone who
  presents its 32-bit link id during the link grace period (20 s by default).
  Rate limits make guessing impractical for a single source, and the bridge
  now uses unpredictable ids, but the proper fix is a longer server-issued
  resume token. That is a protocol change across server, bridges and DLL.
- **Local tunnels.** Behind SSH port forwarding or stunnel every client
  appears as `127.0.0.1`. The same-address resume rule and per-address limits
  then cannot tell bridges apart. A VPN such as WireGuard keeps distinct
  addresses.
- **No transport security.** As before, TCP traffic and the access key are
  cleartext; remote access must go through a trusted VPN or tunnel.
- **Trusted peers inside a session.** A malicious host can still cause slow
  clients to be dropped by flooding them, which is inherent to relaying.
