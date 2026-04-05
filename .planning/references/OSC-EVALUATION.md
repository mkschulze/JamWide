# OSC Cross-DAW Sync Evaluation

**Date:** April 2026
**Author:** Research deliverable for Phase 7 (DAW Sync and Session Polish)
**Requirements Addressed:** XSYNC-01, XSYNC-02 (v2)
**Analysis as of April 2026**

---

## Goal

Evaluate OSC (Open Sound Control) for cross-DAW tempo and transport synchronization with JamWide. Determine which DAWs support OSC natively, what capabilities are available, and whether OSC is a viable path for cross-DAW sync features.

## Constraints

1. Must work alongside existing AudioPlayHead-based sync (Phase 7 primary approach for in-DAW transport reading).
2. Must not require DAW-specific plugins or proprietary extensions.
3. Should work over network for multi-machine setups (e.g., one musician running JamWide in REAPER on machine A, another in Ableton on machine B).

## Findings

### Evaluation Rubric

| DAW | OSC Native | Transport Read | Transport Write | Tempo Access | Bridge Feasibility | Setup Burden | Notes |
|-----|-----------|----------------|-----------------|--------------|-------------------|--------------|-------|
| REAPER | Yes (built-in) | Yes | Yes | Read/Write BPM | High | Low (built-in) | Best OSC support; `/transport`, `/tempo`, `/track` |
| Logic Pro | No (via IAC/Environment) | Limited | Limited | No native BPM OSC | Low | High (complex routing) | Workaround via MIDI Environment + IAC bus only |
| Ableton Live | Via Max for Live | Yes (M4L) | Yes (M4L) | Yes (M4L) | Medium | Medium (M4L included in Suite since Live 12) | Requires Suite edition; M4L patches handle OSC |
| Bitwig Studio | Controller API (not native OSC) | Yes (controller script) | Yes (controller script) | Yes (controller script) | Medium | Medium (custom script needed) | Controller API is powerful but uses JavaScript, not standard OSC |
| Pro Tools | None | No | No | No | None | N/A | No OSC support whatsoever |
| FL Studio | Limited (MIDI scripting) | No | No | No | None | N/A | No meaningful OSC support |
| Cubase/Nuendo | Via Generic Remote | Limited | Limited | Limited | Low | High (complex config) | Inconsistent implementation, limited OSC mapping |
| Studio One | None | No | No | No | None | N/A | No OSC support |

### Summary of Coverage

- **Good OSC support:** REAPER (native), Ableton Live (via M4L, included in Suite since Live 12), Bitwig (via Controller API scripts)
- **Poor/no support:** Logic Pro, Pro Tools, FL Studio, Cubase/Nuendo, Studio One
- **Coverage:** ~3 of 8 major DAWs (37.5%) have usable OSC support. Only REAPER has it built-in without extra configuration.

### OSC Network Security Note

OSC is unencrypted UDP by default. If used for cross-network DAW sync (not just localhost), authentication and encryption would need to be layered on top (e.g., OSC over TCP with TLS, or a VPN tunnel). For localhost-only use (JamWide plugin communicating with the host DAW on the same machine), this is not a concern.

## Recommendation

OSC is viable for REAPER and Bitwig users, and for Ableton Live users with Suite edition. Cross-DAW coverage is poor -- only approximately 25-37% of major DAWs have usable support, and the setup burden varies significantly.

**Recommendation:** Implement OSC as an optional power-user feature in v2+, not as the primary sync mechanism. JamWide's AudioPlayHead approach (Phase 7 SYNC-01/02) is the universal solution that works in all plugin hosts without any configuration. OSC would serve as a supplementary feature for advanced users who want cross-machine or cross-DAW tempo sync in workflows where AudioPlayHead cannot reach (e.g., syncing two separate DAWs running JamWide instances on different machines).

## Open Questions

1. **Directionality:** Should JamWide send OSC to the DAW (write transport/tempo), receive from the DAW (read transport/tempo), or both? Bidirectional adds complexity.
2. **UDP multicast vs unicast:** Multicast is simpler for discovery but may not work across subnets. Unicast requires explicit IP/port configuration.
3. **Port configuration UX:** How should users configure OSC ports? Auto-discovery, manual input, or preset per-DAW profiles?
4. **Latency tolerance:** OSC over UDP on localhost is sub-millisecond. Over network, 1-10ms is typical. Is this acceptable for tempo sync? (Yes -- tempo is not sample-accurate.)

---

*Research deliverable for JamWide Phase 7. This document informs v2 features XSYNC-01, XSYNC-02.*
