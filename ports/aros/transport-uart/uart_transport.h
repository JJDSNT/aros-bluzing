#ifndef AROS_BLUZING_UART_TRANSPORT_H
#define AROS_BLUZING_UART_TRANSPORT_H

#include <bluetooth/h4.h>
#include <bluetooth/status.h>
#include <bluetooth/transport.h>

#include <exec/types.h>

#define BT_AROS_UART_TX_MAX (BT_H4_MAX_PACKET_SIZE + 1u)
#define BT_AROS_UART_RX_CHUNK 256u

/* How many inbound chunks to name on the debug console. Enough to carry the
 * Command Complete for the two LE scan commands and their status bytes, which
 * the core deliberately ignores, and the first advertising reports if any
 * arrive. */
#define BT_AROS_RX_TRACE_LIMIT 12u

struct bt_aros_uart_transport
{
    struct bt_hci_transport transport;
    APTR resource;
    struct bt_h4_rx h4_rx;
    bt_hci_transport_recv_fn receive;
    void *receive_data;
    uint8_t tx[BT_AROS_UART_TX_MAX];
    size_t tx_length;
    size_t tx_offset;
    bool opened;
    bool receiving;
    unsigned rx_traced; /* inbound chunks logged so far, see uart_transport.c */
};

void bt_aros_uart_transport_init(struct bt_aros_uart_transport *uart);
uint32_t bt_aros_uart_transport_signal_mask(
    const struct bt_aros_uart_transport *uart);
bt_status_t bt_aros_uart_transport_poll(struct bt_aros_uart_transport *uart);

#endif /* AROS_BLUZING_UART_TRANSPORT_H */
