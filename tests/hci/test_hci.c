#include "test_hci.h"
#include "../support/test.h"

#include <bluetooth/hci.h>

static void test_opcode_packing(void)
{
    /* HCI_Reset is the well-known opcode 0x0C03 (OGF 0x03, OCF 0x0003). */
    BT_CHECK(BT_HCI_OPCODE_RESET == 0x0C03u);
}

static void test_encode_command(void)
{
    uint8_t buf[16];
    struct bt_buf_writer w;

    bt_buf_writer_init(&w, buf, sizeof(buf));
    BT_CHECK(bt_hci_encode_command(&w, BT_HCI_OPCODE_RESET, NULL, 0) == BT_OK);

    /* Wire bytes for HCI Reset: opcode LE (0x03, 0x0C), param length 0. */
    BT_CHECK(bt_buf_writer_len(&w) == 3);
    BT_CHECK(buf[0] == 0x03 && buf[1] == 0x0C && buf[2] == 0x00);
}

static void test_encode_command_with_params(void)
{
    uint8_t buf[16];
    struct bt_buf_writer w;
    static const uint8_t params[] = {0xaa, 0xbb, 0xcc};

    bt_buf_writer_init(&w, buf, sizeof(buf));
    BT_CHECK(bt_hci_encode_command(&w, 0x1234, params, sizeof(params)) == BT_OK);

    BT_CHECK(bt_buf_writer_len(&w) == 3 + sizeof(params));
    BT_CHECK(buf[0] == 0x34 && buf[1] == 0x12 && buf[2] == 0x03);
    BT_CHECK(buf[3] == 0xaa && buf[4] == 0xbb && buf[5] == 0xcc);
}

static void test_encode_command_too_long(void)
{
    uint8_t buf[16];
    uint8_t params[BT_HCI_MAX_PARAM_LEN + 1] = {0};
    struct bt_buf_writer w;

    bt_buf_writer_init(&w, buf, sizeof(buf));
    BT_CHECK(bt_hci_encode_command(&w, 0x0001, params, sizeof(params)) == BT_ERR_INVALID_ARGUMENT);
}

static void test_parse_command_complete(void)
{
    /* Command Complete for a successful HCI Reset. */
    static const uint8_t wire[] = {0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00};
    struct bt_buf_reader r;
    struct bt_hci_event_header hdr;
    struct bt_hci_command_complete cc;

    bt_buf_reader_init(&r, wire, sizeof(wire));

    BT_CHECK(bt_hci_parse_event_header(&r, &hdr) == BT_OK);
    BT_CHECK(hdr.event_code == BT_HCI_EVENT_COMMAND_COMPLETE);
    BT_CHECK(hdr.param_len == 4);

    BT_CHECK(bt_hci_parse_command_complete(&r, hdr.param_len, &cc) == BT_OK);
    BT_CHECK(cc.num_hci_command_packets == 1);
    BT_CHECK(cc.command_opcode == BT_HCI_OPCODE_RESET);
    BT_CHECK(cc.return_params_len == 1);
    BT_CHECK(cc.return_params[0] == 0x00);

    BT_CHECK(bt_buf_reader_remaining(&r) == 0);
}

static void test_parse_truncated_event(void)
{
    /* Header claims 4 parameter bytes but only 2 are present. */
    static const uint8_t wire[] = {0x0E, 0x04, 0x01, 0x03};
    struct bt_buf_reader r;
    struct bt_hci_event_header hdr;

    bt_buf_reader_init(&r, wire, sizeof(wire));
    BT_CHECK(bt_hci_parse_event_header(&r, &hdr) == BT_ERR_BUFFER_UNDERFLOW);
}

static void test_parse_command_complete_too_short(void)
{
    /* param_len smaller than the fixed 3-byte prefix must be rejected. */
    static const uint8_t wire[] = {0x0E, 0x02, 0x01, 0x03};
    struct bt_buf_reader r;
    struct bt_hci_event_header hdr;
    struct bt_hci_command_complete cc;

    bt_buf_reader_init(&r, wire, sizeof(wire));
    BT_CHECK(bt_hci_parse_event_header(&r, &hdr) == BT_OK);
    BT_CHECK(bt_hci_parse_command_complete(&r, hdr.param_len, &cc) == BT_ERR_BUFFER_UNDERFLOW);
}

void run_hci_tests(void)
{
    test_opcode_packing();
    test_encode_command();
    test_encode_command_with_params();
    test_encode_command_too_long();
    test_parse_command_complete();
    test_parse_truncated_event();
    test_parse_command_complete_too_short();
}
