# Phase 5: Mixer UI and Channel Controls - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md -- this log preserves the alternatives considered.

**Date:** 2026-04-04
**Phase:** 05-mixer-ui-and-channel-controls
**Areas discussed:** Fader style and interaction, Control layout in strips, Metronome controls, State persistence scope, Solo behavior, Local channel switching, Fader value display

---

## Fader Style and Interaction

| Option | Description | Selected |
|--------|-------------|----------|
| Scroll adjusts volume | Mouse wheel over strip adjusts fader, ~0.5 dB per step | ✓ |
| Scroll disabled | Only click-drag changes volume | |
| Scroll with modifier key | Shift/Ctrl + scroll to adjust | |

**User's choice:** Scroll adjusts volume
**Notes:** Fine-grained control common in pro-audio mixers

---

| Option | Description | Selected |
|--------|-------------|----------|
| Double-click resets to 0 dB | Standard pro-audio behavior | ✓ |
| No double-click reset | Only manual drag | |

**User's choice:** Double-click resets to 0 dB

---

| Option | Description | Selected |
|--------|-------------|----------|
| Double-click centers pan | Same reset behavior as fader | ✓ |
| No reset on pan | Only manual drag | |

**User's choice:** Double-click centers pan

---

## Control Layout in Strips

| Option | Description | Selected |
|--------|-------------|----------|
| Pan on top, M/S below | Pan slider (18px) + Mute/Solo buttons (18px) = 38px footer | ✓ |
| M/S on top, pan below | Buttons more accessible at eye level | |

**User's choice:** Pan on top, M/S below
**Notes:** Matches VB-Audio layout

---

## Metronome Controls

| Option | Description | Selected |
|--------|-------------|----------|
| Inside master strip | Horizontal metro fader + mute below master fader | ✓ |
| Separate metronome strip | Dedicated narrow strip | |
| In beat bar area | Small slider near the top | |

**User's choice:** Inside master strip

---

| Option | Description | Selected |
|--------|-------------|----------|
| Volume + Mute only | Simpler, most users just control loudness | ✓ |
| Volume + Pan + Mute | Full control with pan to one ear | |

**User's choice:** Volume + Mute only

---

## State Persistence Scope

**Multi-select question:** What mixer state should persist?

| Option | Description | Selected |
|--------|-------------|----------|
| Local channel settings | Vol, pan, mute, transmit, input selector | ✓ |
| Connection details | Last server, username (not password) | ✓ |
| UI preferences | Scale factor, chat visibility | ✓ |
| Remote user mixer state | Per-user vol/pan/mute keyed by username | |

**User's choice:** Local channel settings, Connection details, UI preferences
**Notes:** Remote user state intentionally excluded -- users change between sessions

---

## Solo Behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Additive solo | Multiple channels soloed simultaneously | ✓ |
| Exclusive solo | Only one channel at a time | |
| Additive + Ctrl exclusive | Default additive, Ctrl+click for exclusive | |

**User's choice:** Additive solo

---

## Local Channel Switching

| Option | Description | Selected |
|--------|-------------|----------|
| Single channel | One local strip, simple | |
| Up to 4 with tabs | Ch1-Ch4 tabs per UI-SPEC | |
| 4 channels with group/child | Same expand/collapse as remote multi-channel | ✓ |

**User's choice:** Expose all 4 channels with same group/child logic and channel selector
**Notes:** User specifically requested the same expand/collapse pattern used for remote multi-channel users, not tabs

---

## Fader Value Display

| Option | Description | Selected |
|--------|-------------|----------|
| dB with one decimal | "-6.0", "+2.5", "-inf" | ✓ |
| Rounded dB integers | "-6", "+3" | |

**User's choice:** dB with one decimal

---

## Claude's Discretion

- VbFader component implementation details
- Pan slider implementation approach
- Local channel expand/collapse state storage
- APVTS parameter IDs for new parameters
- Solo logic implementation layer

## Deferred Ideas

None
