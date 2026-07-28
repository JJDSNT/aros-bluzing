# aros-bluzing

A native, portable Bluetooth Host stack for [AROS](https://www.aros.org/), the open-source
AmigaOS-compatible operating system.

The protocol core is deliberately OS-independent: it builds and is fully tested on a plain host
(Linux/any POSIX-ish machine with a C compiler), with no dependency on Exec, Poseidon, or any other
AROS API. AROS is the primary, first-class port — not an afterthought — but keeping the core portable
is what makes it possible to test extensively (including fuzzing, replay, and big-endian correctness)
without real Bluetooth hardware or an AROS build environment.

See [`project.md`](project.md) for the full architecture specification, and [`ai-context/`](ai-context/)
for the ongoing investigation notes, design decisions, and implementation status.

## Goals

- Bluetooth Classic (BR/EDR) and Bluetooth Low Energy.
- USB controllers via [Poseidon](https://www.aros.org) initially; UART/SDIO transports for onboard
  SoC chipsets later.
- HID Classic and HID over GATT (HOGP), pairing/bonding, and eventually audio (A2DP).
- Correctness on both big-endian (m68k, PowerPC) and little-endian (ARM, x86) targets, from day one —
  not retrofitted later.
- Auditable provenance: clean-room implementation against public specifications, not derived from
  BTstack or (until licensing is resolved) NimBLE source code. See project.md's licensing section.

## Architecture

```text
Applications and AROS classes
          |
          v
   bluetooth.library
          |
          v
  Bluetooth Manager Task
          |
          v
 Portable protocol core
          |
          v
Abstract HCI transport interface
          |
          +-- Poseidon USB Bluetooth class
          +-- UART transport
          +-- SDIO transport
          +-- virtual controller (testing)
```

The portable core never touches `AllocMem`, `CreateTask`, `OpenDevice`, POSIX, or any other
platform API directly — only through a small `bt_platform_ops` seam. Every multi-byte wire field goes
through explicit little-/big-endian helpers (`bluetooth/endian.h`); nothing is ever cast from a raw
buffer to a C struct.

## Repository layout

```text
include/bluetooth/   Public headers for the portable core
core/                 Portable building blocks: buffers, endian helpers, queues, timers,
                      device registry, HCI command queue/controller state machine
protocols/            Wire protocol codecs: HCI, L2CAP (PDU framing, ACL fragmentation,
                      signaling, connection-oriented channels)
ports/
  test-host/          Host-side test doubles (virtual HCI transport/controller)
  aros/                AROS-specific glue (library, task, USB transport adapter) -- not
                      started yet, see Status below
tests/                Unit and integration tests, mirroring the source layout
ai-context/           Investigation notes, architectural decisions, and status tracking
project.md             Full project specification
```

## Building and testing

The portable core and its test suite build with a plain C compiler and no AROS toolchain:

```sh
make test
```

This builds `build/test_runner` with `-Wall -Wextra -Werror` plus AddressSanitizer and
UndefinedBehaviorSanitizer, and runs the full suite. There is currently no host-endianness dependency
anywhere in the code (verified by fixed wire-byte-vector tests), so the same behavior is expected on a
big-endian build; a real big-endian CI target (e.g. m68k) is still future work.

## Status

Implemented so far, entirely host-testable via a simulated virtual HCI controller (no real Bluetooth
hardware or AROS build required):

- **Foundation**: fixed-width types, LE/BE byte-order helpers, bounds-checked buffer reader/writer,
  intrusive SPSC queue, expiry-sorted timer list.
- **HCI**: command/event encode/parse (Command Complete, Command Status, ACL header), a command queue
  with controller credit accounting and per-command timeout, and a controller bring-up state machine
  (Reset -> Read Local Version -> Read Local Supported Features -> Read Buffer Size -> Ready).
- **Discovery**: Classic Inquiry and LE scanning, with a unified, deduplicated device registry that
  flags dual-mode devices.
- **L2CAP**: PDU framing, ACL fragmentation/reassembly, signaling command codec (Connection/
  Configuration/Disconnection, Command Reject), and a connection-oriented channel state machine
  (initiator role only for now) with bidirectional MTU configuration, data transfer, and clean teardown
  — including mid-negotiation cancellation.

Not started yet: AROS integration (`ports/aros/`), SDP, GATT, SMP/pairing, RFCOMM, and audio. See
[`ai-context/status.md`](ai-context/status.md) for the live, detailed tracker (in Portuguese) and
[`ai-context/fase0-propostas.md`](ai-context/fase0-propostas.md) for the concrete plan to integrate with
AROS's existing `rom/usb/classes/bluetooth/` driver rather than writing a new one from scratch.

## Contributing

This project is under active, AI-assisted development following the process and provenance rules laid
out in `project.md`. If you're looking at the source: every protocol format has unit tests with
known-good wire-byte vectors, and every module documents its scope reductions inline rather than
silently under-implementing the spec.
