#include <bluetooth/controller.h>

#include <string.h>

static void on_reset_complete(struct bt_cmdq_completion *completion, void *user_data);
static void on_version_complete(struct bt_cmdq_completion *completion, void *user_data);
static void on_features_complete(struct bt_cmdq_completion *completion, void *user_data);
static void on_buffer_size_complete(struct bt_cmdq_completion *completion, void *user_data);

void bt_controller_init(struct bt_controller *ctrl, struct bt_hci_transport *transport)
{
    ctrl->transport = transport;
    ctrl->state = BT_CONTROLLER_STATE_UNINITIALIZED;
    memset(&ctrl->info, 0, sizeof(ctrl->info));
    bt_timer_list_init(&ctrl->timers);
    bt_cmdq_init(&ctrl->cmdq, transport, &ctrl->timers);
}

bt_status_t bt_controller_start(struct bt_controller *ctrl, uint64_t now_us)
{
    bt_status_t st;

    if (ctrl->state != BT_CONTROLLER_STATE_UNINITIALIZED)
        return BT_ERR_INVALID_ARGUMENT;

    st = bt_cmdq_submit(&ctrl->cmdq, BT_HCI_OPCODE_RESET, NULL, 0, 0, on_reset_complete, ctrl);
    if (st != BT_OK)
        return st;

    ctrl->state = BT_CONTROLLER_STATE_RESETTING;
    bt_cmdq_pump(&ctrl->cmdq, now_us);
    return BT_OK;
}

void bt_controller_on_event(struct bt_controller *ctrl, const uint8_t *data, size_t length,
                             uint64_t now_us)
{
    bt_cmdq_on_event(&ctrl->cmdq, data, length, now_us);
}

void bt_controller_tick(struct bt_controller *ctrl, uint64_t now_us)
{
    bt_cmdq_tick(&ctrl->cmdq, now_us);
}

static bool step_ok(struct bt_controller *ctrl, struct bt_cmdq_completion *completion)
{
    if (completion->result != BT_CMDQ_RESULT_COMPLETE || completion->status != 0x00)
    {
        ctrl->state = BT_CONTROLLER_STATE_ERROR;
        return false;
    }
    return true;
}

static void on_reset_complete(struct bt_cmdq_completion *completion, void *user_data)
{
    struct bt_controller *ctrl = (struct bt_controller *)user_data;

    if (!step_ok(ctrl, completion))
        return;

    ctrl->state = BT_CONTROLLER_STATE_READING_VERSION;
    if (bt_cmdq_submit(&ctrl->cmdq, BT_HCI_OPCODE_READ_LOCAL_VERSION_INFO, NULL, 0, 0,
                        on_version_complete, ctrl) != BT_OK)
        ctrl->state = BT_CONTROLLER_STATE_ERROR;
}

static void on_version_complete(struct bt_cmdq_completion *completion, void *user_data)
{
    struct bt_controller *ctrl = (struct bt_controller *)user_data;

    if (!step_ok(ctrl, completion))
        return;

    if (bt_hci_parse_local_version(completion->return_params, completion->return_params_len,
                                    &ctrl->info.version) != BT_OK)
    {
        ctrl->state = BT_CONTROLLER_STATE_ERROR;
        return;
    }

    ctrl->state = BT_CONTROLLER_STATE_READING_FEATURES;
    if (bt_cmdq_submit(&ctrl->cmdq, BT_HCI_OPCODE_READ_LOCAL_SUPPORTED_FEATURES, NULL, 0, 0,
                        on_features_complete, ctrl) != BT_OK)
        ctrl->state = BT_CONTROLLER_STATE_ERROR;
}

static void on_features_complete(struct bt_cmdq_completion *completion, void *user_data)
{
    struct bt_controller *ctrl = (struct bt_controller *)user_data;

    if (!step_ok(ctrl, completion))
        return;

    if (bt_hci_parse_local_features(completion->return_params, completion->return_params_len,
                                     &ctrl->info.features) != BT_OK)
    {
        ctrl->state = BT_CONTROLLER_STATE_ERROR;
        return;
    }

    ctrl->state = BT_CONTROLLER_STATE_READING_BUFFER_SIZE;
    if (bt_cmdq_submit(&ctrl->cmdq, BT_HCI_OPCODE_READ_BUFFER_SIZE, NULL, 0, 0,
                        on_buffer_size_complete, ctrl) != BT_OK)
        ctrl->state = BT_CONTROLLER_STATE_ERROR;
}

static void on_buffer_size_complete(struct bt_cmdq_completion *completion, void *user_data)
{
    struct bt_controller *ctrl = (struct bt_controller *)user_data;

    if (!step_ok(ctrl, completion))
        return;

    if (bt_hci_parse_buffer_size(completion->return_params, completion->return_params_len,
                                  &ctrl->info.buffer_size) != BT_OK)
    {
        ctrl->state = BT_CONTROLLER_STATE_ERROR;
        return;
    }

    ctrl->state = BT_CONTROLLER_STATE_READY;
}
