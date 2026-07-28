CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Iinclude -Iports/test-host
SAN_CFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g

CORE_SRC := core/buffer/endian.c core/buffer/buffer.c protocols/hci/hci.c \
            core/event/queue.c core/timer/timer.c core/controller/command_queue.c \
            core/controller/controller.c core/addr/addr.c core/device/device_registry.c \
            protocols/l2cap/l2cap.c protocols/l2cap/signaling.c \
            protocols/l2cap/channel_manager.c protocols/sdp/sdp.c \
            protocols/sdp/sdp_client.c protocols/att/att.c
PORT_SRC := ports/test-host/virtual_transport/virtual_transport.c
TEST_SRC := tests/main.c tests/support/test.c tests/endian/test_endian.c \
            tests/buffer/test_buffer.c tests/hci/test_hci.c \
            tests/virtual_transport/test_virtual_transport.c \
            tests/queue/test_queue.c tests/timer/test_timer.c \
            tests/controller/test_command_queue.c tests/controller/test_controller.c \
            tests/addr/test_addr.c tests/device/test_device_registry.c \
            tests/discovery/test_discovery.c tests/l2cap/test_l2cap.c \
            tests/l2cap/test_signaling.c tests/l2cap/test_channel.c \
            tests/sdp/test_sdp.c tests/sdp/test_sdp_client.c tests/att/test_att.c

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
