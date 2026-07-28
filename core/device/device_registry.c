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
    dev->class_of_device = class_of_device;
    dev->sightings++;
    return dev;
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
