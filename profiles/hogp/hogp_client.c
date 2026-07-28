#include <bluetooth/hogp_client.h>

#include <string.h>

static void on_services(struct bt_gatt_client_completion *completion, void *user_data);
static void on_characteristics(struct bt_gatt_client_completion *completion,
                               void *user_data);
static void on_descriptors(struct bt_gatt_client_completion *completion, void *user_data);
static void on_report_map(struct bt_gatt_client_completion *completion, void *user_data);
static void on_report_reference(struct bt_gatt_client_completion *completion,
                                void *user_data);
static void on_cccd_write(struct bt_gatt_client_completion *completion, void *user_data);

static void finish(struct bt_hogp_client *client, enum bt_hogp_client_result result)
{
    bt_hogp_client_complete_fn callback = client->on_complete;

    client->on_complete = NULL;
    if (callback != NULL)
        callback(result, client->user_data);
}

static bool gatt_ok(const struct bt_gatt_client_completion *completion)
{
    return completion->result == BT_GATT_CLIENT_OK;
}

static uint64_t event_time(struct bt_hogp_client *client)
{
    if (client->gatt->event_now_us != 0)
        client->now_us = client->gatt->event_now_us;
    return client->now_us;
}

static uint16_t characteristic_end(const struct bt_hogp_client *client, size_t index)
{
    if (index + 1 < client->characteristic_count)
        return (uint16_t)(client->characteristics[index + 1].declaration_handle - 1u);
    return client->service.end_handle;
}

static void build_report_list(struct bt_hogp_client *client)
{
    size_t i;
    size_t j;

    client->report_count = 0;
    for (i = 0; i < client->characteristic_count; ++i)
    {
        struct bt_hogp_report *report;
        uint16_t end;

        if (client->characteristics[i].uuid16 != BT_HOGP_UUID_REPORT ||
            client->report_count == BT_HOGP_MAX_REPORTS)
            continue;
        report = &client->reports[client->report_count++];
        memset(report, 0, sizeof(*report));
        report->value_handle = client->characteristics[i].value_handle;
        end = characteristic_end(client, i);
        for (j = 0; j < client->descriptor_count; ++j)
        {
            const struct bt_gatt_descriptor *descriptor = &client->descriptors[j];

            if (descriptor->handle <= report->value_handle || descriptor->handle > end)
                continue;
            if (descriptor->uuid16 == BT_HOGP_UUID_CCCD)
                report->cccd_handle = descriptor->handle;
            else if (descriptor->uuid16 == BT_HOGP_UUID_REPORT_REFERENCE)
                report->reference_handle = descriptor->handle;
        }
    }
}

static void read_next_reference(struct bt_hogp_client *client)
{
    while (client->pending_index < client->report_count &&
           client->reports[client->pending_index].reference_handle == 0)
        ++client->pending_index;
    if (client->pending_index == client->report_count)
    {
        client->pending_index = 0;
        on_cccd_write(NULL, client);
        return;
    }
    if (bt_gatt_client_read(client->gatt,
                            client->reports[client->pending_index].reference_handle,
                            on_report_reference, client, event_time(client)) != BT_OK)
        finish(client, BT_HOGP_CLIENT_ERROR_GATT);
}

static void on_services(struct bt_gatt_client_completion *completion, void *user_data)
{
    struct bt_hogp_client *client = user_data;
    size_t i;

    if (!gatt_ok(completion))
    {
        finish(client, BT_HOGP_CLIENT_ERROR_GATT);
        return;
    }
    for (i = 0; i < completion->count; ++i)
        if (completion->services[i].uuid16 == BT_HOGP_UUID_SERVICE)
        {
            client->service = completion->services[i];
            if (bt_gatt_client_discover_characteristics(
                    client->gatt, client->service.start_handle,
                    client->service.end_handle, on_characteristics, client,
                    event_time(client)) != BT_OK)
                finish(client, BT_HOGP_CLIENT_ERROR_GATT);
            return;
        }
    finish(client, BT_HOGP_CLIENT_ERROR_NOT_FOUND);
}

static void on_characteristics(struct bt_gatt_client_completion *completion,
                               void *user_data)
{
    struct bt_hogp_client *client = user_data;
    size_t i;

    if (!gatt_ok(completion) ||
        completion->count > BT_GATT_CLIENT_MAX_CHARACTERISTICS)
    {
        finish(client, BT_HOGP_CLIENT_ERROR_GATT);
        return;
    }
    memcpy(client->characteristics, completion->characteristics,
           completion->count * sizeof(client->characteristics[0]));
    client->characteristic_count = completion->count;
    client->report_map_handle = 0;
    for (i = 0; i < client->characteristic_count; ++i)
        if (client->characteristics[i].uuid16 == BT_HOGP_UUID_REPORT_MAP)
            client->report_map_handle = client->characteristics[i].value_handle;
    if (client->report_map_handle == 0)
    {
        finish(client, BT_HOGP_CLIENT_ERROR_NOT_FOUND);
        return;
    }
    if (bt_gatt_client_discover_descriptors(
            client->gatt, client->service.start_handle, client->service.end_handle,
            on_descriptors, client, event_time(client)) != BT_OK)
        finish(client, BT_HOGP_CLIENT_ERROR_GATT);
}

static void on_descriptors(struct bt_gatt_client_completion *completion, void *user_data)
{
    struct bt_hogp_client *client = user_data;

    if (!gatt_ok(completion) || completion->count > BT_GATT_CLIENT_MAX_DESCRIPTORS)
    {
        finish(client, BT_HOGP_CLIENT_ERROR_GATT);
        return;
    }
    memcpy(client->descriptors, completion->descriptors,
           completion->count * sizeof(client->descriptors[0]));
    client->descriptor_count = completion->count;
    build_report_list(client);
    if (client->report_count == 0)
    {
        finish(client, BT_HOGP_CLIENT_ERROR_NOT_FOUND);
        return;
    }
    if (bt_gatt_client_read(client->gatt, client->report_map_handle, on_report_map,
                            client, event_time(client)) != BT_OK)
        finish(client, BT_HOGP_CLIENT_ERROR_GATT);
}

static void on_report_map(struct bt_gatt_client_completion *completion, void *user_data)
{
    struct bt_hogp_client *client = user_data;

    if (!gatt_ok(completion) ||
        bt_hid_report_parse(completion->value, completion->value_len,
                            &client->report_descriptor) != BT_OK)
    {
        finish(client, BT_HOGP_CLIENT_ERROR_PROTOCOL);
        return;
    }
    client->pending_index = 0;
    read_next_reference(client);
}

static void on_report_reference(struct bt_gatt_client_completion *completion,
                                void *user_data)
{
    struct bt_hogp_client *client = user_data;
    struct bt_hogp_report *report = &client->reports[client->pending_index];

    if (!gatt_ok(completion) || completion->value_len != 2 ||
        completion->value[1] < BT_HOGP_REPORT_TYPE_INPUT ||
        completion->value[1] > BT_HOGP_REPORT_TYPE_FEATURE)
    {
        finish(client, BT_HOGP_CLIENT_ERROR_PROTOCOL);
        return;
    }
    report->report_id = completion->value[0];
    report->report_type = completion->value[1];
    ++client->pending_index;
    read_next_reference(client);
}

static void on_cccd_write(struct bt_gatt_client_completion *completion, void *user_data)
{
    struct bt_hogp_client *client = user_data;
    static const uint8_t notify_enabled[2] = {1, 0};

    if (completion != NULL && !gatt_ok(completion))
    {
        finish(client, BT_HOGP_CLIENT_ERROR_GATT);
        return;
    }
    while (client->pending_index < client->report_count)
    {
        struct bt_hogp_report *report = &client->reports[client->pending_index++];

        if (report->report_type != BT_HOGP_REPORT_TYPE_INPUT)
            continue;
        if (report->cccd_handle == 0)
        {
            finish(client, BT_HOGP_CLIENT_ERROR_PROTOCOL);
            return;
        }
        if (bt_gatt_client_write(client->gatt, report->cccd_handle, notify_enabled,
                                 sizeof(notify_enabled), on_cccd_write, client,
                                 event_time(client)) != BT_OK)
            finish(client, BT_HOGP_CLIENT_ERROR_GATT);
        return;
    }
    finish(client, BT_HOGP_CLIENT_OK);
}

static bool forward_value(const struct bt_hid_value *value, void *user_data)
{
    struct bt_hogp_client *client = user_data;

    return client->on_input == NULL || client->on_input(value, client->user_data);
}

static void on_notification(uint16_t handle, const uint8_t *value, size_t value_len,
                            bool is_indication, void *user_data)
{
    struct bt_hogp_client *client = user_data;
    uint8_t report[BT_GATT_CLIENT_MAX_VALUE_LEN + 1];
    size_t i;

    (void)is_indication;
    for (i = 0; i < client->report_count; ++i)
    {
        const struct bt_hogp_report *endpoint = &client->reports[i];

        if (endpoint->value_handle != handle ||
            endpoint->report_type != BT_HOGP_REPORT_TYPE_INPUT)
            continue;
        if (client->report_descriptor.uses_report_ids)
        {
            if (value_len > BT_GATT_CLIENT_MAX_VALUE_LEN)
                return;
            report[0] = endpoint->report_id;
            memcpy(report + 1, value, value_len);
            (void)bt_hid_report_decode_input(&client->report_descriptor, report,
                                              value_len + 1, forward_value, client);
        }
        else
            (void)bt_hid_report_decode_input(&client->report_descriptor, value,
                                              value_len, forward_value, client);
        return;
    }
}

void bt_hogp_client_init(struct bt_hogp_client *client,
                          struct bt_gatt_client *gatt)
{
    memset(client, 0, sizeof(*client));
    client->gatt = gatt;
}

bt_status_t bt_hogp_client_discover(struct bt_hogp_client *client,
                                     bt_hogp_client_complete_fn on_complete,
                                     bt_hogp_input_value_fn on_input,
                                     void *user_data, uint64_t now_us)
{
    if (client == NULL || client->gatt == NULL || on_complete == NULL ||
        !client->gatt->channel_ready || client->gatt->busy ||
        client->on_complete != NULL)
        return BT_ERR_INVALID_ARGUMENT;
    client->on_complete = on_complete;
    client->on_input = on_input;
    client->user_data = user_data;
    client->now_us = now_us;
    bt_gatt_client_set_notify_handler(client->gatt, on_notification, client);
    if (bt_gatt_client_discover_services(client->gatt, on_services, client,
                                          now_us) != BT_OK)
    {
        client->on_complete = NULL;
        return BT_ERR_INVALID_ARGUMENT;
    }
    return BT_OK;
}
