# Phase 7: DAW Sync and Session Polish - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions captured in CONTEXT.md — this log preserves the Q&A.

**Date:** 2026-04-05
**Phase:** 07-daw-sync-and-session-polish
**Mode:** discuss (interactive)
**Areas discussed:** Transport sync behavior, Session position display, Standalone pseudo-transport, Research deliverables, BPM Vote UI design, Live BPM/BPI change notification, Session info strip layout, State persistence

## Round 1: Transport Sync Behavior

| Question | Options Presented | User Selection |
|----------|-------------------|----------------|
| When DAW transport stops, what happens to remote audio? | Silence send only / Silence send + mute receive / JamTaba-style sync mode | **Silence send only** |
| Should the plugin offer interval alignment to DAW measures? | No alignment needed / Full JamTaba-style sync / Deferred to v2 | **Full JamTaba-style sync** |
| Where should the Sync button live? | ConnectionBar / BeatBar area / You decide | **ConnectionBar** |
| BPM/BPI vote controls? | Dedicated vote UI / Chat command only / Deferred to backlog | **Dedicated vote UI** |
| When sync active and server BPM changes? | Auto-disable sync / Attempt to re-sync automatically | **Auto-disable sync** |
| CLAP target DAW sync? | JUCE target only / Both targets | **JUCE target only** |
| During sync WAITING state, transmit audio? | Send silence while waiting / Keep transmitting input | **Send silence while waiting** |
| Sync button in standalone mode? | Hidden in standalone / Disabled with tooltip | **Hidden in standalone** |

## Round 2: Session Position Display

| Question | Options Presented | User Selection |
|----------|-------------------|----------------|
| What session position info should be visible? | Interval count / Elapsed session time / Current beat/total / Sync status | **All four selected** |
| Where should session position info live? | Expand ConnectionBar / Below BeatBar / You decide | **Below BeatBar** |

## Round 3: Standalone Pseudo-Transport

| Question | Options Presented | User Selection |
|----------|-------------------|----------------|
| How should standalone transport work? | Auto-play on connect / Play/Stop button / You decide | **Auto-play on connect** |
| Pseudo beat position display? | Yes BeatBar works / Enhanced metronome-like | **Yes — BeatBar works in standalone** |

## Round 4: Research Deliverables

| Question | Options Presented | User Selection |
|----------|-------------------|----------------|
| Video feasibility approaches to evaluate? | JamTaba H.264 / VDO.Ninja WebRTC / Custom WebRTC / Recommendation only | **VDO.Ninja WebRTC sidecar** |
| OSC/MCP research depth? | Per-DAW matrix + recommendation / Full analysis + PoC / Brief summary | **Brief summary only** |
| VDO.Ninja research focus? | Music sync demo / General integration / Both angles | **Both angles** |
| Research deliverables location? | .planning/references/ / docs/research/ / You decide | **.planning/references/** |

## Round 5: BPM Vote UI Design

| Question | Options Presented | User Selection |
|----------|-------------------|----------------|
| Where should vote controls live? | BeatBar integrated / ConnectionBar dropdown / Session info strip | **BeatBar integrated** |
| Vote submission interaction? | Direct edit + Enter / Edit + confirm button / You decide | **Direct edit + Enter to vote** |

## Round 6: Live BPM/BPI Change Notification

| Question | Options Presented | User Selection |
|----------|-------------------|----------------|
| How to notify user of BPM/BPI changes? | BeatBar flash / System chat message / Toast overlay / Sync auto-disable warning | **BeatBar flash + System chat message** |

## Round 7: Session Info Strip Layout

| Question | Options Presented | User Selection |
|----------|-------------------|----------------|
| Info strip layout? | Single-line compact / Two-line detailed / Collapsible/toggleable | **Collapsible/toggleable** |
| Visible by default? | Always visible / Hidden by default / You decide | **Hidden by default** |

## Round 8: State Persistence

| Question | Options Presented | User Selection |
|----------|-------------------|----------------|
| What Phase 7 state to persist? | Sync preference / Info strip visibility / Last voted BPM/BPI / None | **Session info strip visibility only** |

## User-Provided References

- `.planning/references/JAMTABA-DAW-SYNC-ANALYSIS.md` — JamTaba DAW sync technical analysis
- `https://github.com/steveseguin/vdo.ninja` — VDO.Ninja (WebRTC video)
- `https://vdo.ninja/alpha/examples/music-sync-buffer-demo` — Music sync buffer demo

## Corrections Made

No corrections — all decisions made via direct selection.
