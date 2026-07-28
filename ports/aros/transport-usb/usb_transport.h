#ifndef AROS_BLUZING_USB_TRANSPORT_H
#define AROS_BLUZING_USB_TRANSPORT_H

#include <bluetooth/status.h>
#include <bluetooth/transport.h>

#include <exec/io.h>
#include <exec/ports.h>

#define BT_AROS_USB_COMMAND_MAX 260u
#define BT_AROS_USB_ACL_MAX 4096u

/* Minimal, ABI-compatible view of devices/bluetoothhci.h.  That legacy
 * header includes "bluetooth/hci.h", which collides with this stack's public
 * header of the same name when the project include directory comes first. */
struct bt_aros_hci_request
{
    struct IORequest request;
    ULONG actual;
    ULONG length;
    APTR data;
    APTR user_data;
};

struct bt_aros_hci_event_message
{
    struct Message message;
    uint8_t event[257];
};

#define BT_AROS_CMD_WRITE_HCI (CMD_NONSTD + 1)
#define BT_AROS_CMD_READ_ACL (CMD_NONSTD + 3)
#define BT_AROS_CMD_WRITE_ACL (CMD_NONSTD + 4)
#define BT_AROS_CMD_ADD_MSGPORT (CMD_NONSTD + 8)
#define BT_AROS_CMD_REMOVE_MSGPORT (CMD_NONSTD + 9)

struct bt_aros_usb_transport
{
    struct bt_hci_transport transport;
    struct MsgPort *port;
    struct bt_aros_hci_request *control;
    struct bt_aros_hci_request *acl_read;
    struct bt_aros_hci_request *command_write;
    struct bt_aros_hci_request *acl_write;
    bt_hci_transport_recv_fn receive;
    void *receive_data;
    uint8_t acl_read_buffer[BT_AROS_USB_ACL_MAX];
    uint8_t command_write_buffer[BT_AROS_USB_COMMAND_MAX];
    uint8_t acl_write_buffer[BT_AROS_USB_ACL_MAX];
    unsigned int unit;
    bool opened;
    bool receiving;
    bool acl_read_pending;
    bool command_write_pending;
    bool acl_write_pending;
};

void bt_aros_usb_transport_init(struct bt_aros_usb_transport *usb,
                                unsigned int unit);

/* Signal bit to include in the owning Manager Task's Wait() mask. */
uint32_t bt_aros_usb_transport_signal_mask(
    const struct bt_aros_usb_transport *usb);

/* Drain completed I/O and HCI event messages. Call only from the owner task. */
bt_status_t bt_aros_usb_transport_poll(struct bt_aros_usb_transport *usb);

#endif /* AROS_BLUZING_USB_TRANSPORT_H */
