#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <bluetooth/controller.h>
#include <bluetooth/status.h>
#include <bluetooth/transport.h>

enum bt_manager_state
{
    BT_MANAGER_STATE_STOPPED,
    BT_MANAGER_STATE_STARTING,
    BT_MANAGER_STATE_RUNNING,
    BT_MANAGER_STATE_ERROR
};

/* Transport-neutral single-owner orchestration.  An OS port owns the task
 * and wait primitive; this object owns controller state and packet dispatch. */
struct bt_manager
{
    struct bt_hci_transport *transport;
    struct bt_controller controller;
    enum bt_manager_state state;
};

void bt_manager_init(struct bt_manager *manager,
                     struct bt_hci_transport *transport);
bt_status_t bt_manager_start(struct bt_manager *manager, uint64_t now_us);
void bt_manager_tick(struct bt_manager *manager, uint64_t now_us);
void bt_manager_stop(struct bt_manager *manager);

#endif /* BLUETOOTH_MANAGER_H */
