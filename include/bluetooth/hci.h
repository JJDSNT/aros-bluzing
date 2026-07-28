#ifndef BLUETOOTH_HCI_H
#define BLUETOOTH_HCI_H

#include <bluetooth/buffer.h>
#include <bluetooth/status.h>
#include <bluetooth/types.h>

/* HCI opcodes pack a 6-bit OGF and a 10-bit OCF: opcode = (ogf << 10) | ocf. */
#define BT_HCI_OPCODE(ogf, ocf) ((uint16_t)(((uint16_t)(ogf) << 10) | ((uint16_t)(ocf) & 0x03ffu)))

#define BT_HCI_OGF_CONTROLLER_BASEBAND 0x03u
#define BT_HCI_OCF_RESET 0x0003u
#define BT_HCI_OPCODE_RESET BT_HCI_OPCODE(BT_HCI_OGF_CONTROLLER_BASEBAND, BT_HCI_OCF_RESET)

#define BT_HCI_EVENT_COMMAND_COMPLETE 0x0Eu

#define BT_HCI_COMMAND_HEADER_LEN 3 /* opcode(2) + parameter length(1) */
#define BT_HCI_EVENT_HEADER_LEN 2   /* event code(1) + parameter length(1) */
#define BT_HCI_MAX_PARAM_LEN 255    /* HCI parameter length field is one byte wide */

/* Encodes a full HCI Command packet (header + parameters) into w. */
bt_status_t bt_hci_encode_command(struct bt_buf_writer *w, uint16_t opcode,
                                   const uint8_t *params, size_t params_len);

struct bt_hci_event_header
{
    uint8_t event_code;
    uint8_t param_len;
};

/* Parses the 2-byte event header. On success, param_len bytes are guaranteed
 * to remain in r (already bounds-checked), so callers may read them without
 * rechecking. */
bt_status_t bt_hci_parse_event_header(struct bt_buf_reader *r, struct bt_hci_event_header *out);

struct bt_hci_command_complete
{
    uint8_t num_hci_command_packets;
    uint16_t command_opcode;
    const uint8_t *return_params; /* points into the reader's underlying buffer */
    size_t return_params_len;
};

/* Parses a Command Complete event's parameters. Call after
 * bt_hci_parse_event_header() has confirmed event_code ==
 * BT_HCI_EVENT_COMMAND_COMPLETE, passing that header's param_len. */
bt_status_t bt_hci_parse_command_complete(struct bt_buf_reader *r, uint8_t param_len,
                                           struct bt_hci_command_complete *out);

#endif /* BLUETOOTH_HCI_H */
