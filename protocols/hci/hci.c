#include <bluetooth/hci.h>

bt_status_t bt_hci_encode_command(struct bt_buf_writer *w, uint16_t opcode,
                                   const uint8_t *params, size_t params_len)
{
    bt_status_t st;

    if (params_len > BT_HCI_MAX_PARAM_LEN)
        return BT_ERR_INVALID_ARGUMENT;

    st = bt_buf_writer_write_le16(w, opcode);
    if (st != BT_OK)
        return st;

    st = bt_buf_writer_write_u8(w, (uint8_t)params_len);
    if (st != BT_OK)
        return st;

    if (params_len == 0)
        return BT_OK;

    return bt_buf_writer_write_bytes(w, params, params_len);
}

bt_status_t bt_hci_parse_event_header(struct bt_buf_reader *r, struct bt_hci_event_header *out)
{
    uint8_t event_code;
    uint8_t param_len;
    bt_status_t st;

    st = bt_buf_reader_read_u8(r, &event_code);
    if (st != BT_OK)
        return st;

    st = bt_buf_reader_read_u8(r, &param_len);
    if (st != BT_OK)
        return st;

    if (bt_buf_reader_remaining(r) < param_len)
        return BT_ERR_BUFFER_UNDERFLOW;

    out->event_code = event_code;
    out->param_len = param_len;
    return BT_OK;
}

bt_status_t bt_hci_parse_command_complete(struct bt_buf_reader *r, uint8_t param_len,
                                           struct bt_hci_command_complete *out)
{
    uint8_t num_pkts;
    uint16_t opcode;
    size_t return_len;
    const uint8_t *p;
    bt_status_t st;

    /* num_hci_command_packets(1) + command_opcode(2) is the fixed prefix. */
    if (param_len < 3)
        return BT_ERR_BUFFER_UNDERFLOW;

    st = bt_buf_reader_read_u8(r, &num_pkts);
    if (st != BT_OK)
        return st;

    st = bt_buf_reader_read_le16(r, &opcode);
    if (st != BT_OK)
        return st;

    return_len = (size_t)param_len - 3;
    p = bt_buf_reader_peek(r, return_len);
    if (p == NULL)
        return BT_ERR_BUFFER_UNDERFLOW;

    st = bt_buf_reader_skip(r, return_len);
    if (st != BT_OK)
        return st;

    out->num_hci_command_packets = num_pkts;
    out->command_opcode = opcode;
    out->return_params = p;
    out->return_params_len = return_len;
    return BT_OK;
}
