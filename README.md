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
- USB controllers via [Poseidon](https://www.aros.org), behind an abstract HCI transport interface.
- HID Classic and HID over GATT (HOGP), pairing/bonding, and eventually audio (A2DP).
- Correctness on both big-endian (m68k, PowerPC) and little-endian (ARM, x86) targets, from day one —
  not retrofitted later.
- A bare-metal-capable protocol core: no mandatory OS, POSIX, filesystem, threads, or dynamic heap;
  platform services and storage are supplied explicitly by each port.
- Auditable provenance: clean-room implementation against public specifications. See
  `project.md`'s licensing section.

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
          +-- virtual controller (testing)
```

The portable core never touches `AllocMem`, `CreateTask`, `OpenDevice`, POSIX, or any other
platform API directly — only through explicit port interfaces such as `bt_platform_ops`. It must also
build for a freestanding bare-metal port without requiring a filesystem, threads, or a dynamic heap.
Every multi-byte wire field goes through explicit little-/big-endian helpers
(`bluetooth/endian.h`); nothing is ever cast from a raw buffer to a C struct.

## Repository layout

```text
include/bluetooth/   Public headers for the portable core
core/                 Portable building blocks: buffers, endian helpers, queues, timers,
                      device registry, HCI command queue/controller state machine
protocols/            Wire protocol codecs: HCI, L2CAP (PDU framing, ACL fragmentation,
                      signaling, connection-oriented channels)
ports/
  test-host/          Host-side test doubles (virtual HCI transport/controller)
  aros/                AROS input.device and usbbluetooth.device adapters, event bridge,
                      and native self-test
tests/                Unit and integration tests, mirroring the source layout
ai-context/           Investigation notes, architectural decisions, and status tracking
mmakefile.src          Native AROS MetaMake integration
Makefile.aros          Standalone AROS cross-build helper
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

### Building as an AROS contrib component

The repository carries its own [`mmakefile.src`](mmakefile.src). Put the complete checkout directly
under the AROS source tree:

```text
AROS/
└── contrib/
    └── aros-bluzing/    # this repository
```

MetaMake discovers the file while scanning the source tree. The regular `contrib` build includes the
`contrib-aros-bluzing` target, which:

- compiles the portable stack and AROS adapter into `libarosbluzing.a`;
- links the native `aros-bluzing-selftest` program; and
- installs that self-test in `C:` in the generated AROS system tree.

For a focused build in an already configured and complete AROS build tree:

```sh
make contrib-aros-bluzing
```

No absolute source, SDK, LLVM, or build-directory path is encoded in `mmakefile.src`; those values
come from the enclosing AROS build. `Makefile.aros` remains useful for isolated cross-compilation,
but it is not the intended integration path.

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
- **Security and GATT**: SMP Legacy and Secure Connections building blocks, pairing manager, bond
  serialization, ATT/GATT client support, and transaction timeouts.
- **HID**: report descriptor parsing, normalized keyboard/mouse/consumer input, HOGP including Boot
  Protocol and output reports, plus an AROS `input.device` event adapter.
- **AROS build validation**: native AArch64 compile/link and an image-booted QEMU self-test covering
  byte order, HID-to-AROS event delivery, and bond storage.
- **AROS USB transport**: asynchronous HCI command/ACL I/O and driver-delivered event messages over
  the existing `usbbluetooth.device`, designed to be polled by the single-owner Manager Task.

Still pending are the production Bluetooth Manager Task, HID Classic, RFCOMM, audio, persistent bond
storage in the AROS port, and controller hardware validation. See
[`ai-context/status.md`](ai-context/status.md) for the live, detailed tracker (in Portuguese).

## Contributing

This project is under active, AI-assisted development following the process and provenance rules laid
out in `project.md`. If you're looking at the source: every protocol format has unit tests with
known-good wire-byte vectors, and every module documents its scope reductions inline rather than
silently under-implementing the spec.
