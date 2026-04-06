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
} from './ui';

// ── Parse URL query params ──
const params = new URLSearchParams(window.location.search);
const port = parseInt(params.get('wsPort') || '7170', 10);

// ── State tracking ──
let configReceived = false;

// ── Show initial pre-connection state (review concern #9: first-load timing) ──
showPreConnectionState();

// ── WebSocket callbacks ──
const callbacks: WsCallbacks = {
  onConfig(msg) {
    configReceived = true;
    setSessionInfo(msg.room);
    loadVdoNinjaIframe(msg.room, msg.push);
    updateHeaderBadge(true);
  },

  onRoster(msg) {
    // Review concern #6 ordering: if config hasn't arrived yet,
    // roster is harmless -- just skip rendering.
    if (!configReceived) {
      console.log('VideoCompanion: roster received before config, skipping');
      return;
    }

    // Log roster update (Phase 11 foundation; roster rendering is Phase 12 VID-10)
    console.log('Roster update:', msg.users);

    // Show empty room state only if no iframe is loaded
    if (msg.users.length === 0) {
      const mainArea = document.getElementById('main-area');
      if (mainArea && !mainArea.querySelector('iframe')) {
        showEmptyRoom();
      }
    }
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
