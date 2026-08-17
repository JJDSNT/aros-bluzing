#include "../task/manager_task.h"
#include "../transport-uart/uart_transport.h"

#include <bluetooth/device_registry.h>

#include <aros/debug.h>
#include <proto/dos.h>
#include <proto/exec.h>

#define STARTUP_TICKS 500u
/* Ticks are Delay() units, 1/50 s, so this is roughly twelve seconds of
 * passive LE scanning -- long enough for a phone or a watch nearby to
 * advertise several times, short enough not to hold up the boot. */
#define SCAN_TICKS 600u
#define SELFTEST_CONSOLE \
    ((CONST_STRPTR)"CON:20/20/600/180/Bluetooth UART Self-Test/CLOSE/WAIT/AUTO")

static uint32_t uart_signal_mask(const void *context)
{
    return bt_aros_uart_transport_signal_mask(context);
}

static bt_status_t uart_poll(void *context)
{
    return bt_aros_uart_transport_poll(context);
}

static const char *controller_state_name(enum bt_controller_state state)
{
    switch (state)
    {
        case BT_CONTROLLER_STATE_UNINITIALIZED: return "uninitialized";
        case BT_CONTROLLER_STATE_RESETTING: return "resetting";
        case BT_CONTROLLER_STATE_READING_VERSION: return "reading-version";
        case BT_CONTROLLER_STATE_READING_FEATURES: return "reading-features";
        case BT_CONTROLLER_STATE_READING_BUFFER_SIZE: return "reading-buffer-size";
        case BT_CONTROLLER_STATE_READY: return "ready";
        case BT_CONTROLLER_STATE_ERROR: return "error";
    }
    return "unknown";
}

int main(void)
{
    struct bt_aros_uart_transport *uart;
    struct bt_aros_manager_task *task;
    enum bt_controller_state state = BT_CONTROLLER_STATE_UNINITIALIZED;
    bt_status_t status;
    unsigned int ticks;
    BPTR output;

    bug("[aros-bluzing:selftest] entered, uart=%u manager=%u bytes\n",
        (unsigned int)sizeof(*uart), (unsigned int)sizeof(*task));
    output = Open(SELFTEST_CONSOLE, MODE_NEWFILE);
    bug("[aros-bluzing:selftest] console=%p\n", (void *)output);

    if (output == BNULL)
        output = Output();

    uart = AllocVec(sizeof(*uart), MEMF_PUBLIC | MEMF_CLEAR);
    bug("[aros-bluzing:selftest] uart allocation=%p\n", (void *)uart);
    task = AllocVec(sizeof(*task), MEMF_PUBLIC | MEMF_CLEAR);
    bug("[aros-bluzing:selftest] manager allocation=%p\n", (void *)task);
    if (uart == NULL || task == NULL)
    {
        bug("[aros-bluzing:selftest] allocation failed\n");
        FPrintf(output, (CONST_STRPTR)
                "aros-bluzing-uart-selftest: allocation failed\n");
        FreeVec(task);
        FreeVec(uart);
        if (output != Output())
            Close(output);
        return RETURN_FAIL;
    }

    bt_aros_uart_transport_init(uart);
    bt_aros_manager_task_init(task, &uart->transport, uart,
                              uart_signal_mask, uart_poll);
    bug("[aros-bluzing:selftest] starting manager\n");
    status = bt_aros_manager_task_start(task);
    bug("[aros-bluzing:selftest] manager start returned %d\n", (int)status);
    if (status != BT_OK)
    {
        FPrintf(output, (CONST_STRPTR)
                "aros-bluzing-uart-selftest: start failed (%d)\n",
                status);
        FreeVec(task);
        FreeVec(uart);
        if (output != Output())
            Close(output);
        return RETURN_FAIL;
    }

    {
        /*
         * Report every state change on the debug console as well as the
         * summary below.
         *
         * The summary alone goes to SELFTEST_CONSOLE, a CON: window, so a
         * headless serial run -- which is how this is actually exercised on a
         * Pi -- saw the transport open and then nothing at all, and could not
         * tell "reached READY" from "still resetting" from "the controller
         * never answered". The bring-up sequence is RESET, read version, read
         * features, read buffer size, so naming each transition also says
         * which HCI command went unanswered.
         */
        enum bt_controller_state seen = BT_CONTROLLER_STATE_UNINITIALIZED;

        bug("[aros-bluzing:selftest] controller %s\n",
            controller_state_name(seen));
        for (ticks = 0; ticks < STARTUP_TICKS; ++ticks)
        {
            state = bt_aros_manager_task_controller_state(task);
            if (state != seen)
            {
                seen = state;
                bug("[aros-bluzing:selftest] controller %s (tick %u)\n",
                    controller_state_name(state), ticks);
            }
            if (state == BT_CONTROLLER_STATE_READY ||
                state == BT_CONTROLLER_STATE_ERROR)
                break;
            Delay(1);
        }
    }

    bug("[aros-bluzing:selftest] controller %s after %u ticks\n",
        controller_state_name(state), ticks);
    FPrintf(output, (CONST_STRPTR)
            "aros-bluzing-uart-selftest: controller %s after %u ticks\n",
            controller_state_name(state), ticks);
    if (state == BT_CONTROLLER_STATE_READY)
    {
        const struct bt_controller_info *info = &task->manager.controller.info;

        bug("[aros-bluzing:selftest] HCI %u.%u mfr 0x%04x "
            "ACL MTU %u packets %u\n",
            info->version.hci_version, info->version.hci_revision,
            info->version.manufacturer_name,
            info->buffer_size.acl_data_packet_length,
            info->buffer_size.total_num_acl_data_packets);
        FPrintf(output, (CONST_STRPTR)
                "  HCI %u.%u, manufacturer 0x%04x, ACL MTU %u, "
                "ACL packets %u\n",
                info->version.hci_version, info->version.hci_revision,
                info->version.manufacturer_name,
                info->buffer_size.acl_data_packet_length,
                info->buffer_size.total_num_acl_data_packets);
    }
    /*
     * Having a controller is not the same as having Bluetooth. Scan, so the
     * self-test answers the question that matters -- does this stack see the
     * world -- rather than only reporting that the chip identified itself.
     *
     * Passive LE scan: no transmission, so nothing here depends on the
     * controller having been given a public address or a firmware patch, and
     * the result is checkable by walking into the room with a phone.
     */
    if (state == BT_CONTROLLER_STATE_READY)
    {
        struct bt_controller *ctrl = &task->manager.controller;
        size_t seen = 0;
        bt_status_t scan;

        /*
         * Ask the manager process to start it, rather than calling
         * bt_controller_start_le_scan() from here.
         *
         * That function pumps the command queue with a timestamp, and the only
         * correct clock is the one the manager process reads. Calling it here
         * meant passing a timestamp this task does not have -- a zero -- which
         * dated both queued commands to the epoch, so the next real tick timed
         * them out before the controller could answer. The scan was cancelled
         * a moment after being submitted, and the only visible symptom was a
         * successful start followed by no advertising reports at all.
         */
        bt_aros_manager_task_request_le_scan(task);
        Delay(2);
        Forbid();
        scan = task->le_scan_status;
        Permit();
        bug("[aros-bluzing:selftest] le scan start = %d\n", (int)scan);

        if (scan == BT_OK)
        {
            for (ticks = 0; ticks < SCAN_TICKS; ++ticks)
            {
                size_t now;

                Forbid();
                now = bt_device_registry_count(&ctrl->devices);
                Permit();
                while (seen < now)
                {
                    const struct bt_discovered_device *d;

                    Forbid();
                    d = bt_device_registry_get(&ctrl->devices, seen);
                    Permit();
                    if (d != NULL)
                        bug("[aros-bluzing:selftest] device "
                            "%02x:%02x:%02x:%02x:%02x:%02x rssi %d flags 0x%x\n",
                            d->addr.b[5], d->addr.b[4], d->addr.b[3],
                            d->addr.b[2], d->addr.b[1], d->addr.b[0],
                            (int)d->last_rssi, d->flags);
                    seen++;
                }
                Delay(1);
            }
        }
        bug("[aros-bluzing:selftest] le scan saw %u device(s)\n",
            (unsigned)seen);
        FPrintf(output, (CONST_STRPTR)
                "aros-bluzing-uart-selftest: LE scan saw %u device(s)\n",
                (unsigned)seen);
    }

    bt_aros_manager_task_stop(task);
    FreeVec(task);
    FreeVec(uart);
    if (output != Output())
        Close(output);
    return state == BT_CONTROLLER_STATE_READY ? RETURN_OK : RETURN_FAIL;
}
