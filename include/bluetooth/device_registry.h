#ifndef BLUETOOTH_DEVICE_REGISTRY_H
#define BLUETOOTH_DEVICE_REGISTRY_H

#include <bluetooth/addr.h>
#include <bluetooth/types.h>

#include <stdbool.h>
#include <stddef.h>

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
/* Human interface device: a keyboard, a mouse or a combination of the two.
 * Set from the Classic class-of-device or from LE advertising data; either
 * sighting is enough, and a dual-mode device usually says so on both. */
#define BT_DEVICE_FLAG_HID (1u << 2)

struct bt_discovered_device
{
    struct bt_addr addr;
    unsigned flags; /* BT_DEVICE_FLAG_* bitmask; both set means dual-mode */
    uint32_t class_of_device; /* meaningful iff BT_DEVICE_FLAG_CLASSIC is set */
    uint8_t le_address_type;  /* meaningful iff BT_DEVICE_FLAG_LE is set */
    uint16_t appearance;      /* LE appearance, 0 when not advertised */
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
/* True if a Classic class-of-device describes a keyboard or pointing device.
 * Major device class 5 is Peripheral; within it bit 6 is keyboard and bit 7 is
 * pointing, and a combo device sets both. */
bool bt_cod_is_hid(uint32_t class_of_device);

/* True if an LE advertising payload announces HID: the HID-over-GATT service
 * UUID 0x1812 in either the complete or the incomplete 16-bit UUID list, or an
 * Appearance whose category is 15 (HID). Writes the appearance out when it is
 * present, so a caller can keep it whether or not it decided HID. */
bool bt_le_adv_is_hid(const uint8_t *data, size_t length, uint16_t *appearance_out);

struct bt_discovered_device *bt_device_registry_note_le(struct bt_device_registry *reg,
                                                          const struct bt_addr *addr,
                                                          uint8_t le_address_type, int8_t rssi);

#endif /* BLUETOOTH_DEVICE_REGISTRY_H */
