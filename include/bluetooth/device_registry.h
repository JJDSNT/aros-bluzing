#ifndef BLUETOOTH_DEVICE_REGISTRY_H
#define BLUETOOTH_DEVICE_REGISTRY_H

#include <bluetooth/addr.h>
#include <bluetooth/types.h>

/*
 * A single "banco unificado de dispositivos" (project.md, Fase 4): every
 * device seen during Classic inquiry or LE scanning is recorded once per
 * address, updated in place on repeated sightings (duplicate filtering),
 * and flagged as dual-mode if it's been seen on both. Fixed-size, no
 * allocation.
 *
 * Known limitation: LE devices using resolvable private addresses will
 * show up as distinct entries each time their address rotates, and won't
 * be matched against a Classic sighting of the same physical device --
 * proper identity resolution needs IRK/bonding data from SMP, out of
 * scope here.
 */

#define BT_DEVICE_FLAG_CLASSIC (1u << 0)
#define BT_DEVICE_FLAG_LE (1u << 1)

struct bt_discovered_device
{
    struct bt_addr addr;
    unsigned flags; /* BT_DEVICE_FLAG_* bitmask; both set means dual-mode */
    uint32_t class_of_device; /* meaningful iff BT_DEVICE_FLAG_CLASSIC is set */
    uint8_t le_address_type;  /* meaningful iff BT_DEVICE_FLAG_LE is set */
    int8_t last_rssi;
    uint32_t sightings;
};

#ifndef BT_DEVICE_REGISTRY_MAX
#define BT_DEVICE_REGISTRY_MAX 32
#endif

struct bt_device_registry
{
    struct bt_discovered_device devices[BT_DEVICE_REGISTRY_MAX];
    size_t count;
};

void bt_device_registry_init(struct bt_device_registry *reg);
size_t bt_device_registry_count(const struct bt_device_registry *reg);
const struct bt_discovered_device *bt_device_registry_get(const struct bt_device_registry *reg,
                                                            size_t index);
const struct bt_discovered_device *bt_device_registry_find(const struct bt_device_registry *reg,
                                                             const struct bt_addr *addr);

/* Records a Classic inquiry sighting, creating a new entry if addr is
 * unseen. Returns NULL only if the registry is full and addr is new. */
struct bt_discovered_device *bt_device_registry_note_classic(struct bt_device_registry *reg,
                                                               const struct bt_addr *addr,
                                                               uint32_t class_of_device);

/* Records an LE advertising sighting, creating a new entry if addr is
 * unseen. Returns NULL only if the registry is full and addr is new. */
struct bt_discovered_device *bt_device_registry_note_le(struct bt_device_registry *reg,
                                                          const struct bt_addr *addr,
                                                          uint8_t le_address_type, int8_t rssi);

#endif /* BLUETOOTH_DEVICE_REGISTRY_H */
