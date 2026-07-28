#include "bluetooth_intern.h"

#include <aros/libcall.h>

AROS_LH0(ULONG, BT_GetAPIVersion,
         struct BluetoothBase *, BluetoothBase, 5, Bluetooth)
{
    AROS_LIBFUNC_INIT

    return 1;

    AROS_LIBFUNC_EXIT
}
