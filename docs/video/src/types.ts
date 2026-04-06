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

export type PluginMessage = ConfigMessage | RosterMessage;

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
