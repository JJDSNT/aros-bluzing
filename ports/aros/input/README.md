# AROS input.device adapter

`input_bridge.c` is compiled by the host tests. It translates transport-neutral
HID events into AROS raw key/mouse events without depending on AROS headers.

`input_device.c` is the thin native adapter. It owns one `MsgPort` and
`IOStdReq`, opens `input.device`, and submits each translated event through
`IND_WRITEEVENT`, following the same lifecycle used by Poseidon's
`bootkeyboard.class` and `bootmouse.class`.

The native file is intentionally not part of the host `Makefile`. It must be
added to the AROS component build once the Bluetooth Manager Task exists and an
AROS cross-toolchain is available. All calls must run on that task because the
adapter uses synchronous `DoIO`.
