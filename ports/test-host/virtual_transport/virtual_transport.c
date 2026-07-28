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

/* Emits a Command Complete event: num_hci_command_packets=1, the given
 * opcode, and whatever return_params the caller already built (status
 * byte included, since its position/meaning varies per command). */
static void emit_command_complete(struct bt_hci_transport *transport, struct bt_virtual_transport *vt,
                                   uint16_t opcode, const uint8_t *return_params,
                                   uint8_t return_params_len)
{
    uint8_t event[BT_HCI_EVENT_HEADER_LEN + 3 + 16];
    struct bt_buf_writer w;

    bt_buf_writer_init(&w, event, sizeof(event));
    bt_buf_writer_write_u8(&w, BT_HCI_EVENT_COMMAND_COMPLETE);
    bt_buf_writer_write_u8(&w, (uint8_t)(3 + return_params_len));
    bt_buf_writer_write_u8(&w, 1); /* num_hci_command_packets */
    bt_buf_writer_write_le16(&w, opcode);
    bt_buf_writer_write_bytes(&w, return_params, return_params_len);

    if (vt->recv != NULL)
        vt->recv(transport, BT_HCI_PACKET_EVENT, event, bt_buf_writer_len(&w), vt->recv_user_data);
}

static int vt_send_command(struct bt_hci_transport *transport, const uint8_t *data, size_t length)
{
    struct bt_virtual_transport *vt = vt_of(transport);
    struct bt_buf_reader r;
    uint16_t opcode;
    uint8_t param_len;
    uint8_t rp[16];
    struct bt_buf_writer rpw;

    if (!vt->is_open)
        return -1;

    bt_buf_reader_init(&r, data, length);
    if (bt_buf_reader_read_le16(&r, &opcode) != BT_OK)
        return -1;
    if (bt_buf_reader_read_u8(&r, &param_len) != BT_OK)
        return -1;
    if (bt_buf_reader_remaining(&r) != param_len)
        return -1; /* malformed command: declared length doesn't match payload */

    bt_buf_writer_init(&rpw, rp, sizeof(rp));

    switch (opcode)
    {
    case BT_HCI_OPCODE_RESET:
        vt->reset_done = true;
        bt_buf_writer_write_u8(&rpw, 0x00); /* status */
        break;

    case BT_HCI_OPCODE_READ_LOCAL_VERSION_INFO:
        bt_buf_writer_write_u8(&rpw, 0x00);   /* status */
        bt_buf_writer_write_u8(&rpw, 0x0c);   /* hci_version */
        bt_buf_writer_write_le16(&rpw, 0x0000); /* hci_revision */
        bt_buf_writer_write_u8(&rpw, 0x0c);   /* lmp_pal_version */
        bt_buf_writer_write_le16(&rpw, 0xffff); /* manufacturer_name (test value) */
        bt_buf_writer_write_le16(&rpw, 0x0001); /* lmp_pal_subversion */
        break;

    case BT_HCI_OPCODE_READ_LOCAL_SUPPORTED_FEATURES:
        {
            static const uint8_t features[8] = {0xff, 0xfe, 0x8d, 0xfe, 0x9b, 0xf9, 0x00, 0x80};
            bt_buf_writer_write_u8(&rpw, 0x00); /* status */
            bt_buf_writer_write_bytes(&rpw, features, sizeof(features));
        }
        break;

    case BT_HCI_OPCODE_READ_BUFFER_SIZE:
        bt_buf_writer_write_u8(&rpw, 0x00);      /* status */
        bt_buf_writer_write_le16(&rpw, 200);     /* acl_data_packet_length */
        bt_buf_writer_write_u8(&rpw, 0);         /* sco_data_packet_length */
        bt_buf_writer_write_le16(&rpw, 8);       /* total_num_acl_data_packets */
        bt_buf_writer_write_le16(&rpw, 0);       /* total_num_sco_data_packets */
        break;

    default:
        return 0; /* not modeled yet: silently accepted, no event generated */
    }

    emit_command_complete(transport, vt, opcode, rp, (uint8_t)bt_buf_writer_len(&rpw));
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
