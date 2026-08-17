#ifndef AROS_BLUZING_MANAGER_TASK_H
#define AROS_BLUZING_MANAGER_TASK_H

#include <bluetooth/manager.h>

#include <stdbool.h>

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
    volatile bool le_scan_requested; /* set by another task, cleared by ours */
    bt_status_t le_scan_status;      /* result of the last requested scan */
    volatile bool inquiry_requested;
    uint8_t inquiry_length;          /* units of 1.28 s, as HCI takes it */
    bt_status_t inquiry_status;
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

/* Takes a scheduler-safe snapshot for diagnostics and hardware self-tests. */
/* Ask the manager process to start a passive LE scan on its next tick.
 *
 * The scan cannot be started from another task: bt_controller_start_le_scan()
 * pumps the command queue with a timestamp, and the only correct clock is the
 * one the manager process reads. A caller that passes its own -- or worse, a
 * zero -- gives every queued command a deadline in the distant past, and the
 * next real tick times them out before the controller has answered. */
void bt_aros_manager_task_request_le_scan(struct bt_aros_manager_task *task);

/* Stop LE scanning and start a Classic inquiry, on the manager's own clock.
 *
 * Not both at once: the CYW43438 has one radio for BR/EDR and LE, so discovery
 * that wants to see a bonded Classic keyboard -- which never advertises -- has
 * to alternate rather than run the two together. `seconds` is the inquiry
 * length in units of 1.28 s, as the HCI command takes it. */
void bt_aros_manager_task_request_inquiry(struct bt_aros_manager_task *task,
                                          uint8_t length);

enum bt_controller_state bt_aros_manager_task_controller_state(
    const struct bt_aros_manager_task *task);

#endif /* AROS_BLUZING_MANAGER_TASK_H */
