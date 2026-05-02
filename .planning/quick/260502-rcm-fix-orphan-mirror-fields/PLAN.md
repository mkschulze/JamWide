---
phase: quick
plan: 260502-rcm-fix-orphan-mirror-fields
type: execute
wave: 1
depends_on: []
files_modified:
  - src/threading/spsc_payloads.h
  - src/core/njclient.h
  - src/core/njclient.cpp
  - tests/test_remote_user_mirror.cpp
  - src/build_number.h
autonomous: true
requirements:
  - RCM-FIX-01  # close 15.1-07a RemoteUserMirror orphan-fields gap
tags:
  - audio
  - realtime
  - spsc
  - mirror
  - 15.1-07a
must_haves:
  truths:
    - "Run-thread mutations to RemoteUser_Channel.{flags,volume,pan,out_chan_index} reach m_remoteuser_mirror[slot].chans[ch] before the next AudioProc block consumes them."
    - "PeerChannelInfoUpdate is published from BOTH user-info-change-notify (njclient.cpp:~1760, post-m_users_cs.Leave) AND SetUserChannelState (njclient.cpp:~4342-4344) any time setvol/setpan/setoutch/flags-change fires."
    - "PeerChannelInfoUpdate apply visitor in drainRemoteUserUpdates writes all four fields onto the mirror with the same MAX_PEERS / MAX_USER_CHANNELS bounds-check shape used by sibling PeerCodecSwapUpdate."
    - "PeerChannelInfoUpdate publishes AFTER PeerAddedUpdate AND BEFORE PeerChannelMaskUpdate (and therefore before PeerNextDsUpdate, which is published from start_decode callsites — covered by SPSC FIFO across publishers). This preserves both the legacy 1863-1871 PeerAddedUpdate-first invariant (mirror slot must be active=true before per-channel attrs land) and the new ChannelInfo-before-Mask/NextDs invariant (per-channel attributes precede mask-driven present/mute/solo apply)."
    - "Two relaxed-atomic counters (m_chinfo_publishes_observed / m_chinfo_applies_observed, sized [MAX_PEERS][MAX_USER_CHANNELS]) tick at publish and apply sites; public noexcept relaxed-load accessors expose them so a UAT readout can falsifiably link symptom to fix."
    - "spsc_payloads.h carries an explicit deviation block ABOVE the new struct citing .planning/debug/remote-channels-cutoff.md as authority and superseding the stale Codex M-9 FINAL marker (the FINAL annotation in the file header is updated, not left lying)."
    - "tests/test_remote_user_mirror.cpp gains roundtrip + 1000-cycle concurrent + bounds-check + ordering tests for PeerChannelInfoUpdate, all green under Release and TSan with zero ThreadSanitizer reports."
    - "Specialist review by realtime-audio-reviewer is the FINAL pre-commit gate (schema is/was Codex M-9 FINAL; deviating without RT-safety review is not allowed)."
  artifacts:
    - path: "src/threading/spsc_payloads.h"
      provides: "PeerChannelInfoUpdate struct, added to RemoteUserUpdate variant, with deviation comment + updated FINAL marker"
      contains: "struct PeerChannelInfoUpdate"
    - path: "src/core/njclient.h"
      provides: "m_chinfo_publishes_observed / m_chinfo_applies_observed counter arrays + GetChannelInfoPublishCount / GetChannelInfoApplyCount accessors"
      contains: "m_chinfo_publishes_observed"
    - path: "src/core/njclient.cpp"
      provides: "Two publisher sites (user-info-change-notify + SetUserChannelState) + apply visitor branch + counter bumps + counter zero-init"
      contains: "PeerChannelInfoUpdate"
    - path: "tests/test_remote_user_mirror.cpp"
      provides: "PeerChannelInfoUpdate roundtrip / concurrent / bounds / ordering tests"
      contains: "PeerChannelInfoUpdate"
    - path: "src/build_number.h"
      provides: "Build number bumped (291 -> 292 or whatever auto-increment yields)"
      contains: "JAMWIDE_BUILD_NUMBER"
  key_links:
    - from: ".planning/debug/remote-channels-cutoff.md"
      to: "this plan"
      via: "Diagnosed root cause; cited verbatim in spsc_payloads.h deviation block"
      pattern: "remote-channels-cutoff"
    - from: ".planning/debug/build253-vu-and-disconnect.md"
      to: "this plan"
      via: "Same architectural era / same legacy-invariant audit pattern; cross-pollinated suspicion"
      pattern: "build253-vu-and-disconnect"
    - from: "src/core/njclient.cpp:1881-1891 (existing PeerChannelMaskUpdate publish)"
      to: "new PeerChannelInfoUpdate publish at the same site"
      via: "Same try_push + m_remoteuser_update_overflows.fetch_add overflow pattern; ordered before any PeerNextDsUpdate"
      pattern: "m_remoteuser_update_q\\.try_push.*PeerChannelInfoUpdate"
    - from: "src/core/njclient.cpp:4381 (existing PeerChannelMaskUpdate publish in SetUserChannelState)"
      to: "new PeerChannelInfoUpdate publish at the same site"
      via: "Coalesced single update covers any of setvol/setpan/setoutch/flags-change for that channel"
      pattern: "m_remoteuser_update_q\\.try_push.*PeerChannelInfoUpdate"
    - from: "src/core/njclient.cpp:drainRemoteUserUpdates apply visitor"
      to: "m_remoteuser_mirror[u.slot].chans[u.channel].{flags,volume,pan,out_chan_index}"
      via: "if constexpr (std::is_same_v<T, jamwide::PeerChannelInfoUpdate>) branch with bounds check"
      pattern: "PeerChannelInfoUpdate.*chans\\[u\\.channel\\]"
---

<objective>
Close the orphan-fields gap left by milestone 15.1-07a's RemoteUserMirror migration. The audio thread reads `flags`, `volume`, `pan`, `out_chan_index` from `m_remoteuser_mirror[s].chans[ch]` on every block, but no SPSC payload was ever defined to carry those four fields from the run thread, so they are stuck at the `PeerRemovedUpdate` reset defaults (1.0/0.0/0/0). Result: instamode peers (`flags & 2`) starve after 1-2 NINJAM intervals; user-set per-peer output routing is silently bypassed.

Purpose: Restore the canonical -> mirror data flow for the four orphan per-channel attribute fields, with cheap relaxed-atomic instrumentation so the symptom-to-defect link is falsifiable in a single UAT cycle. This is the FOURTH layer of the same legacy-invariant-audit class that produced fixes b9899a0, e827453, e151dd8 — see `feedback_legacy_invariant_audit.md` and `.planning/debug/remote-channels-cutoff.md`.

Output: New `PeerChannelInfoUpdate` SPSC variant + two publisher sites + apply visitor + diagnostic counters with public accessors + extended unit/concurrency/ordering tests + build bump. Done at this plan = built, tests green (Release + TSan), realtime-audio-reviewer accepted. UAT in JamWide host is a separate user-driven step.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md
@.planning/PROJECT.md
@.planning/debug/remote-channels-cutoff.md
@.planning/debug/build253-vu-and-disconnect.md
@src/threading/spsc_payloads.h
@src/core/njclient.h
@tests/test_remote_user_mirror.cpp

<interfaces>
<!-- Extracted from the codebase so executor does not have to scavenger-hunt. -->

From src/threading/spsc_payloads.h (current FINAL marker is in the header comment, lines 6-7; will be updated by Task 1):
```cpp
namespace jamwide {

struct PeerAddedUpdate       { int slot; int user_index; };
struct PeerRemovedUpdate     { int slot; };
struct PeerChannelMaskUpdate { int slot; int submask, chanpresentmask, mutedmask, solomask; };
struct PeerVolPanUpdate      { int slot; bool muted; float volume, pan; };       // peer-level only
struct PeerNextDsUpdate      { int slot; int channel; int slot_idx; DecodeState* ds; };
struct PeerCodecSwapUpdate   { int slot; int channel; unsigned int new_fourcc; };

using RemoteUserUpdate = std::variant<
    PeerAddedUpdate, PeerRemovedUpdate, PeerChannelMaskUpdate,
    PeerVolPanUpdate, PeerNextDsUpdate, PeerCodecSwapUpdate>;

static_assert(std::is_trivially_copyable_v<RemoteUserUpdate>, ...);

} // namespace jamwide
```
**Required addition (Task 1):** `PeerChannelInfoUpdate { int slot; int channel; unsigned int flags; float volume; float pan; int out_chan_index; }`, appended to the variant before the static_assert (so the trivially-copyable assertion catches any regression).

From src/core/njclient.h:202-256 (RemoteUserChannelMirror — read sites are .flags / .volume / .pan / .out_chan_index, all written nowhere except PeerRemovedUpdate reset block):
```cpp
struct RemoteUserChannelMirror {
    bool         present;
    bool         muted;
    bool         solo;
    float        volume = 1.0f;
    float        pan = 0.0f;
    int          out_chan_index = 0;
    unsigned int flags = 0;
    unsigned int codec_fourcc = 0;
    DecodeState* ds = nullptr;
    DecodeState* next_ds[2] = {nullptr, nullptr};
    int          dump_samples = 0;
    double       curds_lenleft = 0.0;
    std::atomic<float> peak_vol_l{0.0f};
    std::atomic<float> peak_vol_r{0.0f};
};
```

From src/core/njclient.h:725-740 area (existing per-NJClient mirror + overflow counter pattern to mirror):
```cpp
RemoteUserMirror m_remoteuser_mirror[MAX_PEERS];
std::atomic<uint64_t> m_remoteuser_update_overflows{0};
```

From src/core/njclient.cpp:1881-1891 (existing PeerChannelMaskUpdate publish, post-m_users_cs.Leave — the EXACT pattern to mirror at the new ChannelInfoUpdate publish):
```cpp
if (publish_mask_change && user_slot >= 0) {
  if (!m_remoteuser_update_q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerChannelMaskUpdate{user_slot, pub_submask,
                                      pub_chanpresentmask,
                                      pub_mutedmask, pub_solomask}})) {
    m_remoteuser_update_overflows.fetch_add(1, std::memory_order_relaxed);
    writeLog("WARNING: m_remoteuser_update_q full on PeerChannelMaskUpdate (slot=%d)\n", user_slot);
  }
}
```

From src/core/njclient.cpp:3239-3265 (existing PeerChannelMaskUpdate apply branch — the EXACT shape to mirror for the new PeerChannelInfoUpdate apply branch):
```cpp
else if constexpr (std::is_same_v<T, jamwide::PeerChannelMaskUpdate>) {
  if (u.slot < 0 || u.slot >= MAX_PEERS) return;
  auto& m = m_remoteuser_mirror[u.slot];
  // ...
}
```

From src/core/njclient.cpp:4342-4344 (SetUserChannelState canonical writes — site of new publisher B):
```cpp
if (setvol)   p->volume         = vol;
if (setpan)   p->pan            = pan;
if (setoutch) p->out_chan_index = outchannel;
```
The publish at line 4381 fires when `publish_mask` is true. The new PeerChannelInfoUpdate must fire when ANY of setvol/setpan/setoutch (or a flags-change at publisher A's site) is true — coalesce so one update per call covers all changed fields.

From tests/test_remote_user_mirror.cpp (existing structure):
- Uses local `test_njclient::MAX_PEERS` / `MAX_USER_CHANNELS` mirroring production constants.
- Apply visitor under test is replicated locally (test-only) so the variant logic is unit-testable without linking NJClient.
- Existing tests cover PeerAdded/Removed, PeerChannelMaskUpdate, PeerVolPanUpdate, PeerNextDsUpdate slotting, concurrent peer churn, OOB ignore, HIGH-3 generation-gate. Add new tests in the same style.
</interfaces>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Add PeerChannelInfoUpdate to SPSC schema + add diagnostic counters and accessors on NJClient</name>
  <files>
    src/threading/spsc_payloads.h
    src/core/njclient.h
    src/core/njclient.cpp
  </files>
  <action>
**1a. Update src/threading/spsc_payloads.h.**
- Update the file-level FINAL marker (header comment lines 6-7 — currently reads "FINALIZED at this revision per Codex M-9 — no later sub-plan ... modifies this header"). Do NOT leave that comment unchanged. Replace it with: "Originally FINALIZED at 15.1-04 per Codex M-9. SUPERSEDED 2026-05-02 to add PeerChannelInfoUpdate — see deviation block at the new struct definition and `.planning/debug/remote-channels-cutoff.md` for authority. The header remains stable across 15.1-05/06/07a/07b/07c/08/09; this is a post-15.1 stabilization patch closing the orphan-fields gap left by 15.1-07a."
- Immediately ABOVE the new struct (place it after `PeerCodecSwapUpdate` in the RemoteUserUpdate group), add a deviation block:
  ```
  // -------------------------------------------------------------------------
  // DEVIATION FROM CODEX M-9 "FINAL" — 2026-05-02.
  //
  // Authority: .planning/debug/remote-channels-cutoff.md (gsd-debugger,
  // status=diagnosed, root cause CONFIRMED).
  //
  // 15.1-07a's RemoteUserMirror migration moved the audio-thread reads of
  // four per-channel fields (flags, volume, pan, out_chan_index) onto the
  // mirror but shipped no SPSC payload to carry them. Audio mixed every
  // remote channel as if flags=0 / volume=1.0 / pan=0.0 / out_chan_index=0
  // regardless of canonical state — instamode peers starved after 1-2
  // NINJAM intervals; user-set output routing was silently bypassed.
  //
  // Same shape as the three already-fixed 15.1-07a regressions (b9899a0,
  // e827453, e151dd8) and matches the legacy-invariant audit pattern in
  // memory `feedback_legacy_invariant_audit.md`.
  //
  // Closing this gap requires breaking the M-9 FINAL marker on this header.
  // The header-level FINAL annotation has been updated to mark the
  // supersession explicitly. RT-safety review by realtime-audio-reviewer
  // is the gating pre-commit step (see PLAN Task 3).
  // -------------------------------------------------------------------------
  struct PeerChannelInfoUpdate {
      int          slot = 0;
      int          channel = 0;
      unsigned int flags = 0;
      float        volume = 1.0f;
      float        pan = 0.0f;
      int          out_chan_index = 0;
  };
  ```
- Append `PeerChannelInfoUpdate` to the `RemoteUserUpdate` std::variant template parameter list (after `PeerCodecSwapUpdate`). The existing `static_assert(std::is_trivially_copyable_v<RemoteUserUpdate>, ...)` will exercise the new payload automatically.

**1b. Update src/core/njclient.h.**
- Locate the existing `m_remoteuser_mirror[MAX_PEERS]` declaration (~line 725) and the nearby `m_remoteuser_update_overflows` (~line 736). Add the two new counter arrays adjacent (audio-thread RT-safety: `std::atomic<uint64_t>`, default-initialized to zero, sized `[MAX_PEERS][MAX_USER_CHANNELS]`):
  ```cpp
  // Diagnostic counters for the 2026-05-02 RemoteUserMirror orphan-fields fix.
  // Bumped at PeerChannelInfoUpdate publish (run thread) and apply (audio
  // thread) sites. Relaxed atomics — purpose is falsifiable UAT readout, not
  // synchronization. See .planning/debug/remote-channels-cutoff.md.
  std::atomic<uint64_t> m_chinfo_publishes_observed[MAX_PEERS][MAX_USER_CHANNELS]{};
  std::atomic<uint64_t> m_chinfo_applies_observed  [MAX_PEERS][MAX_USER_CHANNELS]{};
  ```
  Note: brace-default-init `{}` zeroes a std::atomic via aggregate init in C++17 array-of-atomics with default-constructible element. If this trips a compiler bug or aggregate-init incompatibility, fall back to explicit zero-init in the NJClient constructor (Task 1c).
- Below the existing public `GetUserChannelPeak` declaration (~line 431), add:
  ```cpp
  // Falsifiable UAT readout for the 2026-05-02 RemoteUserMirror orphan-fields
  // fix. Returns the per-(slot,channel) count of PeerChannelInfoUpdate
  // publishes (run thread) / applies (audio thread). Both relaxed-load.
  // See .planning/debug/remote-channels-cutoff.md.
  uint64_t GetChannelInfoPublishCount(int slot, int channel) const noexcept;
  uint64_t GetChannelInfoApplyCount  (int slot, int channel) const noexcept;
  ```

**1c. Update src/core/njclient.cpp.**
- In the NJClient constructor (or wherever per-slot atomics are initialized — match the convention used by `m_remoteuser_update_overflows`; if no explicit init is needed because of `{}` in the header, skip), ensure both new arrays are zero-initialized at construction. Bounds-check via static_assert if helpful: `static_assert(MAX_PEERS > 0 && MAX_USER_CHANNELS > 0, ...);`.
- Implement the two accessors at file scope (place near the existing `GetUserChannelPeak` definition):
  ```cpp
  uint64_t NJClient::GetChannelInfoPublishCount(int slot, int channel) const noexcept {
    if (slot < 0 || slot >= MAX_PEERS) return 0;
    if (channel < 0 || channel >= MAX_USER_CHANNELS) return 0;
    return m_chinfo_publishes_observed[slot][channel].load(std::memory_order_relaxed);
  }
  uint64_t NJClient::GetChannelInfoApplyCount(int slot, int channel) const noexcept {
    if (slot < 0 || slot >= MAX_PEERS) return 0;
    if (channel < 0 || channel >= MAX_USER_CHANNELS) return 0;
    return m_chinfo_applies_observed[slot][channel].load(std::memory_order_relaxed);
  }
  ```

This task does NOT publish or apply anything yet. It only adds the schema, counters, and accessors. The existing build must still compile; existing tests must still pass. (Task 2 wires the publishers + apply visitor; Task 3 extends tests + bumps build.)
  </action>
  <verify>
    <automated>cd build-juce &amp;&amp; ./scripts/build.sh 2>&amp;1 | tail -40 &amp;&amp; grep -c "PeerChannelInfoUpdate" /Users/cell/dev/JamWide/src/threading/spsc_payloads.h | (read n; [ "$n" -ge 2 ] || (echo "FAIL: PeerChannelInfoUpdate not found in spsc_payloads.h"; exit 1)) &amp;&amp; grep -c "GetChannelInfoPublishCount" /Users/cell/dev/JamWide/src/core/njclient.h | (read n; [ "$n" -ge 1 ] || (echo "FAIL: accessor decl missing"; exit 1)) &amp;&amp; grep -c "GetChannelInfoPublishCount" /Users/cell/dev/JamWide/src/core/njclient.cpp | (read n; [ "$n" -ge 1 ] || (echo "FAIL: accessor defn missing"; exit 1))</automated>
  </verify>
  <done>
- spsc_payloads.h carries the new struct + deviation block + updated FINAL marker. The static_assert on RemoteUserUpdate trivial-copyability still passes (compile gate).
- njclient.h declares both counter arrays and both accessors.
- njclient.cpp defines both accessors and zero-inits both counter arrays.
- Build succeeds with no errors. Existing tests still pass.
- No publisher / apply branch yet (deferred to Task 2).
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: Wire two publisher sites + apply visitor + counter bumps + ordering invariant</name>
  <files>
    src/core/njclient.cpp
  </files>
  <behavior>
- Publisher A (user-info-change-notify, after `m_users_cs.Leave()` at ~njclient.cpp:1861, BEFORE the existing PeerChannelMaskUpdate publish at lines 1881-1891 — placing first preserves ordering invariant against PeerNextDsUpdate): when `publish_added` OR a flags-change OR mask change OR a `theuser->channels[cid].out_chan_index` write fired in the m_users_cs block, capture pub-time per-channel `pub_chflags`, `pub_chvol`, `pub_chpan`, `pub_choutch` for `cid` (inside the m_users_cs block), then post-Leave try_push a `PeerChannelInfoUpdate{user_slot, cid, pub_chflags, pub_chvol, pub_chpan, pub_choutch}`. Bump `m_chinfo_publishes_observed[user_slot][cid].fetch_add(1, std::memory_order_relaxed)` immediately after a SUCCESSFUL try_push (do NOT bump on overflow, otherwise the symptom-to-defect link breaks).
- Publisher B (SetUserChannelState, at ~njclient.cpp:4377 — same site as the existing publish_mask path at line 4381, BEFORE that publish to preserve ordering invariant): if any of setvol / setpan / setoutch / a flags-change fired in this call, coalesce into ONE PeerChannelInfoUpdate carrying the post-write field values for `channelidx`. Bump `m_chinfo_publishes_observed[slot][channelidx]` on successful try_push.
- Apply visitor (drainRemoteUserUpdates at njclient.cpp:3193+): new `if constexpr (std::is_same_v<T, jamwide::PeerChannelInfoUpdate>)` branch with the same MAX_PEERS / MAX_USER_CHANNELS bounds check shape used by `PeerCodecSwapUpdate` (njclient.cpp:3327-3331). Inside: copy all four fields onto `m_remoteuser_mirror[u.slot].chans[u.channel]`. Bump `m_chinfo_applies_observed[u.slot][u.channel].fetch_add(1, std::memory_order_relaxed)`.
- Ordering invariant: at every publish point that emits both PeerChannelInfoUpdate and PeerNextDsUpdate for the same channel, PeerChannelInfoUpdate is enqueued FIRST. SPSC preserves FIFO so the audio thread observes ChannelInfo before NextDs. (PeerNextDsUpdate is published by `start_decode` callsites, NOT by these publisher sites — but verify by grep that no publish point this task adds emits PeerNextDsUpdate. If a future site does, the ordering MUST hold.)
- Overflow path: on try_push failure, increment `m_remoteuser_update_overflows.fetch_add(1, std::memory_order_relaxed)` and `writeLog("WARNING: m_remoteuser_update_q full on PeerChannelInfoUpdate (slot=%d ch=%d)\n", slot, channel);` — exact pattern of the existing overflow handlers at lines 1888-1889 and 4385.
- RT-safety: every publisher site is ALREADY post-`m_users_cs.Leave()` (per the existing PeerChannelMaskUpdate pattern); no new lock acquisition; no allocations on the audio thread; counter bumps are relaxed atomics. This must be reviewable by realtime-audio-reviewer (see Task 3 gate).
  </behavior>
  <action>
**2a. Publisher A — user-info-change-notify path (njclient.cpp ~lines 1740-1891).**

Inside the `m_users_cs` block (after the existing canonical writes at lines 1745-1801), add capture variables. Place them adjacent to the existing pub_submask/pub_chanpresentmask/pub_mutedmask/pub_solomask (lines 1802-1805):
```cpp
unsigned int pub_chflags = theuser->channels[cid].flags;
float        pub_chvol   = theuser->channels[cid].volume;
float        pub_chpan   = theuser->channels[cid].pan;
int          pub_choutch = theuser->channels[cid].out_chan_index;
```
Add a new boolean `publish_chinfo = false;` near the top of the function (alongside `publish_added` / `publish_mask_change` etc.). Set `publish_chinfo = true;` whenever the add-channel branch fires (before line 1806 — i.e. mirror it on `publish_mask_change`'s set-points, since any time mask changes for an add we want chinfo too; ALSO include the path at lines 1745-1760 where flags change without a chanpresentmask change — set `publish_chinfo = true;` there too).

After `m_users_cs.Leave()` (line 1861), insert the new publish block AFTER the existing `if (publish_added && user_slot >= 0) { ... }` block CLOSES (line 1880, the `}`) and BEFORE the existing `if (publish_mask_change && user_slot >= 0)` block OPENS (line 1881). Independent grep verification (2026-05-02): line 1872 = `if (publish_added && user_slot >= 0)`, line 1880 = `}` closing that block, line 1881 = `if (publish_mask_change && user_slot >= 0)` — line numbers MATCH the prompt; no drift. Insert the following code block exactly between lines 1880 and 1881:
```cpp
// 2026-05-02 RemoteUserMirror orphan-fields fix: publish per-channel
// flags/volume/pan/out_chan_index. Ordered AFTER PeerAddedUpdate (mirror
// slot is active=true before per-channel attrs land) and BEFORE
// PeerChannelMaskUpdate (per-channel attributes precede mask-driven
// present/mute/solo apply). Preserves both the legacy 1863-1871 invariant
// and the new ChannelInfo-before-NextDs invariant. PeerNextDsUpdate is
// published from start_decode callsites (separate publisher); SPSC FIFO
// across publishers guarantees the audio thread sees ChannelInfo first.
// See .planning/debug/remote-channels-cutoff.md.
if (publish_chinfo && user_slot >= 0)
{
  if (m_remoteuser_update_q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerChannelInfoUpdate{user_slot, cid, pub_chflags,
                                       pub_chvol, pub_chpan, pub_choutch}}))
  {
    m_chinfo_publishes_observed[user_slot][cid]
        .fetch_add(1, std::memory_order_relaxed);
  }
  else
  {
    m_remoteuser_update_overflows.fetch_add(1, std::memory_order_relaxed);
    writeLog("WARNING: m_remoteuser_update_q full on PeerChannelInfoUpdate (slot=%d ch=%d)\n", user_slot, cid);
  }
}
```

**2b. Publisher B — SetUserChannelState (njclient.cpp ~lines 4310-4387).**

After the canonical writes at lines 4342-4344 (and the `if (setmute) / if (setsolo)` blocks), introduce a coalesced flag inside the `m_users_cs` lock block:
```cpp
const bool publish_chinfo = (setvol || setpan || setoutch);
```
Place this AFTER the four canonical writes but BEFORE `m_users_cs.Leave()` (which is the existing `}` at the end of the lock block before line 4377). Capture pub-time values inside the lock too (alongside the existing pub_submask/pub_chanpresentmask/pub_mutedmask/pub_solomask captures at lines 4371-4374):
```cpp
unsigned int pub_chflags = p->flags;
float        pub_chvol   = p->volume;
float        pub_chpan   = p->pan;
int          pub_choutch = p->out_chan_index;
```
After the `m_users_cs` block closes (i.e. AFTER `slot = findRemoteUserSlot(user);` at line 4375 and the closing brace of the lock block, BEFORE the existing `if (publish_mask && slot >= 0)` at line 4379), insert:
```cpp
// 2026-05-02 RemoteUserMirror orphan-fields fix — see Publisher A comment.
// Ordered BEFORE PeerChannelMaskUpdate so audio thread sees vol/pan/outch
// updates before any same-tick mute/solo apply.
if (publish_chinfo && slot >= 0)
{
  if (m_remoteuser_update_q.try_push(jamwide::RemoteUserUpdate{
        jamwide::PeerChannelInfoUpdate{slot, channelidx, pub_chflags,
                                       pub_chvol, pub_chpan, pub_choutch}}))
  {
    m_chinfo_publishes_observed[slot][channelidx]
        .fetch_add(1, std::memory_order_relaxed);
  }
  else
  {
    m_remoteuser_update_overflows.fetch_add(1, std::memory_order_relaxed);
    writeLog("WARNING: m_remoteuser_update_q full on PeerChannelInfoUpdate (slot=%d ch=%d)\n", slot, channelidx);
  }
}
```

**2c. Apply visitor — drainRemoteUserUpdates (njclient.cpp ~line 3327, mirroring PeerCodecSwapUpdate's exact shape).**

Add a new branch BETWEEN the existing PeerNextDsUpdate (~3271-3326) and PeerCodecSwapUpdate (~3327-3331) branches — placement is purely stylistic; SPSC ordering, not visitor branch ordering, governs runtime semantics:
```cpp
else if constexpr (std::is_same_v<T, jamwide::PeerChannelInfoUpdate>) {
  // 2026-05-02 RemoteUserMirror orphan-fields fix. Apply per-channel
  // flags/volume/pan/out_chan_index onto the mirror. These fields are
  // read by the audio thread on EVERY block at njclient.cpp:2807 / 2816
  // / 2817 / 3732 / 3733 / 4071. Without this branch they are stuck at
  // PeerRemovedUpdate-reset defaults (1.0/0.0/0/0).
  // See .planning/debug/remote-channels-cutoff.md.
  if (u.slot < 0 || u.slot >= MAX_PEERS) return;
  if (u.channel < 0 || u.channel >= MAX_USER_CHANNELS) return;
  auto& chan = m_remoteuser_mirror[u.slot].chans[u.channel];
  chan.flags          = u.flags;
  chan.volume         = u.volume;
  chan.pan            = u.pan;
  chan.out_chan_index = u.out_chan_index;
  m_chinfo_applies_observed[u.slot][u.channel]
      .fetch_add(1, std::memory_order_relaxed);
}
```

**2d. Ordering invariant — verification.**
After making the publisher edits, run a grep audit to confirm neither new publisher site emits a `PeerNextDsUpdate` between its `PeerChannelInfoUpdate` push and the next `m_users_cs` reentry:
```
grep -n "PeerChannelInfoUpdate\|PeerNextDsUpdate" src/core/njclient.cpp | grep -v '^#' | head -40
```
Expected: PeerNextDsUpdate publishes are NOT in user-info-change-notify or SetUserChannelState. They are emitted by separate run-thread paths (start_decode / decode-arm-completion) which happen later. SPSC FIFO guarantees the per-channel order of the same-publisher emissions. The audio thread drain (drainRemoteUserUpdates at line 3193) processes the queue in FIFO order, so PeerChannelInfoUpdate published at T0 is applied before PeerNextDsUpdate published at T1 > T0.

If a third site emerges that publishes BOTH for the same channel within one critical section, place PeerChannelInfoUpdate first.
  </action>
  <verify>
    <automated>cd build-juce &amp;&amp; ./scripts/build.sh 2>&amp;1 | tail -40 &amp;&amp; PUB=$(grep -v '^[[:space:]]*//\|^[[:space:]]*\*' /Users/cell/dev/JamWide/src/core/njclient.cpp | grep -c "try_push.*PeerChannelInfoUpdate") &amp;&amp; [ "$PUB" -eq 2 ] || (echo "FAIL: expected 2 publisher try_push sites, got $PUB"; exit 1) &amp;&amp; APPLY=$(grep -c "is_same_v<T, jamwide::PeerChannelInfoUpdate>" /Users/cell/dev/JamWide/src/core/njclient.cpp) &amp;&amp; [ "$APPLY" -eq 1 ] || (echo "FAIL: expected 1 apply branch, got $APPLY"; exit 1) &amp;&amp; BUMPS=$(grep -c "m_chinfo_publishes_observed.*fetch_add\|m_chinfo_applies_observed.*fetch_add" /Users/cell/dev/JamWide/src/core/njclient.cpp) &amp;&amp; [ "$BUMPS" -ge 3 ] || (echo "FAIL: expected at least 3 counter fetch_adds, got $BUMPS"; exit 1)</automated>
  </verify>
  <done>
- Two publisher sites publish PeerChannelInfoUpdate via `m_remoteuser_update_q.try_push`, each followed by a relaxed `fetch_add` on `m_chinfo_publishes_observed[slot][channel]` on success and an overflow log + counter bump on failure.
- Apply visitor has the new branch with bounds check, all four field copies, and a relaxed `fetch_add` on `m_chinfo_applies_observed`.
- Ordering invariant verified by grep: no PeerNextDsUpdate publish straddles a PeerChannelInfoUpdate publish in the same critical section. SPSC FIFO covers the cross-publisher case.
- Build succeeds; existing tests still pass.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 3: Extend test_remote_user_mirror.cpp (roundtrip + concurrent + bounds + ordering), bump build, specialist review gate</name>
  <files>
    tests/test_remote_user_mirror.cpp
    src/build_number.h
  </files>
  <behavior>
- New test "PeerChannelInfoUpdate roundtrip: all four fields propagate to mirror" — publish a single PeerChannelInfoUpdate with non-default values, drain, assert mirror[slot].chans[channel].{flags,volume,pan,out_chan_index} match.
- New test "PeerChannelInfoUpdate concurrent producer/consumer (1000 cycles)" — single producer thread emits a mix of PeerAdded / PeerChannelMaskUpdate / PeerChannelInfoUpdate / PeerVolPanUpdate / PeerNextDsUpdate / PeerRemovedUpdate; consumer thread drains. After 1000 cycles, mirror is consistent (no torn fields, no leaks). Run under TSan with zero ThreadSanitizer reports.
- New test "PeerChannelInfoUpdate bounds-check: out-of-range slot or channel ignored" — push slot=MAX_PEERS, slot=-1, channel=MAX_USER_CHANNELS, channel=-1; drain; assert no UB and mirror state unchanged.
- New test "PeerChannelInfoUpdate ordering: ChannelInfoUpdate published first reaches mirror before PeerNextDsUpdate for same channel" — push ChannelInfoUpdate{flags=2}, then PeerNextDsUpdate; drain; assert mirror.chans[ch].flags == 2 BEFORE next_ds slot is observed populated. (SPSC FIFO + the apply-visitor's straight-line semantics make this trivially true; the test exists so a future regression that reorders or splits the drain loop trips immediately.)
- All existing tests in tests/test_remote_user_mirror.cpp continue to pass.
- Build number in src/build_number.h is bumped (291 -> 292, or whatever the auto-increment cmake hook yields; see memory `project_build_number_automation.md`).
- Specialist review checkpoint gate at the end (see action) — schema was Codex M-9 FINAL; deviating without realtime-audio-reviewer accept is forbidden.
  </behavior>
  <action>
**3a. Extend tests/test_remote_user_mirror.cpp.**

The file already replicates the apply visitor locally (see lines ~107-160). Extend that local visitor to handle PeerChannelInfoUpdate using the same shape as PeerCodecSwapUpdate (production parity):
```cpp
else if constexpr (std::is_same_v<T, jamwide::PeerChannelInfoUpdate>) {
    if (u.slot < 0 || u.slot >= MAX_PEERS) return;
    if (u.channel < 0 || u.channel >= MAX_USER_CHANNELS) return;
    auto& chan = g_mirror[u.slot].chans[u.channel];
    chan.flags          = u.flags;
    chan.volume         = u.volume;
    chan.pan            = u.pan;
    chan.out_chan_index = u.out_chan_index;
}
```
(The test mirror struct `TestRemoteUserChannelMirror` may need new fields if it does not already have all four — extend it to include `flags`, `volume`, `pan`, `out_chan_index` if missing; preserve existing fields.)

Append four new TEST blocks at the end of the test sequence (before the final HIGH-3 generation-gate tests, or after — wherever section ordering reads cleanest):

1. `TEST("PeerChannelInfoUpdate apply roundtrip: all four fields propagate")` — push one update with `{slot=3, channel=5, flags=2u, volume=0.7f, pan=-0.25f, out_chan_index=4}`, drain, assert mirror reflects.
2. `TEST("PeerChannelInfoUpdate concurrent (1000 cycles): no torn fields under producer/consumer")` — model on the existing "concurrent peer churn" test at line 304. Mix all six variant types in a deterministic-but-shuffled producer loop; consumer drains in lockstep. Final assert: counters and field values align with the last-published-wins semantics for the slot/channel under test.
3. `TEST("PeerChannelInfoUpdate bounds-check: OOB slot/channel ignored, no UB")` — push four malformed updates (slot=MAX_PEERS, slot=-1, channel=MAX_USER_CHANNELS, channel=-1), drain, assert no segfault and mirror state untouched.
4. `TEST("PeerChannelInfoUpdate orders before PeerNextDsUpdate for same channel")` — push ChannelInfoUpdate{slot=2, channel=1, flags=2u, volume=1.0f, pan=0.0f, out_chan_index=0}, then PeerNextDsUpdate{slot=2, channel=1, slot_idx=0, ds=&fake_ds_marker}, drain, assert mirror.chans[1].flags == 2 AND mirror.chans[1].next_ds[0] == &fake_ds_marker (both true after drain — order matters because the real audio thread reads flags on the SAME block it picks up next_ds).

If TSan-only assertions or build flags are needed, gate them on `__has_feature(thread_sanitizer)` or a project-defined macro. The build script (scripts/build.sh) already handles Release; TSan run uses `build-tsan/` per the existing repo layout (visible in git status — `build-tsan/` already exists). If TSan harness invocation isn't automated, document the manual command in the test file header.

Add a static_assert at the bottom of the file mirroring the existing ones at lines 486-487:
```cpp
static_assert(std::is_trivially_copyable_v<jamwide::PeerChannelInfoUpdate>,
              "PeerChannelInfoUpdate must be trivially copyable (HIGH-2 contract)");
```

**3b. Bump build number.**
Update src/build_number.h: `JAMWIDE_BUILD_NUMBER 291` -> `JAMWIDE_BUILD_NUMBER 292`. Per memory `project_build_number_automation.md`, the cmake increment hook may auto-bump on next configure; if so, this manual bump may be redundant — do the manual bump anyway and let the auto-increment continue from 292+ on the next build.

**3c. Build + run tests.**

The TSan run is part of the AUTOMATED verify command (see `<verify><automated>` below) — not a soft prose-only step. The verify command chains Release build+test followed by TSan build+test and fails the task if EITHER reports a failure or any ThreadSanitizer warning.

Release:
```
cd /Users/cell/dev/JamWide/build-juce && ./scripts/build.sh
# Locate via direct executable (verified 2026-05-02: build-tsan/test_remote_user_mirror at build root, not under tests/):
./tests/test_remote_user_mirror 2>/dev/null || ./test_remote_user_mirror
```

TSan:
```
cd /Users/cell/dev/JamWide/build-tsan
# build-tsan is already configured (CMakeCache.txt present, ninja files in place — verified 2026-05-02 from git status).
# If for some reason CMakeCache.txt is missing, configure first with the project's TSan preset, e.g.:
#   cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DJAMWIDE_TSAN=ON ..
# Then build only the test target (faster than full rebuild):
cmake --build . --target test_remote_user_mirror
# Run (binary lives at build-tsan/test_remote_user_mirror per verified file layout):
./tests/test_remote_user_mirror 2>/dev/null || ./test_remote_user_mirror
```

Both runs must report zero ThreadSanitizer warnings and all tests green. Match the bar set by 15.1-07a's existing test run. The verify command's grep on `WARNING: ThreadSanitizer` is the gating check — TSan reports start with that exact prefix.

**3d. SPECIALIST REVIEW REQUIRED — realtime-audio-reviewer (FINAL pre-commit gate).**

This task is NOT done at "tests green". Before the executor proposes a commit, invoke the realtime-audio-reviewer specialist (Task tool, subagent_type matching the project's specialist registry — likely `realtime-audio-reviewer`). Provide the diff and the deviation block from spsc_payloads.h. The specialist must accept on:
- No allocations on the audio thread.
- All lock acquisitions on the run-thread side stay outside the SPSC try_push.
- Counter bumps are relaxed atomics (no synchronization implication).
- The Codex M-9 FINAL deviation is justified and the audit trail (deviation block + updated header marker + memory entries) is in place.
- Ordering invariant is sound (PeerChannelInfoUpdate before PeerNextDsUpdate for the same channel; SPSC FIFO covers cross-publisher).

If the reviewer requests changes, apply them and re-invoke; do NOT commit until the reviewer accepts. Document the reviewer's accept in the commit body.

**Out-of-scope future work (not implemented in this plan; record so they aren't lost):**
- (i) `njclient.cpp:3313-3325` "both slots full" silence-marker double-free risk: when `incoming_ds==nullptr` arrives while two real ds are queued, real ds_A in next_ds[0] is defer-deleted — destroys one interval of real audio.
- (ii) `njclient.cpp:1906-1916` 200ms generation-gate spinwait leaks slots when host audio thread is paused.
- (iii) sessionmode-flags-wrong: publisher filter at line 1995 makes sessionmode peers silent-by-accident today; logical state is wrong but audible-correct.
  </action>
  <verify>
    <automated>cd /Users/cell/dev/JamWide/build-juce &amp;&amp; ./scripts/build.sh 2>&amp;1 | tail -20 &amp;&amp; ls test_remote_user_mirror tests/test_remote_user_mirror 2>/dev/null | head -1 | (read p; [ -n "$p" ] || (echo "FAIL: test_remote_user_mirror binary not located"; exit 1)) &amp;&amp; (./tests/test_remote_user_mirror 2>/dev/null || ./test_remote_user_mirror) 2>&amp;1 | tee /tmp/rcm-test-release.log | tail -80 &amp;&amp; grep -q "PeerChannelInfoUpdate apply roundtrip" /tmp/rcm-test-release.log &amp;&amp; grep -q "PeerChannelInfoUpdate concurrent" /tmp/rcm-test-release.log &amp;&amp; grep -q "PeerChannelInfoUpdate bounds-check" /tmp/rcm-test-release.log &amp;&amp; grep -q "PeerChannelInfoUpdate orders before" /tmp/rcm-test-release.log &amp;&amp; ! grep -qE "FAILED:|FAIL:|assertion failed|SEGFAULT" /tmp/rcm-test-release.log &amp;&amp; grep -q "292" /Users/cell/dev/JamWide/src/build_number.h &amp;&amp; cd /Users/cell/dev/JamWide/build-tsan &amp;&amp; ([ -f CMakeCache.txt ] || (echo "FAIL: build-tsan not configured — run cmake first per Task 3c"; exit 1)) &amp;&amp; cmake --build . --target test_remote_user_mirror 2>&amp;1 | tail -30 &amp;&amp; ([ -f ./tests/test_remote_user_mirror ] &amp;&amp; ./tests/test_remote_user_mirror || ./test_remote_user_mirror) 2>&amp;1 | tee /tmp/rcm-test-tsan.log | tail -120 &amp;&amp; ! grep -qE "WARNING: ThreadSanitizer|FAILED:|FAIL:|assertion failed|SEGFAULT" /tmp/rcm-test-tsan.log &amp;&amp; echo "OK: Release + TSan both green, all four new tests present, no TSan warnings, build_number >= 292"</automated>
  </verify>
  <done>
- Four new tests in tests/test_remote_user_mirror.cpp pass under Release.
- Same four tests pass under TSan with zero ThreadSanitizer reports.
- All previously-existing tests continue to pass.
- src/build_number.h is bumped (>= 292).
- realtime-audio-reviewer has accepted the diff and the acceptance is captured in the commit body.
- UAT in JamWide host is OUT OF SCOPE for this plan; that is a separate user-driven step. Done = code lands, all tests green, specialist accepted.
  </done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| run-thread → audio-thread | SPSC handoff via m_remoteuser_update_q. Trust contract: trivially-copyable payload only (static_assert enforced). No raw run-thread-owned pointers leak into the audio thread mirror except via documented ownership-transfer (DecodeState* via PeerNextDsUpdate). PeerChannelInfoUpdate carries POD-only fields. |
| audio-thread → run-thread (counters) | Read-only relaxed-load via public NJClient accessors. No back-channel; counters are diagnostic only. |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-RCM-01 | T (tampering) | PeerChannelInfoUpdate payload | mitigate | static_assert(std::is_trivially_copyable_v<RemoteUserUpdate>) at file scope blocks any future mutation of the struct that introduces non-trivial fields (heap pointers, std::string, vtables). |
| T-RCM-02 | I (info disclosure) | counter accessors | accept | Counters expose only per-(slot,channel) publish/apply counts. Not user data; not network-reachable. UAT readout only. |
| T-RCM-03 | D (denial of service) | SPSC try_push overflow | mitigate | On try_push failure, m_remoteuser_update_overflows is bumped and a writeLog warning emitted, but no exception, no audio glitch, and no data loss beyond the missed update — same behavior as existing PeerChannelMaskUpdate overflow path. |
| T-RCM-04 | E (elevation) | apply visitor bounds check | mitigate | u.slot / u.channel are always range-checked against MAX_PEERS / MAX_USER_CHANNELS before indexing into m_remoteuser_mirror; matches PeerCodecSwapUpdate visitor shape (njclient.cpp:3327-3331). Out-of-range indices are silently dropped — audio thread cannot be steered to write past the array. |
| T-RCM-05 | R (repudiation) | PeerChannelInfoUpdate ordering | mitigate | The plan explicitly verifies PeerChannelInfoUpdate is enqueued BEFORE PeerNextDsUpdate at any site that publishes both for the same channel. SPSC FIFO ordering combined with straight-line drain semantics guarantees the audio thread sees ChannelInfo first. Test (4) in tests/test_remote_user_mirror.cpp explicitly catches a regression. |
| T-RCM-06 | S (spoofing) | counter source | accept | Counters are per-NJClient instance; no cross-instance read; relaxed loads cannot be spoofed except by direct memory tampering, which is out of scope for this threat model (RT-audio plugin trust boundary). |
</threat_model>

<verification>
**Code-level (per-task):**
- Task 1: `grep` checks for new struct, accessor decl, accessor defn; build green.
- Task 2: `grep` checks for two try_push sites, one apply branch, three+ counter fetch_add bumps; build green.
- Task 3: Unit + concurrency + bounds + ordering tests green under Release AND TSan; build_number.h bumped; specialist reviewer accepted.

**Plan-level falsifiability (the whole point of the diagnostic counters):**
After the build lands and the user runs UAT against a NINJAM server, the user can falsify or confirm the symptom-to-defect link in a single cycle:
1. Set a debug printout (or lldb breakpoint) on `GetChannelInfoPublishCount(slot, ch)` and `GetChannelInfoApplyCount(slot, ch)` for an active remote peer.
2. Mutate the canonical state (mute/unmute, change vol/pan, change output channel, or — for instamode — let the peer broadcast in instamode mode).
3. Both counters MUST increment per mutation. Publish > 0 with apply == 0 means the audio thread isn't draining (orthogonal bug). Both at 0 after a clear mutation means the publisher path missed a site (regression). Both >0 means the data flow is restored.
4. With instamode peer, pre-fix audio cuts out after 1-2 intervals; post-fix audio remains continuous AND `GetChannelInfoApplyCount` reflects the publisher activity.

**Out-of-scope verification (NOT in this plan's done criteria):**
- UAT in JamWide host (separate user-driven step).
- The three secondary defects called out in spec (3313-3325 silence-marker, 1906-1916 generation-gate spinwait, sessionmode-flags-wrong) — recorded as Future Work, not implemented.
</verification>

<success_criteria>
1. spsc_payloads.h carries PeerChannelInfoUpdate, the deviation block, and an updated FINAL marker (the original "FINAL per Codex M-9" annotation is rewritten to mark the supersession, not left lying as stale).
2. njclient.h carries `m_chinfo_publishes_observed[MAX_PEERS][MAX_USER_CHANNELS]` and `m_chinfo_applies_observed[MAX_PEERS][MAX_USER_CHANNELS]`, plus `GetChannelInfoPublishCount` and `GetChannelInfoApplyCount` public noexcept accessors.
3. njclient.cpp publishes PeerChannelInfoUpdate from BOTH user-info-change-notify (after m_users_cs.Leave, before existing PeerChannelMaskUpdate publish) AND SetUserChannelState (before existing publish_mask publish), with overflow handling matching the existing pattern, and bumps m_chinfo_publishes_observed only on successful try_push.
4. njclient.cpp's drainRemoteUserUpdates apply visitor copies all four fields onto the mirror with bounds checks matching PeerCodecSwapUpdate's shape, and bumps m_chinfo_applies_observed.
5. Ordering invariant: PeerChannelInfoUpdate published BEFORE PeerNextDsUpdate for the same channel — verified by grep audit + test 4.
6. tests/test_remote_user_mirror.cpp's four new tests (roundtrip, 1000-cycle concurrent, bounds, ordering) pass under Release AND TSan with zero ThreadSanitizer reports. All previously-existing tests still pass.
7. The TSan run is part of Task 3's `<verify><automated>` command — it is not a soft prose-only step. The automated command chains Release build+test followed by TSan build+test and fails the task if either reports a FAIL or any ThreadSanitizer warning.
8. src/build_number.h bumped to >= 292.
9. realtime-audio-reviewer specialist has accepted the diff (recorded in commit body).
10. Three deferred secondary defects recorded inline in this PLAN's "Out-of-scope future work" so they aren't lost.

UAT in JamWide host is explicitly NOT in done criteria — that is the user's separate step.
</success_criteria>

<output>
After completion, create `.planning/quick/260502-rcm-fix-orphan-mirror-fields/SUMMARY.md` capturing:
- The two publisher sites and one apply branch added (file:line references).
- The accessor signatures and how to read counters from lldb / debug UI.
- Evidence the four new tests passed under Release and TSan (paste tail of test output).
- The realtime-audio-reviewer accept verbatim.
- Deferred items: (i) silence-marker double-free, (ii) generation-gate spinwait leak, (iii) sessionmode-flags filter mismatch.
- A one-line UAT crib for the user: "Connect to NINJAM server, observe instamode peer audio is continuous; lldb p ((NJClient*)x)->GetChannelInfoApplyCount(slot, ch) increments on mute/vol/pan change."


---

## Iteration 2 amendments (2026-05-02)

This plan was returned BLOCKED by gsd-plan-checker after iteration 1 with two concrete defects. The amendments below patch the existing tasks in place; no scope was broadened, no new tasks were added, no Future Work item was promoted.

**Amendment 1 — BLOCKER 1: Publish ordering at user-info-change-notify site (Task 2a).**
- Iteration-1 prose said "BEFORE the existing `if (publish_added && user_slot >= 0)` block at line 1872". This would have landed per-channel attributes into a slot whose `active=false` was still set, contradicting the documented 1863-1871 ordering invariant ("PeerAddedUpdate first (slot gets active=true), then PeerChannelMaskUpdate").
- Patched: Task 2a now specifies "AFTER the existing `if (publish_added && user_slot >= 0) { ... }` block closes (line 1880) and BEFORE the `if (publish_mask_change && user_slot >= 0)` block opens (line 1881)". The inline code comment was rewritten to encode BOTH ordering constraints (after-PeerAddedUpdate AND before-PeerChannelMaskUpdate, with the cross-publisher PeerNextDsUpdate FIFO note carried forward).
- `must_haves.truths` entry #4 was rewritten to encode both constraints explicitly.
- Independent grep verification of njclient.cpp performed (see Pattern Mapping note below). Lines 1872 / 1880 / 1881 MATCH the prompt — no drift observed.

**Amendment 2 — BLOCKER 2: TSan not enforced in Task 3 automated verify.**
- Iteration-1 plan described TSan in Task 3 action 3c (prose) but the `<verify><automated>` line only built Release in `build-juce/`. An executor running the literal verify command could declare success without TSan green. Prior 15.1 cycles burned multiple UAT loops on bugs TSan would have caught.
- Patched: Task 3 `<verify><automated>` now chains: build-juce build+test → grep four new test names → grep no failure markers → grep build_number >= 292 → cd build-tsan → guard CMakeCache.txt presence → `cmake --build . --target test_remote_user_mirror` → run TSan binary → grep `WARNING: ThreadSanitizer|FAILED:|FAIL:|assertion failed|SEGFAULT` is empty. Failure at any step fails the task.
- Test reporting convention verified against tests/test_remote_user_mirror.cpp: tests print `FAILED: <msg>` (capitalized); grep pattern `FAILED:|FAIL:|assertion failed|SEGFAULT` covers both the project's PASS/FAIL macros and TSan summary lines.
- Build-tsan layout verified: binary at `build-tsan/test_remote_user_mirror` (root, not under `tests/`); verify command tries both paths to be robust.
- Task 3 action 3c was rewritten to make the cmake configure fallback explicit (one-line cmake invocation if CMakeCache.txt is ever absent) and to call out that TSan is part of the AUTOMATED verify, not a soft prose-only step.
- `<success_criteria>` item 7 (new) added: "The TSan run is part of Task 3's automated verify command, not a soft prose-only step." Subsequent items renumbered (8 / 9 / 10).

**Pattern Mapping (independent grep verification, 2026-05-02):**
```
$ grep -n "publish_added\|publish_mask_change\|PeerChannelMaskUpdate" src/core/njclient.cpp | head
1709:                  bool        publish_added = false;
1711:                  bool        publish_mask_change = true;
1738:                      publish_added = (user_slot >= 0);
1872:                  if (publish_added && user_slot >= 0)
1881:                  if (publish_mask_change && user_slot >= 0)
$ sed -n '1880p;1881p' src/core/njclient.cpp
                  }                                          # ← line 1880, closing brace of publish_added block
                  if (publish_mask_change && user_slot >= 0) # ← line 1881, opening of publish_mask_change block
```
Confirmed: line 1872 opens `publish_added` block, line 1880 closes it, line 1881 opens `publish_mask_change`. Insertion target (between 1880 `}` and 1881 `if`) is correct.

</output>
