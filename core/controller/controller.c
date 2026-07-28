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
    bt_device_registry_init(&ctrl->devices);
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
    struct bt_buf_reader r;
    struct bt_hci_event_header hdr;
    const uint8_t *params;

    /* Command Complete/Status for whatever this controller has
     * outstanding, regardless of event type below. */
    bt_cmdq_on_event(&ctrl->cmdq, data, length, now_us);

    /* Independently inspect the same event for discovery events. These
     * never share an event code with Command Complete/Status, so
     * re-parsing the header here is safe and keeps cmdq ignorant of
     * anything beyond generic command completion. */
    bt_buf_reader_init(&r, data, length);
    if (bt_hci_parse_event_header(&r, &hdr) != BT_OK)
        return;

    params = bt_buf_reader_peek(&r, hdr.param_len);
    if (params == NULL)
        return;

    if (hdr.event_code == BT_HCI_EVENT_INQUIRY_RESULT)
    {
        struct bt_hci_inquiry_result_iter it;
        struct bt_hci_inquiry_result_entry entry;

        if (bt_hci_inquiry_result_iter_init(&it, params, hdr.param_len) != BT_OK)
            return;
        while (bt_hci_inquiry_result_iter_next(&it, &entry) == BT_OK)
            bt_device_registry_note_classic(&ctrl->devices, &entry.bd_addr, entry.class_of_device);
    }
    else if (hdr.event_code == BT_HCI_EVENT_LE_META)
    {
        struct bt_hci_le_adv_report_iter it;
        struct bt_hci_le_adv_report report;

        if (bt_hci_le_adv_report_iter_init(&it, params, hdr.param_len) != BT_OK)
            return; /* not an advertising-report subevent; nothing to do here */
        while (bt_hci_le_adv_report_iter_next(&it, &report) == BT_OK)
            bt_device_registry_note_le(&ctrl->devices, &report.address, report.address_type,
                                        report.rssi);
    }
}

void bt_controller_tick(struct bt_controller *ctrl, uint64_t now_us)
{
    bt_cmdq_tick(&ctrl->cmdq, now_us);
}

static void ignore_completion(struct bt_cmdq_completion *completion, void *user_data)
{
    /* Inquiry/LE-scan-enable complete via Command Status; nothing further
     * to do here (results stream in separately as discovery events). A
     * failing status is silently dropped for now -- there's no discovery
     * error signal yet to surface it through. */
    (void)completion;
    (void)user_data;
}

/*
 * bt_cmdq_submit() wants raw command parameters (it encodes the
 * opcode+length header itself in bt_cmdq_pump()) -- so these build just
 * the parameter bytes with a plain writer, rather than going through
 * bt_hci_encode_inquiry()/bt_hci_encode_le_set_scan_*() in bluetooth/hci.h,
 * which produce a full ready-to-send command (header included) for
 * callers that talk to a transport directly.
 */

bt_status_t bt_controller_start_classic_inquiry(struct bt_controller *ctrl, uint8_t inquiry_length,
                                                 uint64_t now_us)
{
    uint8_t params[5];
    struct bt_buf_writer w;
    bt_status_t st;

    if (ctrl->state != BT_CONTROLLER_STATE_READY)
        return BT_ERR_INVALID_ARGUMENT;

    bt_buf_writer_init(&w, params, sizeof(params));
    bt_buf_writer_write_le24(&w, BT_HCI_GIAC_LAP);
    bt_buf_writer_write_u8(&w, inquiry_length);
    bt_buf_writer_write_u8(&w, 0); /* num_responses: 0 = unlimited */

    st = bt_cmdq_submit(&ctrl->cmdq, BT_HCI_OPCODE_INQUIRY, params,
                         (uint8_t)bt_buf_writer_len(&w), 0, ignore_completion, ctrl);
    if (st != BT_OK)
        return st;

    bt_cmdq_pump(&ctrl->cmdq, now_us);
    return BT_OK;
}

bt_status_t bt_controller_start_le_scan(struct bt_controller *ctrl, uint64_t now_us)
{
    uint8_t params[7];
    struct bt_buf_writer w;
    bt_status_t st;

    if (ctrl->state != BT_CONTROLLER_STATE_READY)
        return BT_ERR_INVALID_ARGUMENT;

    bt_buf_writer_init(&w, params, sizeof(params));
    bt_buf_writer_write_u8(&w, 0x00);     /* passive scan */
    bt_buf_writer_write_le16(&w, 0x0010); /* scan interval */
    bt_buf_writer_write_le16(&w, 0x0010); /* scan window */
    bt_buf_writer_write_u8(&w, 0x00);     /* public own address */
    bt_buf_writer_write_u8(&w, 0x00);     /* no filter policy */

    st = bt_cmdq_submit(&ctrl->cmdq, BT_HCI_OPCODE_LE_SET_SCAN_PARAMETERS, params,
                         (uint8_t)bt_buf_writer_len(&w), 0, ignore_completion, ctrl);
    if (st != BT_OK)
        return st;

    bt_buf_writer_init(&w, params, sizeof(params));
    bt_buf_writer_write_u8(&w, 0x01); /* scan enable */
    bt_buf_writer_write_u8(&w, 0x01); /* filter duplicates */

    st = bt_cmdq_submit(&ctrl->cmdq, BT_HCI_OPCODE_LE_SET_SCAN_ENABLE, params,
                         (uint8_t)bt_buf_writer_len(&w), 0, ignore_completion, ctrl);
    if (st != BT_OK)
        return st;

    bt_cmdq_pump(&ctrl->cmdq, now_us);
    return BT_OK;
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
