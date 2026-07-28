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

#define BT_HCI_OGF_INFORMATIONAL 0x04u
#define BT_HCI_OCF_READ_LOCAL_VERSION_INFO 0x0001u
#define BT_HCI_OCF_READ_LOCAL_SUPPORTED_FEATURES 0x0003u
#define BT_HCI_OCF_READ_BUFFER_SIZE 0x0005u
#define BT_HCI_OPCODE_READ_LOCAL_VERSION_INFO \
    BT_HCI_OPCODE(BT_HCI_OGF_INFORMATIONAL, BT_HCI_OCF_READ_LOCAL_VERSION_INFO)
#define BT_HCI_OPCODE_READ_LOCAL_SUPPORTED_FEATURES \
    BT_HCI_OPCODE(BT_HCI_OGF_INFORMATIONAL, BT_HCI_OCF_READ_LOCAL_SUPPORTED_FEATURES)
#define BT_HCI_OPCODE_READ_BUFFER_SIZE \
    BT_HCI_OPCODE(BT_HCI_OGF_INFORMATIONAL, BT_HCI_OCF_READ_BUFFER_SIZE)

#define BT_HCI_EVENT_COMMAND_COMPLETE 0x0Eu
#define BT_HCI_EVENT_COMMAND_STATUS 0x0Fu

#define BT_HCI_COMMAND_HEADER_LEN 3 /* opcode(2) + parameter length(1) */
#define BT_HCI_EVENT_HEADER_LEN 2   /* event code(1) + parameter length(1) */
#define BT_HCI_ACL_HEADER_LEN 4     /* handle+flags(2) + data total length(2) */
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

struct bt_hci_command_status
{
    uint8_t status;
    uint8_t num_hci_command_packets;
    uint16_t command_opcode;
};

/* Parses a Command Status event's parameters. Call after
 * bt_hci_parse_event_header() has confirmed event_code ==
 * BT_HCI_EVENT_COMMAND_STATUS. param_len must be exactly 4, per spec. */
bt_status_t bt_hci_parse_command_status(struct bt_buf_reader *r, uint8_t param_len,
                                         struct bt_hci_command_status *out);

struct bt_hci_acl_header
{
    uint16_t handle;  /* 12-bit connection handle */
    uint8_t pb_flag;  /* 2-bit Packet_Boundary_Flag */
    uint8_t bc_flag;  /* 2-bit Broadcast_Flag */
    uint16_t data_len;
};

bt_status_t bt_hci_encode_acl_header(struct bt_buf_writer *w, uint16_t handle, uint8_t pb_flag,
                                      uint8_t bc_flag, uint16_t data_len);
bt_status_t bt_hci_parse_acl_header(struct bt_buf_reader *r, struct bt_hci_acl_header *out);

/*
 * Response parsers for the Fase 2 initialization sequence. Each takes the
 * return_params slice a bt_hci_command_complete already bounded -- they
 * re-validate the exact expected length rather than trusting the caller.
 */

struct bt_hci_local_version
{
    uint8_t status;
    uint8_t hci_version;
    uint16_t hci_revision;
    uint8_t lmp_pal_version;
    uint16_t manufacturer_name;
    uint16_t lmp_pal_subversion;
};

bt_status_t bt_hci_parse_local_version(const uint8_t *return_params, size_t return_params_len,
                                        struct bt_hci_local_version *out);

struct bt_hci_local_features
{
    uint8_t status;
    uint8_t features[8];
};

bt_status_t bt_hci_parse_local_features(const uint8_t *return_params, size_t return_params_len,
                                         struct bt_hci_local_features *out);

struct bt_hci_buffer_size
{
    uint8_t status;
    uint16_t acl_data_packet_length;
    uint8_t sco_data_packet_length;
    uint16_t total_num_acl_data_packets;
    uint16_t total_num_sco_data_packets;
};

bt_status_t bt_hci_parse_buffer_size(const uint8_t *return_params, size_t return_params_len,
                                      struct bt_hci_buffer_size *out);

#endif /* BLUETOOTH_HCI_H */
