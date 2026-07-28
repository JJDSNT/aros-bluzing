#include "bluetooth_intern.h"

#include <aros/symbolsets.h>

static int bluetooth_init(struct BluetoothBase *base)
{
    (void)base;
    return 1;
}

static int bluetooth_expunge(struct BluetoothBase *base)
{
    (void)base;
    return 1;
}

ADD2INITLIB(bluetooth_init, 0);
ADD2EXPUNGELIB(bluetooth_expunge, 0);
