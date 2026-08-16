#include "../task/manager_task.h"
#include "../transport-uart/uart_transport.h"

#include <proto/dos.h>
#include <proto/exec.h>

#define STARTUP_TICKS 500u
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
    BPTR output = Open(SELFTEST_CONSOLE, MODE_NEWFILE);

    if (output == BNULL)
        output = Output();

    uart = AllocVec(sizeof(*uart), MEMF_PUBLIC | MEMF_CLEAR);
    task = AllocVec(sizeof(*task), MEMF_PUBLIC | MEMF_CLEAR);
    if (uart == NULL || task == NULL)
    {
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
    status = bt_aros_manager_task_start(task);
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

    for (ticks = 0; ticks < STARTUP_TICKS; ++ticks)
    {
        state = bt_aros_manager_task_controller_state(task);
        if (state == BT_CONTROLLER_STATE_READY ||
            state == BT_CONTROLLER_STATE_ERROR)
            break;
        Delay(1);
    }

    FPrintf(output, (CONST_STRPTR)
            "aros-bluzing-uart-selftest: controller %s after %u ticks\n",
            controller_state_name(state), ticks);
    if (state == BT_CONTROLLER_STATE_READY)
    {
        const struct bt_controller_info *info = &task->manager.controller.info;

        FPrintf(output, (CONST_STRPTR)
                "  HCI %u.%u, manufacturer 0x%04x, ACL MTU %u, "
                "ACL packets %u\n",
                info->version.hci_version, info->version.hci_revision,
                info->version.manufacturer_name,
                info->buffer_size.acl_data_packet_length,
                info->buffer_size.total_num_acl_data_packets);
    }
    bt_aros_manager_task_stop(task);
    FreeVec(task);
    FreeVec(uart);
    if (output != Output())
        Close(output);
    return state == BT_CONTROLLER_STATE_READY ? RETURN_OK : RETURN_FAIL;
}
