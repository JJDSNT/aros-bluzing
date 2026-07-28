#include <bluetooth/aros_input_bridge.h>
#include <bluetooth/bond_store.h>
#include <bluetooth/endian.h>
#include <bluetooth/hid_input.h>
#include <bluetooth/hid_report.h>
#include <bluetooth/manager.h>

#include "../task/manager_task.h"
#include <virtual_transport/virtual_transport.h>

#include <stdio.h>
#include <string.h>

struct selftest_sink
{
    size_t count;
    struct bt_aros_input_event last;
};

static bt_status_t capture_aros_event(
    void *context, const struct bt_aros_input_event *event)
{
    struct selftest_sink *sink = context;

    ++sink->count;
    sink->last = *event;
    return BT_OK;
}

static bool forward_to_aros(const struct bt_hid_input_event *event,
                            void *user_data)
{
    return bt_aros_input_bridge_handle(event, user_data);
}

static bool test_endian(void)
{
    uint8_t bytes[8];

    bt_write_le32(bytes, 0x78563412u);
    bt_write_be32(bytes + 4, 0x12345678u);
    return bytes[0] == 0x12 && bytes[3] == 0x78 &&
           bytes[4] == 0x12 && bytes[7] == 0x78 &&
           bt_read_le32(bytes) == 0x78563412u &&
           bt_read_be32(bytes + 4) == 0x12345678u;
}

static bool test_hid_to_aros(void)
{
    static const uint8_t keyboard_descriptor[] = {
        0x05, 0x07, 0x19, 0x00, 0x29, 0x65,
        0x15, 0x00, 0x25, 0x65, 0x75, 0x08,
        0x95, 0x02, 0x81, 0x00};
    const uint8_t key_down[] = {0x04, 0};
    const uint8_t key_up[] = {0, 0};
    struct bt_hid_report_descriptor descriptor;
    struct bt_hid_input input;
    struct bt_aros_input_bridge bridge;
    struct selftest_sink sink;

    memset(&sink, 0, sizeof(sink));
    if (bt_hid_report_parse(keyboard_descriptor,
                            sizeof(keyboard_descriptor), &descriptor) != BT_OK)
        return false;
    bt_hid_input_init(&input, &descriptor);
    bt_aros_input_bridge_init(&bridge, capture_aros_event, &sink);
    if (bt_hid_input_process(&input, key_down, sizeof(key_down),
                              forward_to_aros, &bridge) != BT_OK ||
        sink.count != 1 || sink.last.event_class != BT_AROS_IECLASS_RAWKEY ||
        sink.last.code != 0x20)
        return false;
    if (bt_hid_input_process(&input, key_up, sizeof(key_up),
                              forward_to_aros, &bridge) != BT_OK)
        return false;
    return sink.count == 2 &&
           sink.last.code == (0x20 | BT_AROS_IECODE_UP_PREFIX);
}

static bool test_bond_store(void)
{
    struct bt_bond_store store;
    uint8_t encoded[32];
    struct bt_buf_writer writer;

    bt_bond_store_init(&store);
    bt_buf_writer_init(&writer, encoded, sizeof(encoded));
    return bt_bond_store_serialize(&store, &writer) == BT_OK &&
           bt_buf_writer_len(&writer) == 20 &&
           memcmp(encoded, "BTKD", 4) == 0;
}

static bool test_manager_task(void)
{
    struct bt_virtual_transport transport;
    struct bt_aros_manager_task task;

    bt_virtual_transport_init(&transport);
    bt_aros_manager_task_init(
        &task, &transport.base, &transport, NULL, NULL);
    if (bt_aros_manager_task_start(&task) != BT_OK)
        return false;
    if (task.manager.state != BT_MANAGER_STATE_RUNNING ||
        task.manager.controller.state != BT_CONTROLLER_STATE_READY)
    {
        bt_aros_manager_task_stop(&task);
        return false;
    }
    bt_aros_manager_task_stop(&task);
    return task.manager.state == BT_MANAGER_STATE_STOPPED &&
           task.task == NULL && !transport.is_open;
}

int main(void)
{
    unsigned int passed = 0;

    if (test_endian())
        ++passed;
    else
        printf("FAIL endian\n");
    if (test_hid_to_aros())
        ++passed;
    else
        printf("FAIL HID-to-AROS\n");
    if (test_bond_store())
        ++passed;
    else
        printf("FAIL bond-store\n");
    if (test_manager_task())
        ++passed;
    else
        printf("FAIL manager-task\n");

    printf("aros-bluzing selftest: %u/4 passed\n", passed);
    return passed == 4 ? 0 : 20;
}
