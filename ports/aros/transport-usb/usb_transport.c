#include "usb_transport.h"

#include <exec/errors.h>
#include <exec/io.h>
#include <proto/exec.h>

#include <string.h>

#define BT_AROS_USB_DEVICE_NAME "usbbluetooth.device"

static void prepare_request(struct bt_aros_hci_request *request,
                            const struct bt_aros_hci_request *control,
                            UWORD command, void *data, size_t length)
{
    request->request.io_Device = control->request.io_Device;
    request->request.io_Unit = control->request.io_Unit;
    request->request.io_Command = command;
    request->request.io_Error = 0;
    request->actual = 0;
    request->length = (ULONG)length;
    request->data = data;
}

static struct bt_aros_hci_request *create_request(
    struct bt_aros_usb_transport *usb)
{
    return (struct bt_aros_hci_request *)CreateIORequest(
        usb->port, sizeof(struct bt_aros_hci_request));
}

static void delete_requests(struct bt_aros_usb_transport *usb)
{
    if (usb->acl_write != NULL)
        DeleteIORequest(&usb->acl_write->request);
    if (usb->command_write != NULL)
        DeleteIORequest(&usb->command_write->request);
    if (usb->acl_read != NULL)
        DeleteIORequest(&usb->acl_read->request);
    if (usb->control != NULL)
        DeleteIORequest(&usb->control->request);
    usb->acl_write = NULL;
    usb->command_write = NULL;
    usb->acl_read = NULL;
    usb->control = NULL;
}

static int usb_open(struct bt_hci_transport *transport)
{
    struct bt_aros_usb_transport *usb = transport->impl;

    if (usb == NULL || usb->opened)
        return BT_ERR_INVALID_ARGUMENT;
    usb->port = CreateMsgPort();
    if (usb->port == NULL)
        return BT_ERR_NO_RESOURCES;
    usb->control = create_request(usb);
    usb->acl_read = create_request(usb);
    usb->command_write = create_request(usb);
    usb->acl_write = create_request(usb);
    if (usb->control == NULL || usb->acl_read == NULL ||
        usb->command_write == NULL || usb->acl_write == NULL)
    {
        delete_requests(usb);
        DeleteMsgPort(usb->port);
        usb->port = NULL;
        return BT_ERR_NO_RESOURCES;
    }
    if (OpenDevice((CONST_STRPTR)BT_AROS_USB_DEVICE_NAME, usb->unit,
                   &usb->control->request, 0) != 0)
    {
        delete_requests(usb);
        DeleteMsgPort(usb->port);
        usb->port = NULL;
        return BT_ERR_IO;
    }
    usb->opened = true;
    return BT_OK;
}

static void abort_request(struct bt_aros_hci_request *request, bool *pending)
{
    if (*pending)
    {
        AbortIO(&request->request);
        WaitIO(&request->request);
        *pending = false;
    }
}

static void usb_stop_receive(struct bt_hci_transport *transport);

static void usb_close(struct bt_hci_transport *transport)
{
    struct bt_aros_usb_transport *usb = transport->impl;

    if (usb == NULL || !usb->opened)
        return;
    usb_stop_receive(transport);
    abort_request(usb->command_write, &usb->command_write_pending);
    abort_request(usb->acl_write, &usb->acl_write_pending);
    CloseDevice(&usb->control->request);
    delete_requests(usb);
    DeleteMsgPort(usb->port);
    usb->port = NULL;
    usb->opened = false;
}

static int submit_write(struct bt_aros_usb_transport *usb,
                        struct bt_aros_hci_request *request, bool *pending,
                        UWORD command, uint8_t *buffer, size_t capacity,
                        const uint8_t *data, size_t length)
{
    if (!usb->opened || data == NULL || length == 0 || length > capacity)
        return BT_ERR_INVALID_ARGUMENT;
    if (*pending)
        return BT_ERR_NO_RESOURCES;
    memcpy(buffer, data, length);
    prepare_request(request, usb->control, command, buffer, length);
    SendIO(&request->request);
    *pending = true;
    return BT_OK;
}

static int usb_send_command(struct bt_hci_transport *transport,
                            const uint8_t *data, size_t length)
{
    struct bt_aros_usb_transport *usb = transport->impl;

    return submit_write(usb, usb->command_write,
                        &usb->command_write_pending, BT_AROS_CMD_WRITE_HCI,
                        usb->command_write_buffer,
                        sizeof(usb->command_write_buffer), data, length);
}

static int usb_send_acl(struct bt_hci_transport *transport,
                        const uint8_t *data, size_t length)
{
    struct bt_aros_usb_transport *usb = transport->impl;

    return submit_write(usb, usb->acl_write, &usb->acl_write_pending,
                        BT_AROS_CMD_WRITE_ACL, usb->acl_write_buffer,
                        sizeof(usb->acl_write_buffer), data, length);
}

static int unsupported_send(struct bt_hci_transport *transport,
                            const uint8_t *data, size_t length)
{
    (void)transport;
    (void)data;
    (void)length;
    return BT_ERR_INVALID_ARGUMENT;
}

static bt_status_t submit_acl_read(struct bt_aros_usb_transport *usb)
{
    prepare_request(usb->acl_read, usb->control, BT_AROS_CMD_READ_ACL,
                    usb->acl_read_buffer, sizeof(usb->acl_read_buffer));
    SendIO(&usb->acl_read->request);
    usb->acl_read_pending = true;
    return BT_OK;
}

static int usb_start_receive(struct bt_hci_transport *transport,
                             bt_hci_transport_recv_fn receive,
                             void *user_data)
{
    struct bt_aros_usb_transport *usb = transport->impl;

    if (usb == NULL || !usb->opened || usb->receiving || receive == NULL)
        return BT_ERR_INVALID_ARGUMENT;
    prepare_request(usb->control, usb->control, BT_AROS_CMD_ADD_MSGPORT,
                    usb->port, sizeof(*usb->port));
    DoIO(&usb->control->request);
    if (usb->control->request.io_Error != 0)
        return BT_ERR_IO;
    usb->receive = receive;
    usb->receive_data = user_data;
    usb->receiving = true;
    submit_acl_read(usb);
    return BT_OK;
}

static void usb_stop_receive(struct bt_hci_transport *transport)
{
    struct bt_aros_usb_transport *usb = transport->impl;

    if (usb == NULL || !usb->receiving)
        return;
    abort_request(usb->acl_read, &usb->acl_read_pending);
    prepare_request(usb->control, usb->control, BT_AROS_CMD_REMOVE_MSGPORT,
                    usb->port, sizeof(*usb->port));
    DoIO(&usb->control->request);
    usb->receive = NULL;
    usb->receive_data = NULL;
    usb->receiving = false;
}

static const struct bt_hci_transport_ops usb_ops = {
    usb_open,
    usb_close,
    usb_send_command,
    usb_send_acl,
    unsupported_send,
    unsupported_send,
    usb_start_receive,
    usb_stop_receive
};

void bt_aros_usb_transport_init(struct bt_aros_usb_transport *usb,
                                unsigned int unit)
{
    if (usb == NULL)
        return;
    memset(usb, 0, sizeof(*usb));
    usb->transport.ops = &usb_ops;
    usb->transport.impl = usb;
    usb->unit = unit;
}

uint32_t bt_aros_usb_transport_signal_mask(
    const struct bt_aros_usb_transport *usb)
{
    if (usb == NULL || usb->port == NULL)
        return 0;
    return (uint32_t)1u << usb->port->mp_SigBit;
}

bt_status_t bt_aros_usb_transport_poll(struct bt_aros_usb_transport *usb)
{
    struct Message *message;
    bt_status_t status = BT_OK;

    if (usb == NULL || !usb->opened)
        return BT_ERR_INVALID_ARGUMENT;
    while ((message = GetMsg(usb->port)) != NULL)
    {
        if (message == (struct Message *)usb->command_write)
        {
            usb->command_write_pending = false;
            if (usb->command_write->request.io_Error != 0)
                status = BT_ERR_IO;
        }
        else if (message == (struct Message *)usb->acl_write)
        {
            usb->acl_write_pending = false;
            if (usb->acl_write->request.io_Error != 0)
                status = BT_ERR_IO;
        }
        else if (message == (struct Message *)usb->acl_read)
        {
            usb->acl_read_pending = false;
            if (usb->acl_read->request.io_Error == 0 && usb->receive != NULL)
                usb->receive(&usb->transport, BT_HCI_PACKET_ACL,
                             usb->acl_read_buffer, usb->acl_read->actual,
                             usb->receive_data);
            else
                status = BT_ERR_IO;
            if (usb->receiving)
                submit_acl_read(usb);
        }
        else
        {
            struct bt_aros_hci_event_message *event =
                (struct bt_aros_hci_event_message *)message;
            if (usb->receive != NULL && event->message.mn_Length >= 2)
                usb->receive(&usb->transport, BT_HCI_PACKET_EVENT,
                             event->event, event->message.mn_Length,
                             usb->receive_data);
            ReplyMsg(message);
        }
    }
    return status;
}
