# Inventory Mesh Boxs (IMB)

An event-driven, decentralized smart inventory tracking system for rugged and mobile environments.

## Language

**Mesh**:
A decentralized collection of one or more Boxes that share the same PIN Hash and synchronize their inventory state.
_Avoid_: Network, group, cluster

**Box**:
A physical container equipped with detection hardware (e.g., NFC readers) and computing capability (ESP32-S3) that forms a node in a Mesh.
_Avoid_: Device, node, container

**Item**:
A conceptual object (e.g., "Flashlight", "Hammer") tracked by the system.
_Avoid_: Object, asset

**Tag**:
The physical mechanism (e.g., an NFC sticker with a UID) attached to an Item, used by a Box to detect presence.
_Avoid_: Chip, sensor

**Reachable**:
The state of a Box when it is currently awake and actively communicating with peers over the mesh backhaul.
_Avoid_: Online, connected, active

**Item Registration**:
The act of introducing a new Item to the Mesh by associating a physical object with an identifier (e.g., a Tag).
_Avoid_: Tagging, enrolling, binding

**Anonymous Tag**:
A Tag that has been physically detected by a Box but has not yet been associated with an Item through Registration.
_Avoid_: Pending tag, unknown tag, incomplete item

## Relationships

- A **Mesh** consists of one or more **Boxes**.
- A **Box** detects the presence of **Tags**.
- An **Item** is uniquely identified by one or more **Tags**.
- A **Box** can be **Reachable** or Unreachable to other Boxes in the Mesh.
- An **Anonymous Tag** blocks a Box from returning to normal operation until it is removed or undergoes **Item Registration**.

## Example dialogue

> **Dev:** "If the Phone disconnects while the user is adding things, do the `pendingTags` just get dropped?"
> **Domain expert:** "You mean the **Anonymous Tags**? No, the **Box** remembers them. It can't become fully **Reachable** for normal checks until those tags either undergo **Item Registration** or are physically removed from the box."

## Flagged ambiguities

- "Online" was used to mean both Bluetooth connection to a phone and ESP-Mesh peer-to-peer presence. Resolved: Use **Reachable** for mesh peer presence.
- "Item" and "Tag" were used interchangeably. Resolved: A **Tag** is the physical identifier (sticker), an **Item** is the conceptual object it represents.
- "Pending" was used for tags waiting to be named. Resolved: Use **Anonymous Tag** to avoid confusion with async programming states.
