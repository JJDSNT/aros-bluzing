CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Iinclude
SAN_CFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g

CORE_SRC := core/buffer/endian.c core/buffer/buffer.c
TEST_SRC := tests/main.c tests/support/test.c tests/endian/test_endian.c tests/buffer/test_buffer.c

BUILD := build

.PHONY: test clean

test: $(BUILD)/test_runner
	$(BUILD)/test_runner

$(BUILD)/test_runner: $(CORE_SRC) $(TEST_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(SAN_CFLAGS) -o $@ $(CORE_SRC) $(TEST_SRC)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
