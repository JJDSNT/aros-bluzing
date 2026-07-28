#include "virtual_transport.h"

#include <bluetooth/buffer.h>
#include <bluetooth/hci.h>

#include <string.h>

static struct bt_virtual_transport *vt_of(struct bt_hci_transport *transport)
{
    return (struct bt_virtual_transport *)transport->impl;
}

static int vt_open(struct bt_hci_transport *transport)
{
    struct bt_virtual_transport *vt = vt_of(transport);

    vt->is_open = true;
    vt->reset_done = false;
    return 0;
}

static void vt_close(struct bt_hci_transport *transport)
{
    vt_of(transport)->is_open = false;
}

static int vt_send_command(struct bt_hci_transport *transport, const uint8_t *data, size_t length)
{
    struct bt_virtual_transport *vt = vt_of(transport);
    struct bt_buf_reader r;
    struct bt_buf_writer w;
    uint16_t opcode;
    uint8_t param_len;
    uint8_t event[BT_HCI_EVENT_HEADER_LEN + 4];

    if (!vt->is_open)
        return -1;

    bt_buf_reader_init(&r, data, length);
    if (bt_buf_reader_read_le16(&r, &opcode) != BT_OK)
        return -1;
    if (bt_buf_reader_read_u8(&r, &param_len) != BT_OK)
        return -1;
    if (bt_buf_reader_remaining(&r) != param_len)
        return -1; /* malformed command: declared length doesn't match payload */

    if (opcode != BT_HCI_OPCODE_RESET)
        return 0; /* not modeled yet: silently accepted, no event generated */

    vt->reset_done = true;

    bt_buf_writer_init(&w, event, sizeof(event));
    bt_buf_writer_write_u8(&w, BT_HCI_EVENT_COMMAND_COMPLETE);
    bt_buf_writer_write_u8(&w, 4); /* num_hci_command_packets(1) + opcode(2) + status(1) */
    bt_buf_writer_write_u8(&w, 1); /* num_hci_command_packets */
    bt_buf_writer_write_le16(&w, BT_HCI_OPCODE_RESET);
    bt_buf_writer_write_u8(&w, 0x00); /* status: success */

    if (vt->recv != NULL)
        vt->recv(transport, BT_HCI_PACKET_EVENT, event, bt_buf_writer_len(&w), vt->recv_user_data);

    return 0;
}

static int vt_send_unsupported(struct bt_hci_transport *transport, const uint8_t *data, size_t length)
{
    (void)transport;
    (void)data;
    (void)length;
    return -1; /* ACL/SCO/ISO not modeled by the virtual controller yet */
}

static int vt_start_receive(struct bt_hci_transport *transport, bt_hci_transport_recv_fn recv,
                             void *user_data)
{
    struct bt_virtual_transport *vt = vt_of(transport);

    vt->recv = recv;
    vt->recv_user_data = user_data;
    return 0;
}

static void vt_stop_receive(struct bt_hci_transport *transport)
{
    struct bt_virtual_transport *vt = vt_of(transport);

    vt->recv = NULL;
    vt->recv_user_data = NULL;
}

static const struct bt_hci_transport_ops bt_virtual_transport_ops = {
    .open = vt_open,
    .close = vt_close,
    .send_command = vt_send_command,
    .send_acl = vt_send_unsupported,
    .send_sco = vt_send_unsupported,
    .send_iso = vt_send_unsupported,
    .start_receive = vt_start_receive,
    .stop_receive = vt_stop_receive,
};

void bt_virtual_transport_init(struct bt_virtual_transport *vt)
{
    memset(vt, 0, sizeof(*vt));
    vt->base.ops = &bt_virtual_transport_ops;
    vt->base.impl = vt;
}
