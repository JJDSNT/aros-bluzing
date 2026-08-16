#include "uart_transport.h"

#include <aros/libcall.h>
#include <proto/exec.h>

#include <string.h>

/* Minimal client-side view of btuart.resource's versioned ABI. Keeping this
 * boundary local lets the portable contrib build on AROS targets which do not
 * provide the Bellatrix resource or its generated SDK headers. */
#define BTUART_RESOURCE_NAME "btuart.resource"
#define BTUART_API_VERSION 1u
#define BTUART_CONFIG_RTS_CTS (1UL << 0)
#define BTUART_OK 0

#define btuart_get_version(base) \
    AROS_LC0(unsigned int, BTUARTGetAPIVersion, APTR, (base), 1, Btuart)
#define btuart_claim(base, owner) \
    AROS_LC1(long, BTUARTClaim, AROS_LCA(void *, (owner), A0), \
             APTR, (base), 3, Btuart)
#define btuart_release(base, owner) \
    AROS_LC1NR(void, BTUARTRelease, AROS_LCA(void *, (owner), A0), \
               APTR, (base), 4, Btuart)
#define btuart_configure(base, owner, baud, flags) \
    AROS_LC3(long, BTUARTConfigure, \
             AROS_LCA(void *, (owner), A0), \
             AROS_LCA(unsigned long, (baud), D0), \
             AROS_LCA(unsigned long, (flags), D1), \
             APTR, (base), 5, Btuart)
#define btuart_set_power(base, owner, enabled) \
    AROS_LC2(long, BTUARTSetPower, \
             AROS_LCA(void *, (owner), A0), \
             AROS_LCA(unsigned long, (enabled), D0), \
             APTR, (base), 6, Btuart)
#define btuart_write(base, owner, data, length) \
    AROS_LC3(long, BTUARTWrite, \
             AROS_LCA(void *, (owner), A0), \
             AROS_LCA(const void *, (data), A1), \
             AROS_LCA(unsigned long, (length), D0), \
             APTR, (base), 7, Btuart)
#define btuart_read(base, owner, data, capacity) \
    AROS_LC3(long, BTUARTRead, \
             AROS_LCA(void *, (owner), A0), \
             AROS_LCA(void *, (data), A1), \
             AROS_LCA(unsigned long, (capacity), D0), \
             APTR, (base), 8, Btuart)

static int uart_open(struct bt_hci_transport *transport)
{
    struct bt_aros_uart_transport *uart = transport->impl;

    if (uart == NULL || uart->opened)
        return BT_ERR_INVALID_ARGUMENT;
    uart->resource = OpenResource((CONST_STRPTR)BTUART_RESOURCE_NAME);
    if (uart->resource == NULL ||
        btuart_get_version(uart->resource) != BTUART_API_VERSION)
    {
        uart->resource = NULL;
        return BT_ERR_IO;
    }
    if (btuart_claim(uart->resource, uart) != BTUART_OK)
    {
        uart->resource = NULL;
        return BT_ERR_NO_RESOURCES;
    }
    if (btuart_configure(uart->resource, uart, 115200,
                         BTUART_CONFIG_RTS_CTS) != BTUART_OK ||
        btuart_set_power(uart->resource, uart, 1) != BTUART_OK)
    {
        btuart_release(uart->resource, uart);
        uart->resource = NULL;
        return BT_ERR_IO;
    }
    bt_h4_rx_init(&uart->h4_rx);
    uart->opened = true;
    return BT_OK;
}

static void uart_close(struct bt_hci_transport *transport)
{
    struct bt_aros_uart_transport *uart = transport->impl;

    if (uart == NULL || !uart->opened)
        return;
    uart->receiving = false;
    uart->receive = NULL;
    uart->receive_data = NULL;
    btuart_release(uart->resource, uart);
    uart->resource = NULL;
    uart->opened = false;
    uart->tx_length = 0;
    uart->tx_offset = 0;
}

static int queue_packet(struct bt_aros_uart_transport *uart,
                        enum bt_hci_packet_type type,
                        const uint8_t *data, size_t length)
{
    uint8_t wire_type = bt_h4_wire_type(type);

    if (!uart->opened || data == NULL || length == 0 || wire_type == 0 ||
        length + 1 > sizeof(uart->tx))
        return BT_ERR_INVALID_ARGUMENT;
    if (uart->tx_offset != uart->tx_length)
        return BT_ERR_NO_RESOURCES;
    uart->tx[0] = wire_type;
    memcpy(uart->tx + 1, data, length);
    uart->tx_length = length + 1;
    uart->tx_offset = 0;
    return BT_OK;
}

static int uart_send_command(struct bt_hci_transport *transport,
                             const uint8_t *data, size_t length)
{
    return queue_packet(transport->impl, BT_HCI_PACKET_COMMAND, data, length);
}

static int uart_send_acl(struct bt_hci_transport *transport,
                         const uint8_t *data, size_t length)
{
    return queue_packet(transport->impl, BT_HCI_PACKET_ACL, data, length);
}

static int unsupported_send(struct bt_hci_transport *transport,
                            const uint8_t *data, size_t length)
{
    (void)transport;
    (void)data;
    (void)length;
    return BT_ERR_INVALID_ARGUMENT;
}

static int uart_start_receive(struct bt_hci_transport *transport,
                              bt_hci_transport_recv_fn receive,
                              void *user_data)
{
    struct bt_aros_uart_transport *uart = transport->impl;

    if (uart == NULL || !uart->opened || uart->receiving || receive == NULL)
        return BT_ERR_INVALID_ARGUMENT;
    uart->receive = receive;
    uart->receive_data = user_data;
    uart->receiving = true;
    return BT_OK;
}

static void uart_stop_receive(struct bt_hci_transport *transport)
{
    struct bt_aros_uart_transport *uart = transport->impl;

    if (uart == NULL)
        return;
    uart->receiving = false;
    uart->receive = NULL;
    uart->receive_data = NULL;
    bt_h4_rx_init(&uart->h4_rx);
}

static const struct bt_hci_transport_ops uart_ops = {
    uart_open,
    uart_close,
    uart_send_command,
    uart_send_acl,
    unsupported_send,
    unsupported_send,
    uart_start_receive,
    uart_stop_receive
};

static void deliver_packet(enum bt_hci_packet_type type, const uint8_t *data,
                           size_t length, void *user_data)
{
    struct bt_aros_uart_transport *uart = user_data;

    if (uart->receiving && uart->receive != NULL)
        uart->receive(&uart->transport, type, data, length,
                      uart->receive_data);
}

void bt_aros_uart_transport_init(struct bt_aros_uart_transport *uart)
{
    if (uart == NULL)
        return;
    memset(uart, 0, sizeof(*uart));
    uart->transport.ops = &uart_ops;
    uart->transport.impl = uart;
    bt_h4_rx_init(&uart->h4_rx);
}

uint32_t bt_aros_uart_transport_signal_mask(
    const struct bt_aros_uart_transport *uart)
{
    (void)uart;
    return 0;
}

bt_status_t bt_aros_uart_transport_poll(struct bt_aros_uart_transport *uart)
{
    uint8_t rx[BT_AROS_UART_RX_CHUNK];
    LONG count;

    if (uart == NULL || !uart->opened)
        return BT_ERR_INVALID_ARGUMENT;
    if (uart->tx_offset < uart->tx_length)
    {
        count = btuart_write(uart->resource, uart,
                             uart->tx + uart->tx_offset,
                             uart->tx_length - uart->tx_offset);
        if (count < 0)
            return BT_ERR_IO;
        uart->tx_offset += (size_t)count;
        if (uart->tx_offset == uart->tx_length)
            uart->tx_offset = uart->tx_length = 0;
    }
    if (!uart->receiving)
        return BT_OK;
    do
    {
        count = btuart_read(uart->resource, uart, rx, sizeof(rx));
        if (count < 0)
            return BT_ERR_IO;
        if (count > 0 &&
            bt_h4_rx_feed(&uart->h4_rx, rx, (size_t)count,
                          deliver_packet, uart) != BT_OK)
            return BT_ERR_IO;
    } while (count == (LONG)sizeof(rx));
    return BT_OK;
}
