#ifndef AROS_BLUZING_MANAGER_TASK_H
#define AROS_BLUZING_MANAGER_TASK_H

#include <bluetooth/manager.h>

#include <exec/tasks.h>

typedef uint32_t (*bt_aros_signal_mask_fn)(const void *context);
typedef bt_status_t (*bt_aros_poll_fn)(void *context);

struct bt_aros_manager_task
{
    struct bt_manager manager;
    struct bt_hci_transport *transport;
    void *transport_context;
    bt_aros_signal_mask_fn signal_mask;
    bt_aros_poll_fn poll;
    struct Task *task;
    struct Task *creator;
    bt_status_t startup_status;
};

void bt_aros_manager_task_init(
    struct bt_aros_manager_task *task,
    struct bt_hci_transport *transport,
    void *transport_context,
    bt_aros_signal_mask_fn signal_mask,
    bt_aros_poll_fn poll);

/* Starts a dedicated Exec process and waits for its initialization result. */
bt_status_t bt_aros_manager_task_start(struct bt_aros_manager_task *task);

/* Signals the process to stop and waits until all I/O has been torn down. */
void bt_aros_manager_task_stop(struct bt_aros_manager_task *task);

#endif /* AROS_BLUZING_MANAGER_TASK_H */
