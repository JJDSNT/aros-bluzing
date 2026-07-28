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

static void test_parse_command_status(void)
{
    static const uint8_t wire[] = {0x0F, 0x04, 0x00, 0x01, 0x03, 0x0C};
    struct bt_buf_reader r;
    struct bt_hci_event_header hdr;
    struct bt_hci_command_status cs;

    bt_buf_reader_init(&r, wire, sizeof(wire));
    BT_CHECK(bt_hci_parse_event_header(&r, &hdr) == BT_OK);
    BT_CHECK(hdr.event_code == BT_HCI_EVENT_COMMAND_STATUS);

    BT_CHECK(bt_hci_parse_command_status(&r, hdr.param_len, &cs) == BT_OK);
    BT_CHECK(cs.status == 0x00);
    BT_CHECK(cs.num_hci_command_packets == 1);
    BT_CHECK(cs.command_opcode == BT_HCI_OPCODE_RESET);
}

static void test_parse_command_status_wrong_length(void)
{
    static const uint8_t wire[] = {0x0F, 0x03, 0x00, 0x01, 0x03};
    struct bt_buf_reader r;
    struct bt_hci_event_header hdr;
    struct bt_hci_command_status cs;

    bt_buf_reader_init(&r, wire, sizeof(wire));
    BT_CHECK(bt_hci_parse_event_header(&r, &hdr) == BT_OK);
    BT_CHECK(bt_hci_parse_command_status(&r, hdr.param_len, &cs) == BT_ERR_INVALID_ARGUMENT);
}

static void test_acl_header_round_trip(void)
{
    uint8_t buf[BT_HCI_ACL_HEADER_LEN];
    struct bt_buf_writer w;
    struct bt_buf_reader r;
    struct bt_hci_acl_header hdr;

    bt_buf_writer_init(&w, buf, sizeof(buf));
    BT_CHECK(bt_hci_encode_acl_header(&w, 0x0041, 0x02, 0x00, 23) == BT_OK);

    /* Handle 0x041 (12 bits) | PB=2 (bits 12-13) | BC=0 (bits 14-15) -> 0x2041. */
    BT_CHECK(buf[0] == 0x41 && buf[1] == 0x20);
    BT_CHECK(buf[2] == 23 && buf[3] == 0x00);

    bt_buf_reader_init(&r, buf, sizeof(buf));
    BT_CHECK(bt_hci_parse_acl_header(&r, &hdr) == BT_OK);
    BT_CHECK(hdr.handle == 0x0041);
    BT_CHECK(hdr.pb_flag == 0x02);
    BT_CHECK(hdr.bc_flag == 0x00);
    BT_CHECK(hdr.data_len == 23);
}

static void test_acl_header_rejects_out_of_range(void)
{
    uint8_t buf[BT_HCI_ACL_HEADER_LEN];
    struct bt_buf_writer w;

    bt_buf_writer_init(&w, buf, sizeof(buf));
    BT_CHECK(bt_hci_encode_acl_header(&w, 0x1000, 0, 0, 0) == BT_ERR_INVALID_ARGUMENT);
    bt_buf_writer_init(&w, buf, sizeof(buf));
    BT_CHECK(bt_hci_encode_acl_header(&w, 0, 4, 0, 0) == BT_ERR_INVALID_ARGUMENT);
}

static void test_parse_local_version(void)
{
    /* status=0, hci_version=0x0b, hci_revision=0x1234, lmp_pal_version=0x0b,
     * manufacturer=0x000f, lmp_pal_subversion=0x5678 */
    static const uint8_t rp[] = {0x00, 0x0b, 0x34, 0x12, 0x0b, 0x0f, 0x00, 0x78, 0x56};
    struct bt_hci_local_version v;

    BT_CHECK(bt_hci_parse_local_version(rp, sizeof(rp), &v) == BT_OK);
    BT_CHECK(v.status == 0x00);
    BT_CHECK(v.hci_version == 0x0b);
    BT_CHECK(v.hci_revision == 0x1234);
    BT_CHECK(v.lmp_pal_version == 0x0b);
    BT_CHECK(v.manufacturer_name == 0x000f);
    BT_CHECK(v.lmp_pal_subversion == 0x5678);

    BT_CHECK(bt_hci_parse_local_version(rp, sizeof(rp) - 1, &v) == BT_ERR_INVALID_ARGUMENT);
}

static void test_parse_local_features(void)
{
    static const uint8_t rp[] = {0x00, 0xff, 0xfe, 0x8d, 0xfe, 0x9b, 0xf9, 0x00, 0x80};
    struct bt_hci_local_features f;

    BT_CHECK(bt_hci_parse_local_features(rp, sizeof(rp), &f) == BT_OK);
    BT_CHECK(f.status == 0x00);
    BT_CHECK(f.features[0] == 0xff && f.features[6] == 0x00 && f.features[7] == 0x80);

    BT_CHECK(bt_hci_parse_local_features(rp, sizeof(rp) - 1, &f) == BT_ERR_INVALID_ARGUMENT);
}

static void test_parse_buffer_size(void)
{
    /* status=0, acl_len=0x00fb (251), sco_len=0, total_acl=0x000a (10), total_sco=0 */
    static const uint8_t rp[] = {0x00, 0xfb, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00};
    struct bt_hci_buffer_size bs;

    BT_CHECK(bt_hci_parse_buffer_size(rp, sizeof(rp), &bs) == BT_OK);
    BT_CHECK(bs.status == 0x00);
    BT_CHECK(bs.acl_data_packet_length == 251);
    BT_CHECK(bs.sco_data_packet_length == 0);
    BT_CHECK(bs.total_num_acl_data_packets == 10);
    BT_CHECK(bs.total_num_sco_data_packets == 0);

    BT_CHECK(bt_hci_parse_buffer_size(rp, sizeof(rp) - 1, &bs) == BT_ERR_INVALID_ARGUMENT);
}

static void test_encode_inquiry(void)
{
    uint8_t buf[16];
    struct bt_buf_writer w;

    bt_buf_writer_init(&w, buf, sizeof(buf));
    BT_CHECK(bt_hci_encode_inquiry(&w, BT_HCI_GIAC_LAP, 0x08, 0x00) == BT_OK);

    /* opcode LE (0x01,0x04 -> OGF 1, OCF 1), length 5, LAP LE (0x33,0x8B,0x9E),
     * inquiry_length, num_responses. */
    BT_CHECK(bt_buf_writer_len(&w) == 3 + 5);
    BT_CHECK(buf[0] == 0x01 && buf[1] == 0x04 && buf[2] == 0x05);
    BT_CHECK(buf[3] == 0x33 && buf[4] == 0x8B && buf[5] == 0x9E);
    BT_CHECK(buf[6] == 0x08 && buf[7] == 0x00);
}

static void test_inquiry_result_iter(void)
{
    /* num_responses=2, then two 14-byte entries. */
    static const uint8_t wire[] = {
        0x02,
        /* entry 1: addr AA:BB:CC:DD:EE:01, pscan_rep=0x01, reserved=0000,
         * class_of_device LE 0x123456, clock_offset LE 0x1122 */
        0x01, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x01, 0x00, 0x00, 0x56, 0x34, 0x12, 0x22, 0x11,
        /* entry 2: addr AA:BB:CC:DD:EE:02, pscan_rep=0x00, reserved=0000,
         * class_of_device LE 0x000000, clock_offset LE 0x0000 */
        0x02, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    struct bt_hci_inquiry_result_iter it;
    struct bt_hci_inquiry_result_entry entry;

    BT_CHECK(bt_hci_inquiry_result_iter_init(&it, wire, sizeof(wire)) == BT_OK);

    BT_CHECK(bt_hci_inquiry_result_iter_next(&it, &entry) == BT_OK);
    BT_CHECK(entry.bd_addr.b[0] == 0x01 && entry.bd_addr.b[5] == 0xAA);
    BT_CHECK(entry.page_scan_repetition_mode == 0x01);
    BT_CHECK(entry.class_of_device == 0x123456);
    BT_CHECK(entry.clock_offset == 0x1122);

    BT_CHECK(bt_hci_inquiry_result_iter_next(&it, &entry) == BT_OK);
    BT_CHECK(entry.bd_addr.b[0] == 0x02);
    BT_CHECK(entry.class_of_device == 0x000000);

    BT_CHECK(bt_hci_inquiry_result_iter_next(&it, &entry) == BT_ERR_BUFFER_UNDERFLOW);
}

static void test_encode_le_scan(void)
{
    uint8_t buf[16];
    struct bt_buf_writer w;

    bt_buf_writer_init(&w, buf, sizeof(buf));
    BT_CHECK(bt_hci_encode_le_set_scan_parameters(&w, 0x01, 0x0010, 0x0010, 0x00, 0x00) == BT_OK);
    BT_CHECK(bt_buf_writer_len(&w) == 3 + 7);
    BT_CHECK(buf[0] == 0x0B && buf[1] == 0x20); /* opcode LE for OGF 0x08, OCF 0x000B */

    bt_buf_writer_init(&w, buf, sizeof(buf));
    BT_CHECK(bt_hci_encode_le_set_scan_enable(&w, 0x01, 0x00) == BT_OK);
    BT_CHECK(bt_buf_writer_len(&w) == 3 + 2);
    BT_CHECK(buf[0] == 0x0C && buf[1] == 0x20);
    BT_CHECK(buf[3] == 0x01 && buf[4] == 0x00);
}

static void test_le_adv_report_iter(void)
{
    static const uint8_t wire[] = {
        BT_HCI_LE_META_SUBEVENT_ADVERTISING_REPORT,
        0x02, /* num_reports */
        /* report 1: event_type=0x00, addr_type=0x00, addr ..01, data_len=2, data={0xAA,0xBB}, rssi=-40 */
        0x00, 0x00, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x01, 0x02, 0xAA, 0xBB, (uint8_t)-40,
        /* report 2: event_type=0x04, addr_type=0x01, addr ..02, data_len=0, rssi=-70 */
        0x04, 0x01, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x02, 0x00, (uint8_t)-70,
    };
    struct bt_hci_le_adv_report_iter it;
    struct bt_hci_le_adv_report report;

    BT_CHECK(bt_hci_le_adv_report_iter_init(&it, wire, sizeof(wire)) == BT_OK);

    BT_CHECK(bt_hci_le_adv_report_iter_next(&it, &report) == BT_OK);
    BT_CHECK(report.event_type == 0x00);
    BT_CHECK(report.address_type == 0x00);
    BT_CHECK(report.address.b[0] == 0xEE && report.address.b[5] == 0x01);
    BT_CHECK(report.data_len == 2);
    BT_CHECK(report.data[0] == 0xAA && report.data[1] == 0xBB);
    BT_CHECK(report.rssi == -40);

    BT_CHECK(bt_hci_le_adv_report_iter_next(&it, &report) == BT_OK);
    BT_CHECK(report.event_type == 0x04);
    BT_CHECK(report.data_len == 0);
    BT_CHECK(report.rssi == -70);

    BT_CHECK(bt_hci_le_adv_report_iter_next(&it, &report) == BT_ERR_BUFFER_UNDERFLOW);
}

static void test_le_adv_report_iter_rejects_wrong_subevent(void)
{
    static const uint8_t wire[] = {0x01, 0x00}; /* not an advertising report */
    struct bt_hci_le_adv_report_iter it;

    BT_CHECK(bt_hci_le_adv_report_iter_init(&it, wire, sizeof(wire)) == BT_ERR_INVALID_ARGUMENT);
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
    test_parse_command_status();
    test_parse_command_status_wrong_length();
    test_acl_header_round_trip();
    test_acl_header_rejects_out_of_range();
    test_parse_local_version();
    test_parse_local_features();
    test_parse_buffer_size();
    test_encode_inquiry();
    test_inquiry_result_iter();
    test_encode_le_scan();
    test_le_adv_report_iter();
    test_le_adv_report_iter_rejects_wrong_subevent();
}
