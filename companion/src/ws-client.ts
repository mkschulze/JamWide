// ── WebSocket Client (D-11, D-15) ──
// Connects to plugin's local WebSocket server.
// No auto-reconnect per D-15 -- manual reconnect button only.

import { isConfigMessage, isRosterMessage, isBufferDelayMessage, isPopoutMessage, isBeatHeartbeatMessage, isDeactivateMessage } from './types';
import type { ConfigMessage, RosterMessage, BufferDelayMessage, PopoutMessage, BeatHeartbeatMessage, DeactivateMessage } from './types';

export type WsCallbacks = {
  onConfig: (msg: ConfigMessage) => void;
  onRoster: (msg: RosterMessage) => void;
  onBufferDelay: (msg: BufferDelayMessage) => void;
  onPopout: (msg: PopoutMessage) => void;
  onBeatHeartbeat: (msg: BeatHeartbeatMessage) => void;
  onDeactivate: (msg: DeactivateMessage) => void;
  onStatusChange: (connected: boolean) => void;
};

let currentWs: WebSocket | null = null;

export function connectToPlugin(port: number, callbacks: WsCallbacks): WebSocket {
  // Close existing connection if any (prevents duplicate connections)
  if (currentWs && currentWs.readyState <= WebSocket.OPEN) {
    currentWs.close();
  }

  // CRITICAL: Use 127.0.0.1, NOT localhost (mixed content blocking -- Research pitfall 1)
  const ws = new WebSocket(`ws://127.0.0.1:${port}`);
  currentWs = ws;

  ws.onopen = () => {
    callbacks.onStatusChange(true);
  };

  ws.onmessage = (event: MessageEvent) => {
    // Validate all incoming messages (review concern #6: protocol resilience)
    let parsed: unknown;
    try {
      parsed = JSON.parse(event.data as string);
    } catch {
      console.warn('VideoCompanion: received non-JSON message, ignoring');
      return;
    }

    // Type-checked dispatch with validation
    if (isConfigMessage(parsed)) {
      callbacks.onConfig(parsed);
    } else if (isRosterMessage(parsed)) {
      callbacks.onRoster(parsed);
    } else if (isBufferDelayMessage(parsed)) {
      callbacks.onBufferDelay(parsed);
    } else if (isPopoutMessage(parsed)) {
      callbacks.onPopout(parsed);
    } else if (isBeatHeartbeatMessage(parsed)) {
      callbacks.onBeatHeartbeat(parsed);
    } else if (isDeactivateMessage(parsed)) {
      callbacks.onDeactivate(parsed);
    } else {
      console.warn('VideoCompanion: unknown message type, ignoring:', parsed);
    }
  };

  ws.onclose = () => {
    callbacks.onStatusChange(false);
  };

  ws.onerror = () => {
    // onclose will also fire after onerror, so status change is handled there
  };

  return ws;
}
