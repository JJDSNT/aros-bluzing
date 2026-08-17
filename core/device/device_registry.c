#include <bluetooth/device_registry.h>

void bt_device_registry_init(struct bt_device_registry *reg)
{
    reg->count = 0;
}

size_t bt_device_registry_count(const struct bt_device_registry *reg)
{
    return reg->count;
}

const struct bt_discovered_device *bt_device_registry_get(const struct bt_device_registry *reg,
                                                            size_t index)
{
    if (index >= reg->count)
        return NULL;
    return &reg->devices[index];
}

const struct bt_discovered_device *bt_device_registry_find(const struct bt_device_registry *reg,
                                                             const struct bt_addr *addr)
{
    size_t i;

    for (i = 0; i < reg->count; i++)
    {
        if (bt_addr_equal(&reg->devices[i].addr, addr))
            return &reg->devices[i];
    }
    return NULL;
}

static struct bt_discovered_device *find_or_create(struct bt_device_registry *reg,
                                                     const struct bt_addr *addr)
{
    size_t i;
    struct bt_discovered_device *dev;

    for (i = 0; i < reg->count; i++)
    {
        if (bt_addr_equal(&reg->devices[i].addr, addr))
            return &reg->devices[i];
    }

    if (reg->count >= BT_DEVICE_REGISTRY_MAX)
        return NULL;

    dev = &reg->devices[reg->count++];
    dev->addr = *addr;
    dev->flags = 0;
    dev->class_of_device = 0;
    dev->le_address_type = 0;
    dev->last_rssi = 0;
    dev->sightings = 0;
    return dev;
}

struct bt_discovered_device *bt_device_registry_note_classic(struct bt_device_registry *reg,
                                                               const struct bt_addr *addr,
                                                               uint32_t class_of_device)
{
    struct bt_discovered_device *dev = find_or_create(reg, addr);

    if (dev == NULL)
        return NULL;

    dev->flags |= BT_DEVICE_FLAG_CLASSIC;
    if (bt_cod_is_hid(class_of_device))
        dev->flags |= BT_DEVICE_FLAG_HID;
    dev->class_of_device = class_of_device;
    dev->sightings++;
    return dev;
}

/*
 * Class of device: 24 bits, of which bits 12..8 are the major device class and
 * bits 7..2 the minor. Major 5 is Peripheral, and inside it bit 6 means
 * keyboard and bit 7 pointing device -- a combined keyboard-and-touchpad sets
 * both. Nothing else in the peripheral class is an input device we care about,
 * so requiring one of those two bits keeps joysticks and remotes out.
 */
bool bt_cod_is_hid(uint32_t class_of_device)
{
    const uint32_t major = (class_of_device >> 8) & 0x1fu;
    const uint32_t minor = (class_of_device >> 2) & 0x3fu;

    if (major != 0x05u)
        return false;
    return (minor & 0x30u) != 0u;
}

/*
 * Advertising data is a sequence of length-prefixed structures: one byte of
 * length covering the type byte and the payload, then the type, then the
 * payload. A zero length ends the list, which is how padding to the fixed
 * 31-byte field is expressed.
 *
 * Two things here say HID. The service UUID list (type 0x02 incomplete, 0x03
 * complete) may contain 0x1812, HID over GATT. The Appearance (0x19) is a
 * 16-bit value whose top ten bits are a category, and category 15 is HID --
 * which covers keyboard (0x03C1) and mouse (0x03C2) without enumerating them.
 */
bool bt_le_adv_is_hid(const uint8_t *data, size_t length, uint16_t *appearance_out)
{
    size_t i = 0;
    bool hid = false;

    if (appearance_out != NULL)
        *appearance_out = 0;
    if (data == NULL)
        return false;

    while (i < length)
    {
        const uint8_t field_len = data[i];
        uint8_t type;
        const uint8_t *payload;
        size_t payload_len;

        if (field_len == 0)
            break;
        if (i + 1u + field_len > length)
            break;              /* truncated field; take what was valid */
        type = data[i + 1u];
        payload = &data[i + 2u];
        payload_len = field_len - 1u;

        if ((type == 0x02u || type == 0x03u) && payload_len >= 2u)
        {
            size_t u;

            for (u = 0; u + 1u < payload_len; u += 2u)
            {
                const uint16_t uuid =
                    (uint16_t)payload[u] | ((uint16_t)payload[u + 1u] << 8);

                if (uuid == 0x1812u)
                    hid = true;
            }
        }
        else if (type == 0x19u && payload_len >= 2u)
        {
            const uint16_t appearance =
                (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);

            if (appearance_out != NULL)
                *appearance_out = appearance;
            if ((appearance >> 6) == 15u)
                hid = true;
        }
        i += 1u + field_len;
    }
    return hid;
}

struct bt_discovered_device *bt_device_registry_note_le(struct bt_device_registry *reg,
                                                          const struct bt_addr *addr,
                                                          uint8_t le_address_type, int8_t rssi)
{
    struct bt_discovered_device *dev = find_or_create(reg, addr);

    if (dev == NULL)
        return NULL;

    dev->flags |= BT_DEVICE_FLAG_LE;
    dev->le_address_type = le_address_type;
    dev->last_rssi = rssi;
    dev->sightings++;
    return dev;
}
