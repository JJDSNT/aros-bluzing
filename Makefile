CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Iinclude -Iports/test-host -Iprotocols
SAN_CFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g

CORE_SRC := core/buffer/endian.c core/buffer/buffer.c protocols/hci/hci.c \
            core/event/queue.c core/timer/timer.c core/controller/command_queue.c \
            core/controller/controller.c core/manager/manager.c core/addr/addr.c \
            core/device/device_registry.c \
            protocols/l2cap/l2cap.c protocols/l2cap/signaling.c \
            protocols/l2cap/channel_manager.c protocols/sdp/sdp.c \
            protocols/sdp/sdp_client.c protocols/att/att.c protocols/gatt/gatt_client.c
CORE_SRC += protocols/smp/smp.c
CORE_SRC += core/security/bond_store.c
CORE_SRC += core/security/smp_crypto.c
CORE_SRC += core/security/smp_pairing.c
CORE_SRC += core/security/smp_manager.c
CORE_SRC += core/hid/report_parser.c
CORE_SRC += core/hid/input.c
CORE_SRC += profiles/hogp/hogp_client.c
CORE_SRC += ports/aros/input/input_bridge.c
CORE_SRC += protocols/vendor_init/dummy/dummy_vendor_init.c
PORT_SRC := ports/test-host/virtual_transport/virtual_transport.c
TEST_SRC := tests/main.c tests/support/test.c tests/endian/test_endian.c \
            tests/buffer/test_buffer.c tests/hci/test_hci.c \
            tests/virtual_transport/test_virtual_transport.c \
            tests/queue/test_queue.c tests/timer/test_timer.c \
            tests/controller/test_command_queue.c tests/controller/test_controller.c \
            tests/manager/test_manager.c \
            tests/addr/test_addr.c tests/device/test_device_registry.c \
            tests/discovery/test_discovery.c tests/l2cap/test_l2cap.c \
            tests/l2cap/test_signaling.c tests/l2cap/test_channel.c \
            tests/sdp/test_sdp.c tests/sdp/test_sdp_client.c tests/att/test_att.c \
            tests/gatt/test_gatt_client.c
TEST_SRC += tests/smp/test_smp.c
TEST_SRC += tests/security/test_bond_store.c
TEST_SRC += tests/security/test_smp_crypto.c
TEST_SRC += tests/security/test_smp_pairing.c
TEST_SRC += tests/security/test_smp_manager.c
TEST_SRC += tests/hid/test_hid_report.c
TEST_SRC += tests/hid/test_hid_input.c
TEST_SRC += tests/hid/test_hogp_client.c
TEST_SRC += tests/hid/test_aros_input_bridge.c
TEST_SRC += tests/vendor_init/test_vendor_init.c

BUILD := build

.PHONY: test clean

test: $(BUILD)/test_runner
	$(BUILD)/test_runner

$(BUILD)/test_runner: $(CORE_SRC) $(PORT_SRC) $(TEST_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(SAN_CFLAGS) -o $@ $(CORE_SRC) $(PORT_SRC) $(TEST_SRC)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
