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

**Ambiguous Detection**:
A Tag detection where only one reader fired within the directional window, so whether the Tag was inserted or extracted could not be determined. It becomes the Tag's current state and is surfaced in the report, but is superseded if a later Insert or Extract Detection for the same Tag resolves the direction.
_Avoid_: Pending direction, unresolved scan, ambiguous tag

**Field Check Session**:
The volatile observation window that starts when a Box lid opens in FIELD_CHECK mode and ends when the lid closes. Directional Tag detections update this session in RAM; the session is finalized only when its Field Check Report is applied to persisted Box Inventory State.
_Avoid_: Scan batch, live report

**Field Check Report**:
The transition record produced from a finalized Field Check Session. It describes how Box Inventory State moves from the previous persisted state to the next persisted state. Phone delivery is separate from report finalization.
_Avoid_: Box Report, inventory snapshot, sync, upload

**Box Inventory State**:
The latest persisted per-Box truth about registered Items and observed foreign Tags. It separates identity class (Registered or Foreign) from presence state (Unchecked, Present, Missing, or Ambiguous). The OLED displays this state; BLE may deliver the Field Check Report that produced it.
_Avoid_: Box Report, registry, live session

**Lid Trigger**:
The physical switch signal that defines a Box activity window. For the MC-38 NO magnetic reed switch, the magnet is close when the lid is closed: closed lid pulls GPIO4 LOW, open lid lets GPIO4 pull HIGH.
_Avoid_: Door sensor, lid sensor, wake button

**Mesh Report**:
A consolidated report aggregating Field Check Reports and Box Inventory State from Reachable Boxes in the Mesh. Assembled by the phone after receiving individual Field Check Reports over BLE.
_Avoid_: Full report, global report

**Display State**:
The data struct (`imb_display_state_t`) fed to the display component describing current box status (mode, mesh peer count, phone connectivity, last detection event, report). The display component renders whatever is in this struct and has no knowledge of how values are derived.
_Avoid_: Screen data, UI state

## Relationships

- A **Mesh** consists of one or more **Boxes**.
- A **Box** detects the presence of **Tags**.
- An **Item** is uniquely identified by one or more **Tags**.
- A **Box** can be **Reachable** or Unreachable to other Boxes in the Mesh.
- An **Anonymous Tag** blocks a Box from returning to normal operation until it is removed or undergoes **Item Registration**.
- A **Lid Trigger** opens and closes a **Field Check Session**.
- Every **Box** finalizes a **Field Check Report** by applying it to persisted **Box Inventory State**.
- The OLED displays **Box Inventory State**; BLE delivers **Field Check Reports**.
- A **Mesh Report** is assembled by the phone from the Field Check data of Reachable Boxes.
- **Anonymous Tags** are registration-only and are excluded from **Box Inventory State**.

## Example dialogue

> **Dev:** "If the Phone disconnects while the user is adding things, do the `pendingTags` just get dropped?"
> **Domain expert:** "You mean the **Anonymous Tags**? No, the **Box** remembers them. It can't become fully **Reachable** for normal checks until those tags either undergo **Item Registration** or are physically removed from the box."

## Flagged ambiguities

- "Online" was used to mean both Bluetooth connection to a phone and ESP-Mesh peer-to-peer presence. Resolved: Use **Reachable** for mesh peer presence.
- "Item" and "Tag" were used interchangeably. Resolved: A **Tag** is the physical identifier (sticker), an **Item** is the conceptual object it represents.
- "Pending" was used for tags waiting to be named. Resolved: Use **Anonymous Tag** to avoid confusion with async programming states.
- "AMBIGUOUS" was used both for a single scan's undetermined direction and, separately, for a Tag's general presence uncertainty (e.g. an Extract with no recorded presence). Resolved: **Ambiguous Detection** covers only the former — it is the Tag's state until superseded by a later Insert/Extract for that Tag. An Extract for a Tag with no current presence record has nothing to supersede and is a no-op.
- "Box Report" was used for both the output of a lid-close check and the Box's current inventory truth. Resolved: use **Field Check Report** for the transition record, and **Box Inventory State** for the latest persisted per-Box truth.
