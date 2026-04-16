// ── WebSocket Message Types (D-13, D-14) ──

export interface ConfigMessage {
  type: 'config';
  room: string;
  push: string;
  noaudio: boolean;
  wsPort: number;
}

export interface RosterUser {
  idx: number;
  name: string;
  streamId: string;
}

export interface RosterMessage {
  type: 'roster';
  users: RosterUser[];
}

export interface BufferDelayMessage {
  type: 'bufferDelay';
  delayMs: number;
  syncMode?: 'measured' | 'calculated';
}

export interface PopoutMessage {
  type: 'popout';
  streamId: string;
}

export interface BeatHeartbeatMessage {
  type: 'beatHeartbeat';
  beat: number;
  bpi: number;
  interval: number;
}

export interface DeactivateMessage {
  type: 'deactivate';
}

export type SyncMode = 'measured' | 'calculated' | 'manual';

export type PluginMessage = ConfigMessage | RosterMessage | BufferDelayMessage | PopoutMessage | BeatHeartbeatMessage | DeactivateMessage;

// ── Runtime validation (addresses review: protocol resilience) ──

/** Validate that a parsed object is a valid ConfigMessage */
export function isConfigMessage(msg: unknown): msg is ConfigMessage {
  if (typeof msg !== 'object' || msg === null) return false;
  const m = msg as Record<string, unknown>;
  return (
    m.type === 'config' &&
    typeof m.room === 'string' &&
    typeof m.push === 'string' &&
    typeof m.noaudio === 'boolean' &&
    typeof m.wsPort === 'number'
  );
}

/** Validate that a parsed object is a valid RosterMessage */
export function isRosterMessage(msg: unknown): msg is RosterMessage {
  if (typeof msg !== 'object' || msg === null) return false;
  const m = msg as Record<string, unknown>;
  if (m.type !== 'roster' || !Array.isArray(m.users)) return false;
  // Validate each user entry has required fields
  return (m.users as unknown[]).every(
    (u) =>
      typeof u === 'object' &&
      u !== null &&
      typeof (u as Record<string, unknown>).idx === 'number' &&
      typeof (u as Record<string, unknown>).name === 'string' &&
      typeof (u as Record<string, unknown>).streamId === 'string'
  );
}

/** Validate that a parsed object is a valid BufferDelayMessage */
export function isBufferDelayMessage(msg: unknown): msg is BufferDelayMessage {
  if (typeof msg !== 'object' || msg === null) return false;
  const m = msg as Record<string, unknown>;
  return m.type === 'bufferDelay' && typeof m.delayMs === 'number';
}

/** Validate that a parsed object is a valid PopoutMessage */
export function isPopoutMessage(msg: unknown): msg is PopoutMessage {
  if (typeof msg !== 'object' || msg === null) return false;
  const m = msg as Record<string, unknown>;
  return m.type === 'popout' && typeof m.streamId === 'string';
}

/** Validate that a parsed object is a valid BeatHeartbeatMessage */
export function isBeatHeartbeatMessage(msg: unknown): msg is BeatHeartbeatMessage {
  if (typeof msg !== 'object' || msg === null) return false;
  const m = msg as Record<string, unknown>;
  return (
    m.type === 'beatHeartbeat' &&
    typeof m.beat === 'number' &&
    typeof m.bpi === 'number' &&
    typeof m.interval === 'number'
  );
}

/** Validate that a parsed object is a valid DeactivateMessage */
export function isDeactivateMessage(msg: unknown): msg is DeactivateMessage {
  if (typeof msg !== 'object' || msg === null) return false;
  return (msg as Record<string, unknown>).type === 'deactivate';
}
