# ADR 0001: Use Lid Trigger As Activity Boundary And Persist Box Inventory State Locally

## Status

Accepted

## Context

The Box is moving from a prototype BOOT-button / HC-SR04 lid signal to an MC-38 NO magnetic reed switch. The lid signal is not just a wake source: it defines when a Box is actively observing Tags, when a Field Check Session ends, and when inventory state can be finalized.

The earlier model treated lid close as `imb_session` → `imb_delta` → BLE report delivery. That is not enough for self-contained Boxes with OLED displays. A Box must know its own latest inventory truth even when no phone is available.

## Decision

Use the MC-38 NO reed switch as the production Lid Trigger:

- GPIO4 with internal pull-up
- lid closed = LOW
- lid open = HIGH
- deep-sleep wake on HIGH
- GPIO0 remains BOOT / factory reset only

Introduce a host-testable Lid Trigger component:

- `imb_lid_trigger` owns debounced open/closed state and edge detection
- `imb_lid_trigger_gpio_mc38_no` owns GPIO4 setup, MC-38 NO polarity, and deep-sleep wake

Introduce a host-testable Box Activity orchestrator:

- `imb_box_activity` owns the `imb_session_t` lifecycle
- detector scan events flow through `imb_box_activity`
- scan events mutate the session only while a Field Check Session is open
- lid close freezes the session and attempts local finalization

Separate local finalization from phone delivery:

- A Field Check Report is the transition record produced from a finalized Field Check Session.
- Box Inventory State is the latest persisted per-Box truth.
- A Field Check Session is finalized only when its Field Check Report is applied to persisted Box Inventory State.
- Phone ACK is delivery state only; it does not finalize the session.

Plan `imb_inventory_state` as the NVS-backed persistence component:

- persists latest Box Inventory State
- applies finalized Field Check Reports
- tracks whether the latest Field Check Report is still pending phone delivery
- serves Box Inventory State to OLED and pending reports to BLE

## Consequences

Boxes become self-contained: they can reboot, open, and display their latest persisted Box Inventory State without requiring a phone.

Lid close is no longer enough to discard a session. If the lid reopens before local persistence succeeds, the same session is resumed. If the lid reopens after local persistence succeeds, a new session starts and any undelivered Field Check Report remains pending for BLE delivery.

Registration remains separate from inventory state. Item Registration writes `imb_local` identity after NDEF commit, but it does not seed Box Inventory State. Registered Items begin as Unchecked until a Field Check finalizes them.

This adds complexity beyond a GPIO driver, but it keeps the power/session/report semantics testable and prevents BLE availability from becoming the source of truth.

## Non-Goals

This decision does not implement mesh migration, movable mode, or auto-registration of foreign Tags from other Boxes. Those belong to later Mesh Report preparation.

This decision does not refactor `imb_delta` yet. `imb_inventory_state` can adapt the current delta output first and evolve the richer identity/presence model later.
