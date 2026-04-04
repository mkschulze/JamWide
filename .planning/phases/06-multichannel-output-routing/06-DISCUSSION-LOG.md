# Phase 6: Multichannel Output Routing - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md -- this log preserves the alternatives considered.

**Date:** 2026-04-04
**Phase:** 06-multichannel-output-routing
**Areas discussed:** Auto-assign modes, Per-user audio extraction, Routing persistence, DAW compatibility

---

## Auto-assign Modes

| Option | Description | Selected |
|--------|-------------|----------|
| Auto-assign default | New users auto-assigned to next free bus. Manual overrides. | |
| Always manual | All users start on Main Mix. User picks output per strip. | ✓ |
| Auto with user choice | First connect: auto. User can switch modes via toggle. | |

**User's choice:** Always manual
**Notes:** User prefers explicit control. Auto-assign available via quick-assign button.

| Option | Description | Selected |
|--------|-------------|----------|
| One stereo pair per user | All channels from one user mixed to single bus. | ✓ |
| One stereo pair per channel | Each channel gets own bus. | |

**User's choice:** One stereo pair per user

| Option | Description | Selected |
|--------|-------------|----------|
| Quick-assign button | Auto-assigns all current users at once. Best of both. | ✓ |
| Purely manual | Each strip manually assigned. | |
| You decide | Claude picks. | |

**User's choice:** Quick-assign button

| Option | Description | Selected |
|--------|-------------|----------|
| Both modes as dropdown | "Assign by User" and "Assign by Channel" in dropdown. | ✓ |
| Only 'by user' | Single button, always by-user. | |

**User's choice:** Both modes as dropdown

| Option | Description | Selected |
|--------|-------------|----------|
| ConnectionBar, near Fit | Top header bar, visible at all times. | ✓ |
| Above strip area | Between beat bar and strips. | |
| You decide | Claude picks. | |

**User's choice:** ConnectionBar, near Fit button

| Option | Description | Selected |
|--------|-------------|----------|
| Bus stays reserved | Keeps position until reassigned. | ✓ |
| Bus freed immediately | Available for next user. | |

**User's choice:** Bus stays reserved

| Option | Description | Selected |
|--------|-------------|----------|
| Falls back to Main Mix | New user on bus 0. No error. | ✓ |
| Show warning | Audio on bus 0 but warning shown. | |

**User's choice:** Falls back to Main Mix

---

## Per-user Audio Extraction

| Option | Description | Selected |
|--------|-------------|----------|
| Modify AudioProc call | Expand output buffer for per-user audio. ReaNINJAM approach. | ✓ |
| Post-process from internals | Read per-user buffers from NJClient after AudioProc. | |
| You decide | Claude researches best approach. | |

**User's choice:** Modify AudioProc call

| Option | Description | Selected |
|--------|-------------|----------|
| Always full mix on bus 0 | Main Mix = sum of all. Individual buses get copies. | ✓ |
| Exclusive routing | Routed user only on their bus, not on bus 0. | |

**User's choice:** Always full mix

| Option | Description | Selected |
|--------|-------------|----------|
| Main Mix only | Local stays on bus 0. DAW gets dry input separately. | ✓ |
| Routable like remote | Local also gets routing selector. | |

**User's choice:** Main Mix only

| Option | Description | Selected |
|--------|-------------|----------|
| Before routing | Vol/pan applied, then routed. Standard. | ✓ |
| After routing (raw) | Individual buses get raw NJClient audio. | |

**User's choice:** Before routing

---

## Routing Persistence

| Option | Description | Selected |
|--------|-------------|----------|
| Persist mode only | Save routing mode + metronome bus. Not individual mappings. | ✓ |
| Persist everything | Save all user-to-bus mappings keyed by username. | |
| Don't persist | Reset every session. | |

**User's choice:** Persist routing mode only

| Option | Description | Selected |
|--------|-------------|----------|
| Fixed last bus | Metronome always on bus 17. Simple, predictable. | ✓ |
| User-configurable | Dropdown in master strip. | |

**User's choice:** Fixed last bus

| Option | Description | Selected |
|--------|-------------|----------|
| Manual (all on Main Mix) | Safe default. User opts in. | ✓ |
| Auto-assign by user | Immediately separates on connect. | |

**User's choice:** Manual

---

## DAW Compatibility

| Option | Description | Selected |
|--------|-------------|----------|
| Ableton Live | Largest user base. | |
| Bitwig Studio | Good multi-output. Growing jam community. | ✓ |
| Cubase/Nuendo | Professional market. | |

**User's choice:** Bitwig Studio

| Option | Description | Selected |
|--------|-------------|----------|
| Document in tooltip/help | Show tip in routing area + docs. | ✓ |
| Auto-detect and warn | Check enabled buses, warn on disabled. | |

**User's choice:** Document in tooltip/help

---

## Additional Notes

User mentioned username should persist in plugin state. This is already implemented in Phase 5 (D-22: lastUsername as ValueTree property). May need verification/bugfix.

## Claude's Discretion

- AudioProc buffer expansion implementation details
- Quick-assign button UI design
- Bus labeling
- Signal routing in processBlock

## Deferred Ideas

None
