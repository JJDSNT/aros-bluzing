#ifndef BT_AROS_INPUT_DEVICE_H
#define BT_AROS_INPUT_DEVICE_H

#include <bluetooth/aros_input_bridge.h>

struct MsgPort;
struct IOStdReq;

struct bt_aros_input_device
{
    struct MsgPort *port;
    struct IOStdReq *request;
    bool opened;
};

bt_status_t bt_aros_input_device_open(struct bt_aros_input_device *device);
void bt_aros_input_device_close(struct bt_aros_input_device *device);

/* Matches bt_aros_input_emit_fn. Calls must originate from the Bluetooth
 * Manager Task; DoIO is synchronous and the InputEvent is stack-backed. */
bt_status_t bt_aros_input_device_emit(
    void *context, const struct bt_aros_input_event *event);

#endif /* BT_AROS_INPUT_DEVICE_H */
