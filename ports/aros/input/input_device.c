#include "input_device.h"

#include <devices/input.h>
#include <devices/inputevent.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <proto/exec.h>

#include <string.h>

bt_status_t bt_aros_input_device_open(struct bt_aros_input_device *device)
{
    if (device == NULL || device->opened)
        return BT_ERR_INVALID_ARGUMENT;
    memset(device, 0, sizeof(*device));
    device->port = CreateMsgPort();
    if (device->port == NULL)
        return BT_ERR_NO_RESOURCES;
    device->request = (struct IOStdReq *)CreateIORequest(
        device->port, sizeof(struct IOStdReq));
    if (device->request == NULL)
    {
        DeleteMsgPort(device->port);
        device->port = NULL;
        return BT_ERR_NO_RESOURCES;
    }
    if (OpenDevice((CONST_STRPTR)"input.device", 0,
                   (struct IORequest *)device->request, 0) != 0)
    {
        DeleteIORequest((struct IORequest *)device->request);
        DeleteMsgPort(device->port);
        device->request = NULL;
        device->port = NULL;
        return BT_ERR_IO;
    }
    device->opened = true;
    return BT_OK;
}

void bt_aros_input_device_close(struct bt_aros_input_device *device)
{
    if (device == NULL)
        return;
    if (device->opened)
        CloseDevice((struct IORequest *)device->request);
    if (device->request != NULL)
        DeleteIORequest((struct IORequest *)device->request);
    if (device->port != NULL)
        DeleteMsgPort(device->port);
    memset(device, 0, sizeof(*device));
}

bt_status_t bt_aros_input_device_emit(
    void *context, const struct bt_aros_input_event *event)
{
    struct bt_aros_input_device *device = context;
    struct InputEvent input;

    if (device == NULL || event == NULL || !device->opened)
        return BT_ERR_INVALID_ARGUMENT;
    memset(&input, 0, sizeof(input));
    input.ie_Class = event->event_class;
    input.ie_SubClass = 0;
    input.ie_Code = event->code;
    input.ie_Qualifier = event->qualifier;
    input.ie_X = event->x;
    input.ie_Y = event->y;

    device->request->io_Data = &input;
    device->request->io_Length = sizeof(input);
    device->request->io_Command = IND_WRITEEVENT;
    DoIO((struct IORequest *)device->request);
    return device->request->io_Error == 0 ? BT_OK : BT_ERR_IO;
}
