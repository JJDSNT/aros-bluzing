# Bluetooth Device Identity, Dual-Mode Operation, Reverse Initiation and Persistent Relationships

**Status:** Architectural Considerations  
**Scope:** Bluetooth subsystem behavior  
**Applies to:** BR/EDR, Bluetooth Low Energy and dual-mode devices

---

# 1. Purpose

This document records architectural requirements and considerations concerning four closely related aspects of a Bluetooth subsystem:

* device name resolution;
* dual-mode BR/EDR + LE devices;
* remotely initiated connections and reverse discovery;
* persistence of device relationships and reconnection state.

These areas become particularly important once basic discovery, connection, pairing and profile support are functional.

They should not be treated as isolated features.

Together, they determine whether the Bluetooth subsystem understands a remote device merely as something temporarily observed during discovery or as a persistent system object with an identity, security relationship, capabilities and connection policy.

The central principle is:

> **A Bluetooth device identity must remain meaningful independently of a particular discovery event, transport bearer, connection instance or application session.**

---

# 2. Device Identity Versus Observed Address

A Bluetooth address observed during discovery is not, by itself, a complete device identity.

The subsystem should distinguish between:

~~~text
observed address
       │
       ▼
discovered peer
       │
       ▼
resolved identity
       │
       ▼
known device
~~~

This distinction is particularly important for Bluetooth Low Energy.

LE devices may use:

* public addresses;
* random static addresses;
* resolvable private addresses;
* non-resolvable private addresses.

Consequently:

> **The address currently visible over the air must not automatically become the permanent identity of the device.**

For bonded LE devices using privacy, an Identity Resolving Key (IRK) may allow a newly observed private address to be associated with an already known device.

Conceptually:

~~~text
new advertisement
       │
       ▼
temporary/RPA address
       │
       ▼
identity resolution
       │
       ├── unresolved ──► newly observed peer
       │
       └── resolved ───► existing known device
~~~

This identity model also provides the foundation for persistent bonding, automatic reconnection and dual-mode device handling.

---

# 3. Device Name Resolution

A device name should be treated as metadata associated with a device identity, not as the identity itself.

Names may:

* not be available during initial discovery;
* become available asynchronously;
* change;
* be incomplete;
* come from different mechanisms depending on the Bluetooth transport.

The subsystem must therefore allow a device to exist before its name is known.

For example:

~~~text
Device
    Address = xx:xx:xx:xx:xx:xx
    Name    = unknown
~~~

may later become:

~~~text
Device
    Address = xx:xx:xx:xx:xx:xx
    Name    = "Bluetooth Mouse"
~~~

without creating a new device object.

## 3.1 BR/EDR Name Resolution

BR/EDR discovery does not require the complete remote device name to be immediately available.

Name resolution may require a separate Remote Name Request.

The expected model is therefore asynchronous:

~~~text
Inquiry
   │
   ▼
device discovered
   │
   ├── immediately usable as a discovered device
   │
   └── optional name resolution
             │
             ▼
        device updated
~~~

Discovery should not unnecessarily block while names are resolved sequentially.

---

# 4. LE Device Names

Bluetooth Low Energy may provide a local name in advertising or scan-response data.

Possible advertising information includes:

* Complete Local Name;
* Shortened Local Name.

However, the presence of either is not guaranteed.

A connected LE device may also expose its Device Name through GATT.

Therefore the subsystem may learn a name through several paths:

~~~text
LE discovery
    │
    ├── advertising name
    │
    ├── scan-response name
    │
    └── no name
            │
            ▼
       later connection
            │
            ▼
           GATT
            │
            ▼
       Device Name
~~~

These should update the same logical device.

The subsystem should also distinguish between a device-provided name and a user-assigned alias if aliases are supported.

---

# 5. Name Caching

Resolved names may be cached.

Caching avoids unnecessary name-resolution operations every time a known device is observed.

A cached name, however, should not be considered immutable.

Conceptually:

~~~text
Known Device
    ├── identity
    ├── current address information
    ├── reported name
    ├── optional user alias
    └── name cache metadata
~~~

Applications and preference tools should be able to display a useful cached name while the subsystem independently refreshes information when appropriate.

---

# 6. Dual-Mode Devices

A Bluetooth device may support both:

* BR/EDR;
* Bluetooth Low Energy.

Such a device should not automatically be represented as two unrelated devices merely because it is visible through two different discovery mechanisms.

The architecture should support the concept of:

~~~text
                 Device Identity
                       │
            ┌──────────┴──────────┐
            │                     │
        BR/EDR bearer          LE bearer
            │                     │
        BR/EDR state            LE state
~~~

The device is the persistent logical object.

BR/EDR and LE are communication bearers associated with it.

---

# 7. Independent Bearer State

Although BR/EDR and LE may belong to the same logical device, their runtime states must not necessarily be collapsed into a single state variable.

For example:

~~~text
Device
    │
    ├── BR/EDR
    │      connected = false
    │
    └── LE
           connected = true
~~~

is a perfectly meaningful state.

Consequently, a single:

~~~text
connected = true/false
~~~

may be insufficient as the authoritative internal representation.

An aggregate property may still be exposed:

~~~text
device.connected =
    bredr.connected OR le.connected
~~~

but it should be derived from bearer-specific state rather than replace it.

---

# 8. Dual-Mode Discovery

A dual-mode device may be encountered through different discovery paths.

For example:

~~~text
BR/EDR Inquiry
      │
      ▼
   Device A

LE Scan
      │
      ▼
   Device ?
~~~

The subsystem should attempt to determine whether these observations represent the same persistent device.

This association must not rely blindly on textual names.

Names are not unique identifiers.

Likewise, simple address equality should not be assumed to solve every identity case, particularly when LE privacy is involved.

The architecture should therefore permit observations from different bearers to converge on one persistent device identity when sufficient identity information becomes available.

---

# 9. Security State of Dual-Mode Devices

A logical device may have security material associated with different transports.

Conceptually:

~~~text
Known Device
    │
    ├── BR/EDR security
    │      └── Link Key
    │
    └── LE security
           ├── LTK
           ├── IRK
           └── CSRK
~~~

The precise security material depends on the procedures and capabilities involved.

The important architectural requirement is:

> **Device identity and security state must not be reduced to a single Bluetooth address plus a generic "paired" flag.**

A device may have different security relationships on different bearers while still representing one logical peer.

---

# 10. Locally Initiated Connections

The most obvious connection flow is locally initiated:

~~~text
local system
     │
     │ Connect
     ▼
remote device
     │
     ▼
connection established
~~~

In this case, the subsystem normally already has context describing:

* which device is being connected;
* which bearer is intended;
* why the connection was requested;
* potentially which service or profile requires it.

This is the simplest case.

It must not become an assumption embedded throughout the architecture.

---

# 11. Remotely Initiated Connections

Bluetooth connections may also originate from the remote side.

Conceptually:

~~~text
remote device
      │
      │ connection request
      ▼
local controller
      │
      ▼
Bluetooth subsystem
~~~

The subsystem must therefore be capable of receiving a connection for which no currently executing local `Connect()` operation exists.

The connection event itself may cause the subsystem to locate or construct the corresponding device context.

A possible flow is:

~~~text
incoming connection
        │
        ▼
identify remote peer
        │
        ├── known
        │     │
        │     ▼
        │ reuse persistent device
        │
        └── unknown
              │
              ▼
        create discovered/
        temporary device
              │
              ▼
        security and policy
              │
              ▼
        service/profile handling
~~~

Connection management must therefore be fundamentally event-driven and bilateral.

---

# 12. Reverse Service Discovery

A remotely initiated connection may arrive from a peer whose capabilities are not yet known.

The subsystem may consequently need to perform service discovery after accepting or observing the connection.

Conceptually:

~~~text
incoming connection
        │
        ▼
peer recognized
        │
        ▼
services unknown?
        │
       yes
        │
        ▼
service discovery
        │
        ├── SDP for relevant BR/EDR services
        │
        └── GATT discovery for relevant LE services
        │
        ▼
class/profile matching
~~~

This may be described as reverse discovery because service discovery follows a connection initiated by the remote device rather than a normal locally driven discovery/connect sequence.

The policy controlling such discovery should belong to the Bluetooth subsystem rather than to a scanning application.

---

# 13. Connection Persistence

A physical Bluetooth connection cannot survive:

* controller reset;
* subsystem restart;
* operating-system reboot;
* power loss.

Therefore the term "persistent connection" should be interpreted carefully.

What persists is not the ACL or LE connection itself.

What may persist is the **relationship with the remote device**.

~~~text
runtime connection
      │
      X  reboot
      │
      ▼
persistent relationship
      │
      ▼
future reconnection
~~~

---

# 14. Persistent Device Relationship

A persistent known-device record may contain information such as:

~~~text
Known Device
    │
    ├── persistent identity
    ├── known addresses / identity information
    ├── reported name
    ├── optional alias
    ├── device capabilities
    ├── known services
    ├── BR/EDR security material
    ├── LE security material
    ├── class/profile associations
    ├── trust policy
    └── reconnection policy
~~~

Not every implementation must persist every field.

Security-sensitive material must be stored appropriately.

The important distinction is between information describing a persistent peer relationship and transient information describing the current radio connection.

---

# 15. Bonding Is Not Connection

Several concepts must remain distinct:

~~~text
discovered
    ≠
known
    ≠
paired
    ≠
bonded
    ≠
trusted
    ≠
connected
    ≠
auto-connect
~~~

An implementation may expose simplified user-facing states, but these distinctions should not disappear from the internal architecture.

For example, a mouse may be:

~~~text
known       = yes
bonded      = yes
trusted     = yes
connected   = no
autoConnect = yes
~~~

This describes a perfectly normal powered-off mouse.

When the mouse becomes available again, the subsystem may reconnect without repeating the original pairing procedure.

---

# 16. Persistent Security Material

Bonding is useful precisely because security information survives the original connection.

Depending on the transport and security procedure, persistent material may include:

~~~text
BR/EDR
    └── Link Key

LE
    ├── LTK
    ├── IRK
    └── CSRK
~~~

The IRK has additional importance for LE privacy because it may permit changing resolvable private addresses to be associated with the same persistent identity.

Therefore:

~~~text
new RPA
   │
   ▼
IRK resolution
   │
   ▼
Known Device
~~~

is fundamentally different from:

~~~text
new address
   │
   ▼
new device
~~~

Failure to preserve this distinction can result in duplicated devices, repeated pairing requests and failed reconnection.

---

# 17. Reconnection Policy

Reconnection should be treated as policy rather than as an accidental side effect of bonding.

Possible states include:

~~~text
bonded + no auto-connect

bonded + auto-connect

known but not bonded

temporarily connected

trusted persistent device
~~~

Different profiles may also have different expectations.

A persistent HID keyboard or mouse, for example, normally benefits greatly from automatic restoration of its usable state.

The desired user experience is:

~~~text
first use

discover
   ↓
pair/bond
   ↓
authorize/trust
   ↓
bind profile
   ↓
device works
~~~

and subsequently:

~~~text
later boot / device power-on

device becomes available
        ↓
identity recognized
        ↓
existing bond recognized
        ↓
connection/reconnection
        ↓
profile restored
        ↓
device works
~~~

The discovery and pairing workflow should not normally need to be repeated.

---

# 18. Persistence Storage

On systems following Amiga/AROS conventions, persistent Bluetooth configuration can naturally participate in the normal volatile/persistent configuration model, including mechanisms such as `ENV:` and `ENVARC:` where appropriate.

The architectural distinction remains:

~~~text
runtime state
     │
     ▼
ENV: / memory state

persistent configuration
     │
     ▼
ENVARC: / persistent storage
~~~

The exact representation is an implementation decision.

This document does not prescribe whether devices are stored as:

* individual files;
* one configuration database;
* IFF-like records;
* serialized subsystem objects;
* another native configuration format.

What matters is that persistent device identity and security relationships can be reconstructed when the subsystem starts again.

---

# 19. Startup Reconstruction

Subsystem initialization should conceptually be able to perform:

~~~text
Bluetooth subsystem starts
          │
          ▼
load persistent configuration
          │
          ▼
reconstruct known devices
          │
          ▼
restore security identities/keys
          │
          ▼
initialize controller
          │
          ▼
apply controller-side state
          │
          ▼
begin normal operation
~~~

A known device therefore exists conceptually before it is rediscovered during the current boot.

Discovery should update an existing known object when appropriate rather than automatically create a duplicate.

---

# 20. Device Availability Versus Device Existence

This leads to an important architectural distinction.

A persistent device may exist in the subsystem while being completely unavailable over the radio:

~~~text
Known Device
    identity  = Mouse A
    bonded    = yes
    trusted   = yes
    present   = no
    connected = no
~~~

Later:

~~~text
Mouse A appears
      │
      ▼
same Known Device
    present   = yes
    connected = ...
~~~

Applications should therefore not equate the list of known devices with the results of the latest discovery operation.

These are different views of subsystem state.

---

# 21. Events and User Interfaces

Name resolution, identity resolution, incoming connections and reconnection are asynchronous operations.

The subsystem should expose changes as events rather than requiring user interfaces to continuously reconstruct state.

Conceptually:

~~~text
Bluetooth subsystem
       │
       ├── DEVICE_DISCOVERED
       ├── DEVICE_UPDATED
       ├── NAME_RESOLVED
       ├── IDENTITY_RESOLVED
       ├── DEVICE_CONNECTED
       ├── DEVICE_DISCONNECTED
       ├── BOND_CREATED
       ├── BOND_REMOVED
       └── SERVICES_CHANGED
                │
                ▼
          interested clients
~~~

The exact event API is implementation-specific.

The architectural requirement is that GUI tools must not become the authority for these transitions.

---

# 22. Ownership

The Bluetooth subsystem should own:

* device identities;
* known-device records;
* bearer state;
* connection state;
* security relationships;
* service knowledge;
* persistent device policy;
* identity resolution;
* reconnection policy.

Applications and preference tools should inspect and control this state through public interfaces.

They should not independently maintain authoritative Bluetooth device databases.

Conceptually:

~~~text
       applications / preferences
                 │
                 │ public API
                 ▼
          Bluetooth subsystem
                 │
       ┌─────────┼─────────┐
       ▼         ▼         ▼
   devices    profiles   controllers
       │
       ▼
persistent configuration
~~~

---

# 23. Example: Persistent HID Device

Consider a dual-mode Bluetooth mouse.

During its first use:

~~~text
discovery
    │
    ▼
device object
    │
    ▼
identity established
    │
    ▼
name resolved
    │
    ▼
services discovered
    │
    ▼
HID capability recognized
    │
    ▼
pairing / bonding
    │
    ▼
security material stored
    │
    ▼
HID class binding
    │
    ▼
mouse operational
~~~

The computer is then rebooted.

The desired sequence becomes:

~~~text
Bluetooth subsystem starts
        │
        ▼
known mouse restored
        │
        ▼
bond/security restored
        │
        ▼
mouse becomes available
        │
        ▼
identity matched
        │
        ▼
connection established
        │
        ▼
existing HID relationship restored
        │
        ▼
mouse operational
~~~

There should normally be no need for:

~~~text
manual scan
    ↓
manual selection
    ↓
new pairing
    ↓
new profile configuration
~~~

on every boot.

---

# 24. Architectural Invariants

The following invariants are recommended.

## 24.1 Device identity is not a connection

A connection may disappear without destroying the persistent device.

## 24.2 Device identity is not a currently observed address

This is particularly important for LE privacy.

## 24.3 Device name is metadata

Names may be unknown, delayed, cached or changed.

## 24.4 BR/EDR and LE are separate bearers

A dual-mode device may have independent connection and security state for each bearer.

## 24.5 Incoming connections are first-class events

The subsystem must not assume every connection was initiated locally.

## 24.6 Bonding and auto-connect are independent

Remembering security material does not automatically imply a policy to maintain or restore a connection.

## 24.7 Persistent state belongs to the subsystem

Applications should not be required to remain running for device relationships or reconnection behavior to work.

## 24.8 Discovery updates identity

Rediscovering a known device should normally update the existing persistent object rather than create another logical device.

---

# 25. Implementation Questions to Verify

When evaluating an existing Bluetooth implementation, the following questions should be answered.

## Identity

* Is there a persistent device object independent of discovery results?
* Are observed addresses distinguished from persistent identity?
* Can LE resolvable private addresses be resolved through stored IRKs?
* Can rediscovery update an existing device instead of duplicating it?

## Names

* Is BR/EDR Remote Name Request supported?
* Are LE advertising names parsed?
* Can the Device Name be obtained through GATT?
* Is name resolution asynchronous?
* Are names cached?
* Is a user alias distinct from the remote-reported name?

## Dual Mode

* Can one logical device represent both BR/EDR and LE?
* Are bearer-specific connection states maintained?
* Is bearer-specific security material maintained?
* Can observations from BR/EDR and LE converge onto one identity?

## Reverse Initiation

* Can an incoming connection exist without a pending local Connect operation?
* Can an incoming peer be associated with an existing known device?
* Can an unknown incoming peer create an appropriate temporary device context?
* Can service discovery be triggered after a remotely initiated connection?

## Persistence

* Are known devices restored when the subsystem starts?
* Are bonding keys persisted?
* Is LE identity information persisted?
* Are trust and auto-connect separate policies?
* Can profiles/classes recover their relationship with persistent devices?

## Reconnection

* Can a bonded HID device become operational again without manual discovery?
* Is reconnection driven by subsystem policy rather than by a GUI?
* Can the remote device initiate restoration of the relationship?
* Does reboot preserve the relationship while correctly discarding transient connection state?

---

# 26. Summary

The four areas covered here are manifestations of the same underlying requirement:

> **Bluetooth must model persistent peers rather than transient scan results.**

Name resolution enriches the peer.

Dual-mode handling allows multiple radio bearers to belong to that peer.

Reverse initiation allows the peer to participate actively in connection establishment.

Persistence allows the relationship with the peer to survive the lifetime of an individual connection or operating-system session.

A mature model therefore looks approximately like:

~~~text
                   Persistent Device
                          │
        ┌─────────────────┼─────────────────┐
        │                 │                 │
     Identity          Metadata          Policy
        │                 │                 │
   addresses/IRK       name/alias      trust/reconnect
        │
        ├──────────────────────────────┐
        │                              │
        ▼                              ▼
   BR/EDR bearer                    LE bearer
        │                              │
   connection state               connection state
   security state                 security state
   SDP/services                   GATT/services
        │                              │
        └──────────────┬───────────────┘
                       ▼
                classes/profiles
~~~

This separation makes discovery, pairing, reconnection, dual-mode operation and user interfaces consequences of one coherent device model rather than independent special cases.
