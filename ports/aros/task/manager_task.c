#include "manager_task.h"

#include <devices/timer.h>
#include <dos/dostags.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/timer.h>

#include <string.h>

/*
 * One millisecond, not ten.
 *
 * This is the only thing draining the UART: there is no receive interrupt yet
 * (BTUART_CAP_RX_INTERRUPT is defined in the resource ABI and never set), so
 * the tick period is the window in which the sixteen-byte PL011 FIFO has to
 * survive. Ten milliseconds is 115 bytes at 115200 baud and loses data during
 * any burst; one is eleven, which fits. The real fix is the interrupt, and
 * this is what makes the scan usable until it exists.
 */
#define BT_AROS_MANAGER_TICK_US 1000u

/*
 * The clock, read without an IORequest.
 *
 * This used to set TR_GETSYSTIME on the same timerequest the periodic tick is
 * armed on and call DoIO(). An IORequest carries one request at a time, so
 * whenever the loop woke on the transport signal -- with the TR_ADDREQUEST
 * still in flight -- the DoIO overwrote its io_Command and tr_time and the
 * tick was lost. The command queue then stopped being pumped, the HCI_Reset
 * timed out, and the controller fell back to UNINITIALIZED even though the
 * chip had answered. Intermittent by construction: whichever signal arrived
 * first decided whether the request was clobbered.
 *
 * GetSysTime() is the same clock through timer.device's library vector, so it
 * needs no request of its own and cannot collide with anything.
 */
/* Not static: proto/timer.h already declares it extern, and its inlines
 * call through this exact name. */
struct Device *TimerBase;

static uint64_t timer_now_us(struct timerequest *request)
{
    struct timeval tv;

    (void)request;
    GetSysTime(&tv);
    return (uint64_t)tv.tv_secs * 1000000ull + tv.tv_micro;
}

static void arm_tick(struct timerequest *request)
{
    request->tr_node.io_Command = TR_ADDREQUEST;
    request->tr_node.io_Flags = 0;
    request->tr_time.tv_secs = 0;
    request->tr_time.tv_micro = BT_AROS_MANAGER_TICK_US;
    SendIO(&request->tr_node);
}

static void close_timer(struct timerequest *request)
{
    struct MsgPort *port;

    if (request == NULL)
        return;
    port = request->tr_node.io_Message.mn_ReplyPort;
    if (!CheckIO(&request->tr_node))
        AbortIO(&request->tr_node);
    WaitIO(&request->tr_node);
    CloseDevice(&request->tr_node);
    DeleteIORequest(&request->tr_node);
    DeleteMsgPort(port);
}

static struct timerequest *open_timer(void)
{
    struct MsgPort *port = CreateMsgPort();
    struct timerequest *request;

    if (port == NULL)
        return NULL;
    request = (struct timerequest *)CreateIORequest(
        port, sizeof(struct timerequest));
    if (request == NULL)
    {
        DeleteMsgPort(port);
        return NULL;
    }
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ,
                   &request->tr_node, 0) != 0)
    {
        DeleteIORequest(&request->tr_node);
        DeleteMsgPort(port);
        return NULL;
    }
    TimerBase = request->tr_node.io_Device;
    return request;
}

static void manager_process(void)
{
    struct Task *self = FindTask(NULL);
    struct bt_aros_manager_task *task = self->tc_UserData;
    struct timerequest *timer = open_timer();
    uint32_t timer_mask = 0;

    task->task = self;
    if (timer == NULL)
        task->startup_status = BT_ERR_NO_RESOURCES;
    else
    {
        timer_mask =
            (uint32_t)1u << timer->tr_node.io_Message.mn_ReplyPort->mp_SigBit;
        bt_manager_init(&task->manager, task->transport);
        task->startup_status =
            bt_manager_start(&task->manager, timer_now_us(timer));
    }
    if (task->startup_status != BT_OK)
    {
        close_timer(timer);
        task->task = NULL;
        Signal(task->creator, SIGF_SINGLE);
        return;
    }

    Signal(task->creator, SIGF_SINGLE);
    {
        bool running = true;

        arm_tick(timer);
        while (running)
        {
            uint32_t transport_mask =
                task->signal_mask != NULL
                    ? task->signal_mask(task->transport_context)
                    : 0;
            uint32_t signals =
                Wait(SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F | timer_mask | transport_mask);

            if ((signals & transport_mask) != 0 && task->poll != NULL &&
                task->poll(task->transport_context) != BT_OK)
                task->manager.state = BT_MANAGER_STATE_ERROR;
            if (task->le_scan_requested)
            {
                task->le_scan_requested = false;
                task->le_scan_status = bt_controller_start_le_scan(
                    &task->manager.controller, timer_now_us(timer));
            }
            if (task->inquiry_requested)
            {
                struct bt_controller *ctrl = &task->manager.controller;
                uint64_t now = timer_now_us(timer);

                task->inquiry_requested = false;
                /* Stop scanning first: one radio, so the two cannot overlap. */
                bt_controller_stop_le_scan(ctrl, now);
                task->inquiry_status = bt_controller_start_classic_inquiry(
                    ctrl, task->inquiry_length, now);
            }
            if ((signals & timer_mask) != 0)
            {
                WaitIO(&timer->tr_node);
                if (transport_mask == 0 && task->poll != NULL &&
                    task->poll(task->transport_context) != BT_OK)
                    task->manager.state = BT_MANAGER_STATE_ERROR;
                bt_manager_tick(&task->manager, timer_now_us(timer));
                arm_tick(timer);
            }
            if ((signals & SIGBREAKF_CTRL_C) != 0 ||
                task->manager.state == BT_MANAGER_STATE_ERROR)
                running = false;
        }
        bt_manager_stop(&task->manager);
    }

    close_timer(timer);
    task->task = NULL;
    Signal(task->creator, SIGF_SINGLE);
}

void bt_aros_manager_task_init(
    struct bt_aros_manager_task *task,
    struct bt_hci_transport *transport,
    void *transport_context,
    bt_aros_signal_mask_fn signal_mask,
    bt_aros_poll_fn poll)
{
    if (task == NULL)
        return;
    memset(task, 0, sizeof(*task));
    task->transport = transport;
    task->transport_context = transport_context;
    task->signal_mask = signal_mask;
    task->poll = poll;
    task->startup_status = BT_ERR_INVALID_ARGUMENT;
}

void bt_aros_manager_task_request_le_scan(struct bt_aros_manager_task *task)
{
    if (task == NULL || task->task == NULL)
        return;
    Forbid();
    task->le_scan_status = BT_ERR_INVALID_ARGUMENT;  /* see request_inquiry */
    task->le_scan_requested = true;
    Permit();
    Signal(task->task, SIGBREAKF_CTRL_F);
}

void bt_aros_manager_task_request_inquiry(struct bt_aros_manager_task *task,
                                          uint8_t length)
{
    if (task == NULL || task->task == NULL)
        return;
    Forbid();
    /* Not BT_OK: the caller reads this back to learn whether the inquiry
     * started, and pre-setting success made the answer meaningless -- it read
     * as started whether or not the manager had reached the request. */
    task->inquiry_status = BT_ERR_INVALID_ARGUMENT;
    task->inquiry_length = length;
    task->inquiry_requested = true;
    Permit();
    Signal(task->task, SIGBREAKF_CTRL_F);
}

bt_status_t bt_aros_manager_task_start(struct bt_aros_manager_task *task)
{
    struct Process *process;

    if (task == NULL || task->transport == NULL || task->task != NULL)
        return BT_ERR_INVALID_ARGUMENT;
    task->creator = FindTask(NULL);
    SetSignal(0, SIGF_SINGLE);
    /*
     * NP_UserData rather than assigning pr_Task.tc_UserData afterwards.
     *
     * manager_process() reads tc_UserData as its first act and dereferences it
     * immediately, so it must be set before the process can run. Assigning it
     * after CreateNewProcTags() returns is a race that Forbid() does not close:
     * CreateNewProc() allocates several times on the way (rom/dos/createnewproc.c
     * :195-254, only one of them MEMF_NO_EXPUNGE) and an expunge inside AllocMem
     * breaks the Forbid. Losing that race is silent -- the new process faults on
     * a NULL task before reaching any Signal(), and the creator waits on
     * SIGF_SINGLE forever.
     */
    process = CreateNewProcTags(
        NP_Entry, (IPTR)manager_process,
        NP_Name, (IPTR)"Bluetooth Manager",
        NP_Priority, 1,
        NP_UserData, (IPTR)task,
        TAG_DONE);
    if (process == NULL)
        return BT_ERR_NO_RESOURCES;
    Wait(SIGF_SINGLE);
    return task->startup_status;
}

void bt_aros_manager_task_stop(struct bt_aros_manager_task *task)
{
    if (task == NULL || task->task == NULL)
        return;
    SetSignal(0, SIGF_SINGLE);
    Signal(task->task, SIGBREAKF_CTRL_C);
    Wait(SIGF_SINGLE);
}

enum bt_controller_state bt_aros_manager_task_controller_state(
    const struct bt_aros_manager_task *task)
{
    enum bt_controller_state state;

    if (task == NULL)
        return BT_CONTROLLER_STATE_ERROR;
    Forbid();
    state = task->manager.controller.state;
    Permit();
    return state;
}
