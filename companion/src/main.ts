// ── JamWide Video Companion — App Entry ──
// Connects to the plugin's local WebSocket, receives config and roster,
// and renders a VDO.Ninja iframe with audio suppressed.

import { connectToPlugin } from './ws-client';
import type { WsCallbacks } from './ws-client';
import {
  showPreConnectionState,
  updateHeaderBadge,
  updateFooterStatus,
  setSessionInfo,
  loadVdoNinjaIframe,
  showConnectionLost,
  showEmptyRoom,
  getSavedEffect,
  saveEffect,
  applyBufferDelay,
  renderRosterStrip,
  getSavedBandwidthProfile,
  saveBandwidthProfile,
} from './ui';
import type { BgEffect, BandwidthProfile } from './ui';

// ── Parse URL query params ──
const params = new URLSearchParams(window.location.search);
const port = parseInt(params.get('wsPort') || '7170', 10);

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

// ── Show initial pre-connection state (review concern #9: first-load timing) ──
showPreConnectionState();

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

    // Show empty room state only if no iframe is loaded
    if (msg.users.length === 0) {
      const mainArea = document.getElementById('main-area');
      if (mainArea && !mainArea.querySelector('iframe')) {
        showEmptyRoom();
      }
    }
  },

  onBufferDelay(msg) {
    applyBufferDelay(msg.delayMs);
  },

  onStatusChange(connected) {
    updateHeaderBadge(connected);
    updateFooterStatus(connected);
    if (!connected) {
      showConnectionLost();
    }
  },
};

// ── Connect to plugin ──
connectToPlugin(port, callbacks);

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
