/*
 * BTScan -- a Zune front end for Bluetooth discovery.
 *
 * The manager runs in its own process (ports/aros/task) and owns the only
 * correct clock for the command queue, so this program never talks to the
 * controller directly: it asks the manager to scan or to inquire, and reads
 * the device registry the manager fills in.
 *
 * Reading that registry across tasks is deliberate and bounded. Entries are
 * only ever appended or updated in place, `count` only grows, and every field
 * this program reads is at most 32 bits wide, so the worst a torn read can
 * produce is one stale row -- corrected on the next refresh half a second
 * later. Anything stronger would mean a lock on the manager's hot path to
 * make a list redraw prettier.
 */

#include "../task/manager_task.h"
#include "../transport-uart/uart_transport.h"

#include <bluetooth/controller.h>
#include <bluetooth/device_registry.h>

#include <devices/timer.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <libraries/mui.h>
#include <utility/hooks.h>
#include <workbench/workbench.h>

#include <clib/alib_protos.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/muimaster.h>

#include <stdio.h>
#include <string.h>

struct Library *MUIMasterBase;
struct Library *IconBase;

/* Inquiry length in units of 1.28 s, as the HCI command takes it. */
#define BTSCAN_INQUIRY_LENGTH 8u
/* How often the list is reconciled with the registry. Fast enough that a
 * device appearing feels immediate, slow enough to be free. */
#define BTSCAN_REFRESH_US 500000u

#define BTSCAN_BANNER ((CONST_STRPTR) "PROGDIR:BTScan-banner.png")
#define BTSCAN_ICON ((CONST_STRPTR) "PROGDIR:BTScan")

enum
{
    BTSCAN_MENU_ABOUT = 1,
    BTSCAN_MENU_ABOUT_MUI,
    BTSCAN_MENU_QUIT
};

/* One list row, formatted once at insertion. The display hook must not
 * allocate or format -- it is called during rendering. */
struct btscan_row
{
    char name[BT_DEVICE_NAME_LEN + 4];
    char addr[20];
    char kind[10];
    char hid[4];
    char rssi[8];
};

struct btscan
{
    struct bt_aros_uart_transport *uart;
    struct bt_aros_manager_task *task;
    BOOL manager_running;

    Object *app;
    Object *window;
    Object *list;
    Object *status;
    Object *connect;
    Object *disconnect;

    struct MsgPort *timer_port;
    struct timerequest *timer;
    BOOL timer_open;
    BOOL timer_pending;

    /* Cheap digest of what the list is showing, so an unchanged registry
     * does not cost a rebuild and does not throw away the selection. */
    ULONG digest;
};

static struct btscan g_btscan;

/* ------------------------------------------------------------------ model */

static uint32_t uart_signal_mask(const void *context)
{
    return bt_aros_uart_transport_signal_mask(context);
}

static bt_status_t uart_poll(void *context)
{
    return bt_aros_uart_transport_poll(context);
}

static const char *state_name(enum bt_controller_state state)
{
    switch (state)
    {
        case BT_CONTROLLER_STATE_UNINITIALIZED: return "not started";
        case BT_CONTROLLER_STATE_RESETTING: return "resetting";
        case BT_CONTROLLER_STATE_READING_VERSION: return "reading version";
        case BT_CONTROLLER_STATE_READING_FEATURES: return "reading features";
        case BT_CONTROLLER_STATE_READING_BUFFER_SIZE: return "sizing buffers";
        case BT_CONTROLLER_STATE_READY: return "ready";
        case BT_CONTROLLER_STATE_ERROR: return "error";
    }
    return "unknown";
}

static void format_addr(char *out, size_t size, const struct bt_addr *addr)
{
    snprintf(out, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             addr->b[5], addr->b[4], addr->b[3],
             addr->b[2], addr->b[1], addr->b[0]);
}

static const struct bt_device_registry *registry(const struct btscan *bs)
{
    return &bs->task->manager.controller.devices;
}

/* ------------------------------------------------------------------- hooks */

static IPTR display_func(struct Hook *hook, STRPTR *columns, STRPTR *entry)
{
    struct btscan_row *row = (struct btscan_row *)entry;

    (void)hook;
    if (row == NULL)
    {
        columns[0] = (STRPTR)"Device";
        columns[1] = (STRPTR)"Address";
        columns[2] = (STRPTR)"Radio";
        columns[3] = (STRPTR)"HID";
        columns[4] = (STRPTR)"Signal";
    }
    else
    {
        columns[0] = row->name;
        columns[1] = row->addr;
        columns[2] = row->kind;
        columns[3] = row->hid;
        columns[4] = row->rssi;
    }
    return TRUE;
}

static IPTR destruct_func(struct Hook *hook, APTR pool, APTR entry)
{
    (void)hook;
    (void)pool;
    FreeVec(entry);
    return 0;
}

static struct Hook display_hook;
static struct Hook destruct_hook;

/* --------------------------------------------------------------- the list */

static ULONG registry_digest(const struct bt_device_registry *reg)
{
    ULONG digest = (ULONG)bt_device_registry_count(reg);
    size_t i;

    for (i = 0; i < bt_device_registry_count(reg); i++)
    {
        const struct bt_discovered_device *d = bt_device_registry_get(reg, i);

        /* Everything the row shows, and nothing else: a sighting that changes
         * none of it does not need a redraw. */
        digest = digest * 31u + (ULONG)d->flags;
        digest = digest * 31u + (ULONG)(UBYTE)d->last_rssi;
        digest = digest * 31u + (ULONG)d->name_state;
        digest = digest * 31u + (ULONG)d->name[0];
    }
    return digest;
}

static void fill_row(struct btscan_row *row,
                     const struct bt_discovered_device *d)
{
    if (d->name[0] != '\0')
        snprintf(row->name, sizeof(row->name), "%.*s",
                 (int)BT_DEVICE_NAME_LEN, d->name);
    else
        snprintf(row->name, sizeof(row->name), "%s", "unknown");

    format_addr(row->addr, sizeof(row->addr), &d->addr);

    if ((d->flags & (BT_DEVICE_FLAG_CLASSIC | BT_DEVICE_FLAG_LE)) ==
        (BT_DEVICE_FLAG_CLASSIC | BT_DEVICE_FLAG_LE))
        snprintf(row->kind, sizeof(row->kind), "%s", "dual");
    else if ((d->flags & BT_DEVICE_FLAG_CLASSIC) != 0)
        snprintf(row->kind, sizeof(row->kind), "%s", "classic");
    else
        snprintf(row->kind, sizeof(row->kind), "%s", "LE");

    snprintf(row->hid, sizeof(row->hid), "%s",
             (d->flags & BT_DEVICE_FLAG_HID) != 0 ? "yes" : "");

    /* An RSSI of zero is what a Classic inquiry without extended results
     * reports, and it means "not measured" rather than "very strong". */
    if (d->last_rssi != 0)
        snprintf(row->rssi, sizeof(row->rssi), "%d dBm", (int)d->last_rssi);
    else
        snprintf(row->rssi, sizeof(row->rssi), "%s", "-");
}

static void refresh_list(struct btscan *bs)
{
    const struct bt_device_registry *reg = registry(bs);
    LONG active = MUIV_List_Active_Off;
    size_t i;

    get(bs->list, MUIA_List_Active, &active);

    set(bs->list, MUIA_List_Quiet, TRUE);
    DoMethod(bs->list, MUIM_List_Clear);

    for (i = 0; i < bt_device_registry_count(reg); i++)
    {
        const struct bt_discovered_device *d = bt_device_registry_get(reg, i);
        struct btscan_row *row;

        if (d == NULL)
            continue;
        row = AllocVec(sizeof(*row), MEMF_PUBLIC | MEMF_CLEAR);
        if (row == NULL)
            break;
        fill_row(row, d);
        DoMethod(bs->list, MUIM_List_InsertSingle, (IPTR)row,
                 MUIV_List_Insert_Bottom);
    }

    /* Entries are only ever appended, so an index still names the same
     * device after a refresh. */
    if (active != MUIV_List_Active_Off)
        set(bs->list, MUIA_List_Active, active);
    set(bs->list, MUIA_List_Quiet, FALSE);
}

static void refresh_status(struct btscan *bs)
{
    const struct bt_device_registry *reg = registry(bs);
    enum bt_controller_state state;
    char text[128];
    size_t hid = 0;
    size_t i;

    state = bt_aros_manager_task_controller_state(bs->task);
    for (i = 0; i < bt_device_registry_count(reg); i++)
    {
        const struct bt_discovered_device *d = bt_device_registry_get(reg, i);

        if (d != NULL && (d->flags & BT_DEVICE_FLAG_HID) != 0)
            hid++;
    }

    snprintf(text, sizeof(text),
             MUIX_C "Controller %s" MUIX_N "  -  %u device%s, %u input device%s",
             state_name(state),
             (unsigned)bt_device_registry_count(reg),
             bt_device_registry_count(reg) == 1 ? "" : "s",
             (unsigned)hid, hid == 1 ? "" : "s");
    set(bs->status, MUIA_Text_Contents, (IPTR)text);
}

static void refresh(struct btscan *bs)
{
    ULONG digest = registry_digest(registry(bs));

    if (digest != bs->digest)
    {
        bs->digest = digest;
        refresh_list(bs);
    }
    refresh_status(bs);
}

/* ------------------------------------------------------------- the buttons */

static IPTR scan_func(struct Hook *hook, Object *caller, void *data)
{
    (void)hook;
    (void)caller;
    (void)data;
    bt_aros_manager_task_request_le_scan(g_btscan.task);
    set(g_btscan.status, MUIA_Text_Contents,
        (IPTR)(MUIX_C "Scanning for advertising devices..."));
    return TRUE;
}

static IPTR inquiry_func(struct Hook *hook, Object *caller, void *data)
{
    (void)hook;
    (void)caller;
    (void)data;
    bt_aros_manager_task_request_inquiry(g_btscan.task, BTSCAN_INQUIRY_LENGTH);
    set(g_btscan.status, MUIA_Text_Contents,
        (IPTR)(MUIX_C "Inquiring for classic devices..."));
    return TRUE;
}

static struct Hook scan_hook;
static struct Hook inquiry_hook;

/* ------------------------------------------------------------------- timer */

static BOOL open_timer(struct btscan *bs)
{
    bs->timer_port = CreateMsgPort();
    if (bs->timer_port == NULL)
        return FALSE;
    bs->timer = (struct timerequest *)CreateIORequest(bs->timer_port,
                                                      sizeof(*bs->timer));
    if (bs->timer == NULL)
        return FALSE;
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ,
                   (struct IORequest *)bs->timer, 0) != 0)
        return FALSE;
    bs->timer_open = TRUE;
    return TRUE;
}

static void arm_timer(struct btscan *bs)
{
    bs->timer->tr_node.io_Command = TR_ADDREQUEST;
    bs->timer->tr_time.tv_secs = 0;
    bs->timer->tr_time.tv_micro = BTSCAN_REFRESH_US;
    SendIO((struct IORequest *)bs->timer);
    bs->timer_pending = TRUE;
}

static void close_timer(struct btscan *bs)
{
    if (bs->timer_pending)
    {
        AbortIO((struct IORequest *)bs->timer);
        WaitIO((struct IORequest *)bs->timer);
        bs->timer_pending = FALSE;
    }
    if (bs->timer_open)
    {
        CloseDevice((struct IORequest *)bs->timer);
        bs->timer_open = FALSE;
    }
    if (bs->timer != NULL)
    {
        DeleteIORequest((struct IORequest *)bs->timer);
        bs->timer = NULL;
    }
    if (bs->timer_port != NULL)
    {
        DeleteMsgPort(bs->timer_port);
        bs->timer_port = NULL;
    }
}

/* --------------------------------------------------------------------- UI */

static Object *build_menu(void)
{
    /* Menus are a Family, not a Group: the child tag is MUIA_Family_Child and
     * `Child` (which is MUIA_Group_Child) is silently the wrong one here. */
    return MenustripObject,
        MUIA_Family_Child, (IPTR)MenuObject,
            MUIA_Menu_Title, (IPTR)"Project",
            MUIA_Family_Child, (IPTR)MenuitemObject,
                MUIA_Menuitem_Title, (IPTR)"About BTScan...",
                MUIA_Menuitem_Shortcut, (IPTR)"?",
                MUIA_UserData, BTSCAN_MENU_ABOUT,
                End,
            MUIA_Family_Child, (IPTR)MenuitemObject,
                MUIA_Menuitem_Title, (IPTR)"About Zune...",
                MUIA_UserData, BTSCAN_MENU_ABOUT_MUI,
                End,
            MUIA_Family_Child, (IPTR)MenuitemObject,
                MUIA_Menuitem_Title, ~0, /* a separator bar */
                End,
            MUIA_Family_Child, (IPTR)MenuitemObject,
                MUIA_Menuitem_Title, (IPTR)"Quit",
                MUIA_Menuitem_Shortcut, (IPTR)"Q",
                MUIA_UserData, BTSCAN_MENU_QUIT,
                End,
            End,
        End;
}

static BOOL build_ui(struct btscan *bs, struct DiskObject *icon)
{
    Object *scan;
    Object *inquiry;

    display_hook.h_Entry = (HOOKFUNC)HookEntry;
    display_hook.h_SubEntry = (HOOKFUNC)display_func;
    destruct_hook.h_Entry = (HOOKFUNC)HookEntry;
    destruct_hook.h_SubEntry = (HOOKFUNC)destruct_func;
    scan_hook.h_Entry = (HOOKFUNC)HookEntry;
    scan_hook.h_SubEntry = (HOOKFUNC)scan_func;
    inquiry_hook.h_Entry = (HOOKFUNC)HookEntry;
    inquiry_hook.h_SubEntry = (HOOKFUNC)inquiry_func;

    bs->app = ApplicationObject,
        MUIA_Application_Title, (IPTR)"BTScan",
        MUIA_Application_Version, (IPTR)"$VER: BTScan 0.1 (17.08.2026)",
        MUIA_Application_Copyright, (IPTR)"Bellatrix / aros-bluzing",
        MUIA_Application_Author, (IPTR)"The Bellatrix project",
        MUIA_Application_Description, (IPTR)"Bluetooth device discovery",
        MUIA_Application_Base, (IPTR)"BTSCAN",
        MUIA_Application_DiskObject, (IPTR)icon,
        MUIA_Application_Menustrip, (IPTR)build_menu(),

        SubWindow, (IPTR)(bs->window = WindowObject,
            MUIA_Window_Title, (IPTR)"BTScan",
            MUIA_Window_ID, MAKE_ID('B', 'T', 'S', 'C'),
            MUIA_Window_Activate, TRUE,

            WindowContents, (IPTR)VGroup,

                /* Masthead: the art carries the name, so the window does not
                 * have to repeat it in a label. */
                Child, (IPTR)HGroup,
                    MUIA_Group_Spacing, 8,
                    Child, (IPTR)DtpicObject,
                        MUIA_Dtpic_Name, (IPTR)BTSCAN_BANNER,
                        End,
                    Child, (IPTR)VGroup,
                        Child, (IPTR)HVSpace,
                        Child, (IPTR)TextObject,
                            MUIA_Text_Contents, (IPTR)(MUIX_B
                                "Bluetooth device discovery"),
                            End,
                        Child, (IPTR)TextObject,
                            MUIA_Text_Contents, (IPTR)
                                "Scan finds devices that advertise; inquiry\n"
                                "finds devices that answer a page. The radio\n"
                                "does one at a time.",
                            End,
                        Child, (IPTR)HVSpace,
                        End,
                    End,

                Child, (IPTR)ListviewObject,
                    MUIA_Weight, 300,
                    MUIA_Listview_List, (IPTR)(bs->list = ListObject,
                        InputListFrame,
                        MUIA_List_DisplayHook, (IPTR)&display_hook,
                        MUIA_List_DestructHook, (IPTR)&destruct_hook,
                        MUIA_List_Format, (IPTR)
                            "BAR,BAR,BAR,P=\33c BAR,P=\33r",
                        MUIA_List_Title, TRUE,
                        End),
                    End,

                Child, (IPTR)(bs->status = TextObject,
                    TextFrame,
                    MUIA_Background, MUII_TextBack,
                    MUIA_Text_Contents, (IPTR)(MUIX_C "Starting..."),
                    End),

                Child, (IPTR)HGroup,
                    Child, (IPTR)HVSpace,
                    Child, (IPTR)(scan = SimpleButton("_Scan")),
                    Child, (IPTR)(inquiry = SimpleButton("_Inquiry")),
                    Child, (IPTR)(bs->connect = SimpleButton("_Connect")),
                    Child, (IPTR)(bs->disconnect = SimpleButton("_Disconnect")),
                    End,
                End,
            End),
        End;

    if (bs->app == NULL)
        return FALSE;

    set(scan, MUIA_ShortHelp, (IPTR)
        "Listen for LE advertisements. Stops any inquiry in progress.");
    set(inquiry, MUIA_ShortHelp, (IPTR)
        "Page for classic devices. Stops LE scanning: one radio, one mode.");

    /* Connection is not in the stack yet. A live button that silently does
     * nothing is worse than one that says why it cannot. */
    set(bs->connect, MUIA_Disabled, TRUE);
    set(bs->disconnect, MUIA_Disabled, TRUE);
    set(bs->connect, MUIA_ShortHelp, (IPTR)
        "Not yet: the stack discovers devices but does not connect to them.");
    set(bs->disconnect, MUIA_ShortHelp, (IPTR)
        "Not yet: the stack discovers devices but does not connect to them.");

    DoMethod(bs->window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             (IPTR)bs->app, 2, MUIM_Application_ReturnID,
             MUIV_Application_ReturnID_Quit);
    DoMethod(scan, MUIM_Notify, MUIA_Pressed, FALSE,
             (IPTR)bs->app, 3, MUIM_CallHook, (IPTR)&scan_hook, NULL);
    DoMethod(inquiry, MUIM_Notify, MUIA_Pressed, FALSE,
             (IPTR)bs->app, 3, MUIM_CallHook, (IPTR)&inquiry_hook, NULL);

    return TRUE;
}

static void handle_menu(struct btscan *bs, ULONG id)
{
    switch (id)
    {
        case BTSCAN_MENU_ABOUT:
            MUI_RequestA(bs->app, bs->window, 0, (CONST_STRPTR)"BTScan",
                         (CONST_STRPTR)"*_Ok",
                         (CONST_STRPTR)
                         "BTScan 0.1\n\n"
                         "Bluetooth discovery for AROS, on the aros-bluzing\n"
                         "stack. Part of the Bellatrix project.",
                         NULL);
            break;
        case BTSCAN_MENU_ABOUT_MUI:
            DoMethod(bs->app, MUIM_Application_AboutMUI, (IPTR)bs->window);
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------- main */

static void run(struct btscan *bs)
{
    ULONG sigs = 0;
    ULONG timer_sig = 1UL << bs->timer_port->mp_SigBit;
    BOOL running = TRUE;

    set(bs->window, MUIA_Window_Open, TRUE);
    refresh(bs);
    arm_timer(bs);

    while (running)
    {
        ULONG id = DoMethod(bs->app, MUIM_Application_NewInput, (IPTR)&sigs);

        if (id == MUIV_Application_ReturnID_Quit)
            break;
        if (id != 0)
            handle_menu(bs, id);

        if (sigs == 0)
            continue;

        sigs = Wait(sigs | SIGBREAKF_CTRL_C | timer_sig);

        if ((sigs & SIGBREAKF_CTRL_C) != 0)
            running = FALSE;

        if ((sigs & timer_sig) != 0)
        {
            while (GetMsg(bs->timer_port) != NULL)
                ;
            bs->timer_pending = FALSE;
            refresh(bs);
            arm_timer(bs);
        }
    }

    set(bs->window, MUIA_Window_Open, FALSE);
}

static void teardown(struct btscan *bs, struct DiskObject *icon)
{
    close_timer(bs);
    if (bs->app != NULL)
    {
        MUI_DisposeObject(bs->app);
        bs->app = NULL;
    }
    if (bs->manager_running)
    {
        bt_aros_manager_task_stop(bs->task);
        bs->manager_running = FALSE;
    }
    if (icon != NULL)
        FreeDiskObject(icon);
    FreeVec(bs->task);
    FreeVec(bs->uart);
    bs->task = NULL;
    bs->uart = NULL;
}

static void fail(struct btscan *bs, struct DiskObject *icon, const char *why)
{
    /* Before the UI exists there is nowhere on screen to say this, so it goes
     * where a Workbench-launched program's complaints can still be read. */
    PutStr((CONST_STRPTR)"BTScan: ");
    PutStr((CONST_STRPTR)why);
    PutStr((CONST_STRPTR)"\n");
    teardown(bs, icon);
}

int main(void)
{
    struct btscan *bs = &g_btscan;
    struct DiskObject *icon = NULL;
    bt_status_t status;

    MUIMasterBase = OpenLibrary((CONST_STRPTR)MUIMASTER_NAME,
                                MUIMASTER_VMIN);
    if (MUIMasterBase == NULL)
    {
        PutStr((CONST_STRPTR)"BTScan: muimaster.library is not available\n");
        return RETURN_FAIL;
    }
    IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 0);
    if (IconBase != NULL)
        icon = GetDiskObject(BTSCAN_ICON);

    bs->uart = AllocVec(sizeof(*bs->uart), MEMF_PUBLIC | MEMF_CLEAR);
    bs->task = AllocVec(sizeof(*bs->task), MEMF_PUBLIC | MEMF_CLEAR);
    if (bs->uart == NULL || bs->task == NULL)
    {
        fail(bs, icon, "out of memory");
        goto done;
    }

    bt_aros_uart_transport_init(bs->uart);
    bt_aros_manager_task_init(bs->task, &bs->uart->transport, bs->uart,
                              uart_signal_mask, uart_poll);
    status = bt_aros_manager_task_start(bs->task);
    if (status != BT_OK)
    {
        fail(bs, icon, "the Bluetooth manager would not start");
        goto done;
    }
    bs->manager_running = TRUE;

    if (!build_ui(bs, icon))
    {
        fail(bs, icon, "the interface could not be created");
        goto done;
    }
    if (!open_timer(bs))
    {
        fail(bs, icon, "timer.device is not available");
        goto done;
    }

    run(bs);
    teardown(bs, icon);

done:
    if (IconBase != NULL)
        CloseLibrary(IconBase);
    CloseLibrary(MUIMasterBase);
    return RETURN_OK;
}
