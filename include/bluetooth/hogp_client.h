#ifndef BLUETOOTH_HOGP_CLIENT_H
#define BLUETOOTH_HOGP_CLIENT_H

#include <bluetooth/gatt_client.h>
#include <bluetooth/hid_report.h>

#define BT_HOGP_UUID_SERVICE 0x1812u
#define BT_HOGP_UUID_REPORT_MAP 0x2A4Bu
#define BT_HOGP_UUID_REPORT 0x2A4Du
#define BT_HOGP_UUID_PROTOCOL_MODE 0x2A4Eu
#define BT_HOGP_UUID_CCCD 0x2902u
#define BT_HOGP_UUID_REPORT_REFERENCE 0x2908u

#define BT_HOGP_REPORT_TYPE_INPUT 1u
#define BT_HOGP_REPORT_TYPE_OUTPUT 2u
#define BT_HOGP_REPORT_TYPE_FEATURE 3u

#ifndef BT_HOGP_MAX_REPORTS
#define BT_HOGP_MAX_REPORTS 8
#endif

enum bt_hogp_client_result
{
    BT_HOGP_CLIENT_OK,
    BT_HOGP_CLIENT_ERROR_GATT,
    BT_HOGP_CLIENT_ERROR_NOT_FOUND,
    BT_HOGP_CLIENT_ERROR_PROTOCOL,
    BT_HOGP_CLIENT_ERROR_TOO_LARGE
};

struct bt_hogp_report
{
    uint16_t value_handle;
    uint16_t cccd_handle;
    uint16_t reference_handle;
    uint8_t report_id;
    uint8_t report_type;
};

typedef void (*bt_hogp_client_complete_fn)(enum bt_hogp_client_result result,
                                           void *user_data);
typedef bool (*bt_hogp_input_value_fn)(const struct bt_hid_value *value,
                                       void *user_data);

struct bt_hogp_client
{
    struct bt_gatt_client *gatt;
    struct bt_gatt_service service;
    struct bt_gatt_characteristic characteristics[BT_GATT_CLIENT_MAX_CHARACTERISTICS];
    size_t characteristic_count;
    struct bt_gatt_descriptor descriptors[BT_GATT_CLIENT_MAX_DESCRIPTORS];
    size_t descriptor_count;
    struct bt_hogp_report reports[BT_HOGP_MAX_REPORTS];
    size_t report_count;
    size_t pending_index;
    uint16_t report_map_handle;
    struct bt_hid_report_descriptor report_descriptor;
    bt_hogp_client_complete_fn on_complete;
    bt_hogp_input_value_fn on_input;
    void *user_data;
    uint64_t now_us;
};

void bt_hogp_client_init(struct bt_hogp_client *client,
                          struct bt_gatt_client *gatt);

/*
 * Discovers and configures one HID service in Report Protocol. The GATT
 * client must already be connected. Completion occurs after all input Report
 * characteristics have their CCCDs enabled.
 */
bt_status_t bt_hogp_client_discover(struct bt_hogp_client *client,
                                     bt_hogp_client_complete_fn on_complete,
                                     bt_hogp_input_value_fn on_input,
                                     void *user_data, uint64_t now_us);

#endif /* BLUETOOTH_HOGP_CLIENT_H */
