// ── JamWide Video Companion — App Entry ──
// Connects to the plugin's local WebSocket, receives config and roster,
// and renders a VDO.Ninja iframe with audio suppressed.

import { connectToPlugin } from './ws-client';
import type { WsCallbacks } from './ws-client';
import type { RosterUser } from './types';
import {
  showPreConnectionState,
  updateHeaderBadge,
  updateFooterStatus,
  setSessionInfo,
  loadVdoNinjaIframe,
  showConnectionLost,
  markDeactivated,
  showEmptyRoom,
  updateSyncIndicator,
  hideSyncIndicator,
  getSavedEffect,
  saveEffect,
  setLastAutoDelay,
  showSyncingOverlay,
  initDelayControls,
  updateFooterDelayStatus,
  renderRosterStrip,
  getSavedBandwidthProfile,
  saveBandwidthProfile,
  getActiveDelayMs,
  getLastAutoSyncMode,
} from './ui';
import type { BgEffect, BandwidthProfile } from './ui';

// ── Parse URL query params ──
const params = new URLSearchParams(window.location.search);
// WR-02 fix: Validate parseInt result to avoid NaN port (e.g. ?wsPort=abc)
const rawPort = parseInt(params.get('wsPort') || '7170', 10);
const port = Number.isFinite(rawPort) && rawPort > 0 && rawPort < 65536
  ? rawPort
  : 7170;

// -- Parse hash fragment for room security (D-05) --
// Parsed once on page load and stored in module-level variable.
// Addresses review concern R-HIGH-03: state preservation across iframe reloads.
// This value is reused for every loadVdoNinjaIframe call (initial load, bandwidth
// change, effect change) without re-parsing.
const hashParams = new URLSearchParams(window.location.hash.substring(1));
const roomHashFragment = hashParams.get('password') || '';

// ── State tracking ──
let configReceived = false;
let currentRoom = '';
let currentPush = '';

// -- Popout Window Tracking (D-04, D-05, D-07) --
// Addresses Codex review HIGH concern: window lifecycle cleanup.
// Map tracks open popout windows by stream ID.
// Stale entries (manually closed windows) are cleaned on every roster update
// AND on a periodic 2-second sweep timer (addresses the review concern that
// "there is no explicit cleanup when a user manually closes a popout until
// some later event happens").
const popoutWindows = new Map<string, Window>();
let lastRosterUsers: RosterUser[] = [];

// ── Show initial pre-connection state (review concern #9: first-load timing) ──
showPreConnectionState();

// -- Popout Window Management Functions --

function openPopout(streamId: string, username: string): void {
  // D-05: Focus existing window instead of opening duplicate
  const existing = popoutWindows.get(streamId);
  if (existing && !existing.closed) {
    existing.focus();
    return;
  }

  // Clean stale entry if window was manually closed
  if (existing) {
    popoutWindows.delete(streamId);
  }

  // Build popout URL with current session params
  const popoutParams = new URLSearchParams({
    room: currentRoom,
    push: currentPush,
    view: streamId,
    name: username,
  });
  if (roomHashFragment) popoutParams.set('password', roomHashFragment);
  popoutParams.set('quality', getSavedBandwidthProfile());
  if (new URLSearchParams(window.location.search).get('vdoProduction') === '1') {
    popoutParams.set('vdoProduction', '1');
  }
  const activeDelay = getActiveDelayMs();
  if (activeDelay !== null && activeDelay > 0) {
    popoutParams.set('buffer', String(activeDelay));
    popoutParams.set('syncMode', getLastAutoSyncMode());
  }

  const url = `popout.html?${popoutParams.toString()}`;
  const windowName = `jamwide-popout-${streamId}`;
  const features = 'width=640,height=480,toolbar=no,menubar=no,resizable=yes';

  const win = window.open(url, windowName, features);

  if (!win) {
    showPopupBlockedBanner();
    return;
  }

  popoutWindows.set(streamId, win);
  updatePillIndicators();
}

function showPopupBlockedBanner(): void {
  const existing = document.getElementById('popup-blocked-banner');
  if (existing) return;

  const banner = document.createElement('div');
  banner.id = 'popup-blocked-banner';
  banner.setAttribute('role', 'alert');
  banner.textContent = 'Popup blocked \u2014 allow popups for this site to use video popout';
  banner.addEventListener('click', () => banner.remove());

  const mainArea = document.getElementById('main-area');
  if (mainArea) {
    mainArea.style.position = 'relative';
    mainArea.appendChild(banner);
  }

  setTimeout(() => banner.remove(), 5000);
}

// D-07: Relay roster to popouts via postMessage. Also performs lifecycle cleanup
// for manually closed popout windows (addresses review HIGH concern #2).
function notifyPopouts(users: RosterUser[]): void {
  for (const [streamId, win] of popoutWindows.entries()) {
    // Addresses Codex review HIGH concern: cleanup manually closed popout windows.
    // Check window.closed on every roster update. If the user closed the popout
    // via the browser close button, the Map entry is stale. Remove it and update
    // pill indicators so the green border disappears.
    if (win.closed) {
      popoutWindows.delete(streamId);
      continue;
    }
    // D-07: Relay roster to popout via postMessage.
    // Addresses Codex Round 2 MEDIUM: use window.location.origin instead of '*'
    // as targetOrigin. Both pages are same-origin (same GitHub Pages deployment
    // or same localhost dev server), so origin is always correct. The popout also
    // validates event.source === window.opener for defense in depth (see popout.ts).
    win.postMessage({ type: 'roster', users }, window.location.origin);
  }
  updatePillIndicators();
}

function notifyPopoutDelay(delayMs: number, syncMode?: 'measured' | 'calculated'): void {
  for (const [streamId, win] of popoutWindows.entries()) {
    if (win.closed) {
      popoutWindows.delete(streamId);
      continue;
    }
    win.postMessage({ type: 'bufferDelay', delayMs, syncMode }, window.location.origin);
  }
  updatePillIndicators();
}

// Periodic sweep: clean up stale popout window references every 2 seconds.
// This catches windows the user manually closes between roster updates.
// Addresses Codex review HIGH concern: "no explicit cleanup when a user manually
// closes a popout until some later event happens."
setInterval(() => {
  let cleaned = false;
  for (const [streamId, win] of popoutWindows.entries()) {
    if (win.closed) {
      popoutWindows.delete(streamId);
      cleaned = true;
    }
  }
  if (cleaned) updatePillIndicators();
}, 2000);

// UI-SPEC: 2px green left border on pills with open popout windows
function updatePillIndicators(): void {
  const pills = document.querySelectorAll('.roster-pill');
  pills.forEach(pill => {
    const btn = pill as HTMLButtonElement;
    const sid = btn.dataset.streamId ?? '';
    const win = popoutWindows.get(sid);
    const isOpen = win && !win.closed;
    btn.classList.toggle('popout-active', !!isOpen);
  });
}

// D-13: Deactivate handler -- closes all popout windows and clears tracking state
function handleDeactivate(): void {
  for (const [, win] of popoutWindows.entries()) {
    if (!win.closed) win.close();
  }
  popoutWindows.clear();
  updatePillIndicators();
  // Mark as explicitly deactivated BEFORE the WS onclose fires.
  // updateFooterStatus() checks this flag to hide the useless Reconnect
  // button — the plugin's WS server is gone after deactivate() and the
  // only way back is to re-click Video in JamWide.
  markDeactivated();
  showConnectionLost();
}

// ── WebSocket callbacks ──
const callbacks: WsCallbacks = {
  onConfig(msg) {
    configReceived = true;
    currentRoom = msg.room;
    currentPush = msg.push;
    setSessionInfo(msg.room);
    loadVdoNinjaIframe(msg.room, msg.push, roomHashFragment);
    updateHeaderBadge(true);
  },

  onRoster(msg) {
    if (!configReceived) {
      console.log('VideoCompanion: roster received before config, skipping');
      return;
    }

    // VID-10: Render roster name labels in strip.
    // Addresses review concern R-HIGH-02: roster lifecycle.
    // Each roster message is a full state replacement (protocol contract).
    // renderRosterStrip clears all previous pills and rebuilds from msg.users.
    renderRosterStrip(msg.users);

    // Phase 13: Store roster and attach click handlers for popout
    lastRosterUsers = msg.users;

    // Attach click handlers to roster pills for popout (D-01)
    const pills = document.querySelectorAll<HTMLButtonElement>('.roster-pill');
    pills.forEach(pill => {
      pill.addEventListener('click', () => {
        const streamId = pill.dataset.streamId;
        const username = pill.textContent ?? '';
        if (streamId) openPopout(streamId, username);
      });
    });

    // D-07: Relay roster update to all open popout windows (also cleans stale entries)
    notifyPopouts(msg.users);

    // Show empty room state only if no iframe is loaded
    if (msg.users.length === 0) {
      const mainArea = document.getElementById('main-area');
      if (mainArea && !mainArea.querySelector('iframe')) {
        showEmptyRoom();
      }
    }
  },

  onBufferDelay(msg) {
    console.log('VideoSync: plugin sent bufferDelay:', msg.delayMs, 'ms',
      msg.syncMode ? `(${msg.syncMode})` : '');
    setLastAutoDelay(msg.delayMs, msg.syncMode);
    notifyPopoutDelay(msg.delayMs, msg.syncMode);
    updateFooterDelayStatus();
    // D-09: show sync overlay when measured delay arrives.
    // Only for 'measured' mode -- calculated mode doesn't need a visible sync phase
    // because it's a theoretical estimate, not a probe-triggered calibration.
    if (msg.syncMode === 'measured' && msg.delayMs > 0) {
      showSyncingOverlay(msg.delayMs);
    }
  },

  onBeatHeartbeat(msg) {
    updateSyncIndicator(msg.beat, msg.bpi, msg.interval);
  },

  onPopout(msg) {
    // OSC-triggered popout -- same logic as pill click.
    // Addresses review MEDIUM concern: requestPopout broadcasts to ALL connected
    // companion clients. If multiple companion pages are open, all of them will
    // react and open popouts. This is acceptable behavior -- each companion page
    // manages its own popout windows independently.
    const user = lastRosterUsers.find(u => u.streamId === msg.streamId);
    const username = user?.name ?? msg.streamId;
    openPopout(msg.streamId, username);
  },

  onDeactivate() {
    handleDeactivate();
  },

  onStatusChange(connected) {
    updateHeaderBadge(connected);
    updateFooterStatus(connected);
    if (!connected) {
      hideSyncIndicator();
      showConnectionLost();
    }
  },
};

// ── Connect to plugin ──
connectToPlugin(port, callbacks);

// ── Wire delay slider and auto button (Phase 12.1) ──
initDelayControls();

// ── Wire reconnect button ──
const reconnectBtn = document.getElementById('reconnect-btn');
if (reconnectBtn) {
  reconnectBtn.addEventListener('click', () => {
    connectToPlugin(port, callbacks);
  });
}

// ── Wire background effect dropdown ──
const bgSelect = document.getElementById('bg-effect') as HTMLSelectElement | null;
if (bgSelect) {
  bgSelect.value = getSavedEffect();
  bgSelect.addEventListener('change', () => {
    const effect = bgSelect.value as BgEffect;
    saveEffect(effect);
    if (configReceived && currentRoom) {
      loadVdoNinjaIframe(currentRoom, currentPush, roomHashFragment);
    }
  });
}

// -- Wire bandwidth profile dropdown (D-12, D-13, D-15) --
const bwSelect = document.getElementById('bandwidth-profile') as HTMLSelectElement | null;
if (bwSelect) {
  // Addresses review concern R-MEDIUM-07: set dropdown to saved value (validated by getSavedBandwidthProfile)
  bwSelect.value = getSavedBandwidthProfile();
  bwSelect.addEventListener('change', () => {
    const profile = bwSelect.value as BandwidthProfile;
    saveBandwidthProfile(profile);
    // D-15: Changing profile requires iframe reload.
    // Addresses review concern R-HIGH-03: state preservation.
    // loadVdoNinjaIframe rebuilds URL with current hash fragment and bandwidth profile,
    // and the iframe load event handler re-applies cached buffer delay.
    if (configReceived && currentRoom) {
      loadVdoNinjaIframe(currentRoom, currentPush, roomHashFragment);
    }
  });
}
