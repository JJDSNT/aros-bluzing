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

bt_status_t bt_hci_parse_command_status(struct bt_buf_reader *r, uint8_t param_len,
                                         struct bt_hci_command_status *out)
{
    uint8_t status;
    uint8_t num_pkts;
    uint16_t opcode;
    bt_status_t st;

    if (param_len != 4)
        return BT_ERR_INVALID_ARGUMENT;

    st = bt_buf_reader_read_u8(r, &status);
    if (st != BT_OK)
        return st;

    st = bt_buf_reader_read_u8(r, &num_pkts);
    if (st != BT_OK)
        return st;

    st = bt_buf_reader_read_le16(r, &opcode);
    if (st != BT_OK)
        return st;

    out->status = status;
    out->num_hci_command_packets = num_pkts;
    out->command_opcode = opcode;
    return BT_OK;
}

bt_status_t bt_hci_encode_acl_header(struct bt_buf_writer *w, uint16_t handle, uint8_t pb_flag,
                                      uint8_t bc_flag, uint16_t data_len)
{
    uint16_t handle_and_flags;
    bt_status_t st;

    if (handle > 0x0fffu || pb_flag > 0x03u || bc_flag > 0x03u)
        return BT_ERR_INVALID_ARGUMENT;

    handle_and_flags = (uint16_t)(handle | ((uint16_t)pb_flag << 12) | ((uint16_t)bc_flag << 14));

    st = bt_buf_writer_write_le16(w, handle_and_flags);
    if (st != BT_OK)
        return st;

    return bt_buf_writer_write_le16(w, data_len);
}

bt_status_t bt_hci_parse_acl_header(struct bt_buf_reader *r, struct bt_hci_acl_header *out)
{
    uint16_t handle_and_flags;
    uint16_t data_len;
    bt_status_t st;

    st = bt_buf_reader_read_le16(r, &handle_and_flags);
    if (st != BT_OK)
        return st;

    st = bt_buf_reader_read_le16(r, &data_len);
    if (st != BT_OK)
        return st;

    out->handle = (uint16_t)(handle_and_flags & 0x0fffu);
    out->pb_flag = (uint8_t)((handle_and_flags >> 12) & 0x03u);
    out->bc_flag = (uint8_t)((handle_and_flags >> 14) & 0x03u);
    out->data_len = data_len;
    return BT_OK;
}

bt_status_t bt_hci_parse_local_version(const uint8_t *return_params, size_t return_params_len,
                                        struct bt_hci_local_version *out)
{
    struct bt_buf_reader r;
    bt_status_t st;

    if (return_params_len != 9)
        return BT_ERR_INVALID_ARGUMENT;

    bt_buf_reader_init(&r, return_params, return_params_len);

    st = bt_buf_reader_read_u8(&r, &out->status);
    if (st != BT_OK)
        return st;
    st = bt_buf_reader_read_u8(&r, &out->hci_version);
    if (st != BT_OK)
        return st;
    st = bt_buf_reader_read_le16(&r, &out->hci_revision);
    if (st != BT_OK)
        return st;
    st = bt_buf_reader_read_u8(&r, &out->lmp_pal_version);
    if (st != BT_OK)
        return st;
    st = bt_buf_reader_read_le16(&r, &out->manufacturer_name);
    if (st != BT_OK)
        return st;
    return bt_buf_reader_read_le16(&r, &out->lmp_pal_subversion);
}

bt_status_t bt_hci_parse_local_features(const uint8_t *return_params, size_t return_params_len,
                                         struct bt_hci_local_features *out)
{
    struct bt_buf_reader r;
    bt_status_t st;

    if (return_params_len != 9)
        return BT_ERR_INVALID_ARGUMENT;

    bt_buf_reader_init(&r, return_params, return_params_len);

    st = bt_buf_reader_read_u8(&r, &out->status);
    if (st != BT_OK)
        return st;

    return bt_buf_reader_read_bytes(&r, out->features, sizeof(out->features));
}

bt_status_t bt_hci_parse_buffer_size(const uint8_t *return_params, size_t return_params_len,
                                      struct bt_hci_buffer_size *out)
{
    struct bt_buf_reader r;
    bt_status_t st;

    if (return_params_len != 8)
        return BT_ERR_INVALID_ARGUMENT;

    bt_buf_reader_init(&r, return_params, return_params_len);

    st = bt_buf_reader_read_u8(&r, &out->status);
    if (st != BT_OK)
        return st;
    st = bt_buf_reader_read_le16(&r, &out->acl_data_packet_length);
    if (st != BT_OK)
        return st;
    st = bt_buf_reader_read_u8(&r, &out->sco_data_packet_length);
    if (st != BT_OK)
        return st;
    st = bt_buf_reader_read_le16(&r, &out->total_num_acl_data_packets);
    if (st != BT_OK)
        return st;
    return bt_buf_reader_read_le16(&r, &out->total_num_sco_data_packets);
}
