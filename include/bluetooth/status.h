#ifndef BLUETOOTH_STATUS_H
#define BLUETOOTH_STATUS_H

typedef enum bt_status
{
    BT_OK = 0,
    BT_ERR_INVALID_ARGUMENT,
    BT_ERR_BUFFER_OVERFLOW,   /* writer has no room left for the requested data */
    BT_ERR_BUFFER_UNDERFLOW   /* reader has fewer bytes left than requested */
} bt_status_t;

#endif /* BLUETOOTH_STATUS_H */
