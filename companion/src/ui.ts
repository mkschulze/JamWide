// ── DOM Manipulation & UI State ──
// Matches the UI-SPEC layout: header badge, footer status, VDO.Ninja iframe,
// empty state, pre-connection state.

import type { RosterUser } from './types';

// ── Helper: safe element query ──

function el<T extends HTMLElement>(id: string): T {
  const element = document.getElementById(id);
  if (!element) throw new Error(`Element #${id} not found`);
  return element as T;
}

/** Create a centered paragraph with optional CSS class. Uses textContent (XSS-safe). */
function makeCenterText(text: string, extraClass?: string): HTMLParagraphElement {
  const p = document.createElement('p');
  p.className = extraClass ? `center-text ${extraClass}` : 'center-text';
  p.textContent = text;
  return p;
}

/** Remove all children from an element. */
function clearChildren(parent: HTMLElement): void {
  while (parent.firstChild) {
    parent.removeChild(parent.firstChild);
  }
}

// ── Header Badge ──

export function updateHeaderBadge(connected: boolean): void {
  const badge = el('status-badge');
  if (connected) {
    badge.textContent = 'Connected';
    badge.classList.add('badge-connected');
    badge.classList.remove('badge-disconnected');
  } else {
    badge.textContent = 'Disconnected';
    badge.classList.add('badge-disconnected');
    badge.classList.remove('badge-connected');
  }
}

// ── Footer Status ──
// Tracks whether the plugin explicitly said goodbye (deactivate message)
// vs. a transient WebSocket drop. After deactivate the plugin's WS server
// is gone, so the Reconnect button is useless and we hide it.
let deactivated = false;

export function markDeactivated(): void {
  deactivated = true;
}

export function updateFooterStatus(connected: boolean): void {
  const dot = el('ws-dot');
  const text = el('ws-status-text');
  const btn = el<HTMLButtonElement>('reconnect-btn');

  if (connected) {
    // Reset deactivated flag on successful reconnect
    deactivated = false;
    dot.classList.add('dot-connected');
    dot.classList.remove('dot-disconnected');
    text.textContent = 'Connected to plugin';
    btn.style.display = 'none';
  } else {
    dot.classList.add('dot-disconnected');
    dot.classList.remove('dot-connected');
    if (deactivated) {
      text.textContent = 'Video deactivated — click Video in JamWide to reopen';
      btn.style.display = 'none';
    } else {
      text.textContent = 'Disconnected';
      btn.style.display = 'inline-block';
    }
  }
}

// ── Session Info ──

export function setSessionInfo(roomId: string): void {
  const info = el('session-info');
  info.textContent = `Room: ${roomId}`;
}

// ── Background Effects ──
// VDO.Ninja supports these via URL params:
//   &effects=3 = blur background
//   &effects=5 = virtual green screen (VDO.Ninja provides its own image picker)
// Custom image injection via URL is not supported by VDO.Ninja.

export type BgEffect = 'none' | 'blur' | 'virtual-bg';

const EFFECT_STORAGE_KEY = 'jamwide-bg-effect';

export function getSavedEffect(): BgEffect {
  return (localStorage.getItem(EFFECT_STORAGE_KEY) as BgEffect) || 'none';
}

export function saveEffect(effect: BgEffect): void {
  localStorage.setItem(EFFECT_STORAGE_KEY, effect);
}

// -- Bandwidth Profiles (D-12, D-14) --
// CRITICAL: VDO.Ninja quality numbering is INVERTED (Research Pitfall 1)
//   quality=0 = 1080p60 (highest)
//   quality=1 = 720p60  (middle)
//   quality=2 = 360p30  (lowest)

export type BandwidthProfile = 'low' | 'balanced' | 'high';

const VALID_BANDWIDTH_PROFILES: ReadonlySet<string> = new Set(['low', 'balanced', 'high']);

export const BANDWIDTH_PROFILES = {
  low:      { quality: 2, maxvideobitrate: 500  },  // 360p30, 500kbps
  balanced: { quality: 1, maxvideobitrate: 1500 },  // 720p60, 1.5Mbps
  high:     { quality: 0, maxvideobitrate: 3000 },  // 1080p60, 3Mbps
} as const;

const BANDWIDTH_STORAGE_KEY = 'jamwide-bandwidth-profile';

/** Read saved bandwidth profile from localStorage.
 *  Addresses review concern R-MEDIUM-07: defensive handling for invalid stored values.
 *  Falls back to 'balanced' if stored value is missing, empty, or not a valid profile name. */
export function getSavedBandwidthProfile(): BandwidthProfile {
  const stored = localStorage.getItem(BANDWIDTH_STORAGE_KEY);
  if (stored && VALID_BANDWIDTH_PROFILES.has(stored)) {
    return stored as BandwidthProfile;
  }
  return 'balanced';
}

export function saveBandwidthProfile(profile: BandwidthProfile): void {
  localStorage.setItem(BANDWIDTH_STORAGE_KEY, profile);
}

function effectToParams(effect: BgEffect): string {
  if (effect === 'blur') return '&effects=3';
  if (effect === 'virtual-bg') return '&effects=5';
  return '';
}

// ── Sync Indicator (Beat Heartbeat) ──

export function updateSyncIndicator(beat: number, bpi: number, interval: number): void {
  const container = document.getElementById('sync-indicator');
  const dot = document.getElementById('sync-beat-dot');
  const label = document.getElementById('sync-beat-label');
  if (!container || !dot || !label) return;

  container.style.display = 'inline-flex';
  label.textContent = `${beat + 1}/${bpi} #${interval}`;

  // Remove previous flash classes before adding new one
  dot.classList.remove('beat-flash', 'beat-flash-downbeat');
  // Force reflow to restart animation
  void dot.offsetWidth;

  if (beat === 0) {
    dot.classList.add('beat-flash-downbeat');
  } else {
    dot.classList.add('beat-flash');
  }
}

export function hideSyncIndicator(): void {
  const container = document.getElementById('sync-indicator');
  if (container) container.style.display = 'none';
}

// ── VDO.Ninja URL Builder ──

export function buildVdoNinjaUrl(
  room: string,
  push: string,
  effect?: BgEffect,
  bandwidthProfile?: BandwidthProfile,
  hashFragment?: string,
  viewStreamId?: string
): string {
  const profile = bandwidthProfile ?? getSavedBandwidthProfile();
  const bw = BANDWIDTH_PROFILES[profile];
  const fx = effectToParams(effect ?? getSavedEffect());

  // Base URL with chunked mode (Pitfall 2: required for setBufferDelay > 4s)
  let url = `https://vdo.ninja/?room=${encodeURIComponent(room)}`;

  // Phase 13 (VID-07): When viewStreamId is set, this is a view-only popout window.
  // Omit &push= (no outbound camera/audio) and add &view= to filter to one stream.
  if (viewStreamId) {
    url += `&view=${encodeURIComponent(viewStreamId)}`;
  } else {
    url += `&push=${encodeURIComponent(push)}`;
  }

  url += '&noaudio&cleanoutput&ad=0'
    + '&chunked'
    + '&chunkbufferadaptive=0'
    + '&chunkbufferceil=180000'
    + `&quality=${bw.quality}`
    + `&maxvideobitrate=${bw.maxvideobitrate}`
    + fx;

  // Include cached buffer delay in URL so VDO.Ninja applies it at page init,
  // before its JS postMessage handler is ready. This fixes the race condition
  // where setBufferDelay via postMessage arrives before VDO.Ninja initializes.
  // Subsequent BPM/BPI changes still update via postMessage.
  if (cachedBufferDelay !== null && cachedBufferDelay > 0) {
    url += `&buffer=${cachedBufferDelay}`;
  }

  // D-05: Room derived password forwarded as VDO.Ninja password param in iframe URL
  // (Addresses review concern R-MEDIUM-08: uses "password" terminology consistently)
  if (hashFragment)
    url += `&password=${encodeURIComponent(hashFragment)}`;

  return url;
}

// -- Buffer Delay Relay (D-01, D-04) --

let cachedBufferDelay: number | null = null;

/** Apply buffer delay by caching it and forwarding to iframe via postMessage.
 *  Addresses review concern R-MEDIUM-06: postMessage is only sent if iframe exists
 *  and has a contentWindow. If iframe is not yet loaded, the value is cached and
 *  re-applied when the iframe fires its load event (see reapplyCachedBufferDelay). */
export function applyBufferDelay(delayMs: number): void {
  cachedBufferDelay = delayMs;
  sendBufferDelayToIframe(delayMs);
}

function sendBufferDelayToIframe(delayMs: number): void {
  const iframe = document.querySelector('#main-area iframe') as HTMLIFrameElement | null;
  if (!iframe?.contentWindow) return;
  iframe.contentWindow.postMessage({ setBufferDelay: delayMs }, '*');
}

/** Call after iframe load event to re-apply cached buffer delay (Pitfall 4).
 *  Addresses review concern R-HIGH-03: state preservation across iframe reloads.
 *  After bandwidth profile change triggers iframe reload, the cached delay value
 *  is re-sent to the new iframe once its load event fires. */
export function reapplyCachedBufferDelay(): void {
  if (cachedBufferDelay !== null) {
    sendBufferDelayToIframe(cachedBufferDelay);
  }
}

/** Exported for testing */
export function getCachedBufferDelay(): number | null {
  return cachedBufferDelay;
}

/** Reset cached delay (for testing) */
export function resetCachedBufferDelay(): void {
  cachedBufferDelay = null;
}

// -- Roster Label Strip (D-09, D-10) --
// Addresses review concern R-HIGH-02: roster lifecycle.
// Each call to renderRosterStrip is a FULL STATE REPLACEMENT per the protocol contract.
// The function clears all existing pills and rebuilds from the new users array.
// This handles: users joining, users leaving, username changes, and reordering.

// Known bot username prefixes that should be hidden from the roster strip.
// These are server-side bots on public NINJAM servers that don't have video streams.
// Prefix match handles variants like "ninbot_", "ninbot2", "Jambot_server".
const BOT_PREFIXES = ['ninbot', 'jambot', 'ninjam'];

/** Returns true if the username starts with a known bot prefix (case-insensitive). */
export function isBotUser(name: string): boolean {
  const lower = name.toLowerCase();
  return BOT_PREFIXES.some(prefix => lower.startsWith(prefix));
}

export function renderRosterStrip(users: RosterUser[]): void {
  const strip = document.getElementById('roster-strip');
  if (!strip) return;

  // Filter out known bot users before rendering
  const visibleUsers = users.filter(u => !isBotUser(u.name));

  // Clear ALL existing pills -- full state replacement, not incremental update
  while (strip.firstChild) {
    strip.removeChild(strip.firstChild);
  }

  if (visibleUsers.length === 0) {
    strip.style.display = 'none';
    return;
  }

  strip.style.display = 'flex';

  visibleUsers.forEach(user => {
    const pill = document.createElement('button');
    pill.className = 'roster-pill';
    // T-12-02 mitigation: textContent is XSS-safe (never use innerHTML for user names)
    pill.textContent = user.name;
    pill.dataset.streamId = user.streamId;
    pill.title = `Pop out ${user.name} video`;
    strip.appendChild(pill);
  });
}

// ── VDO.Ninja Iframe ──

/** Load VDO.Ninja iframe with current settings.
 *  Addresses review concern R-HIGH-03: state preservation across reloads.
 *  - hashFragment is parsed once from URL on page load and passed to every call
 *  - bandwidth profile is read from localStorage by buildVdoNinjaUrl
 *  - cached buffer delay is re-applied via reapplyCachedBufferDelay on iframe load event
 *  - roster strip DOM node is preserved across iframe reloads */
export function loadVdoNinjaIframe(
  room: string,
  push: string,
  hashFragment?: string
): void {
  const main = el('main-area');

  // Preserve roster strip if it exists
  const existingStrip = document.getElementById('roster-strip');

  clearChildren(main);

  const url = buildVdoNinjaUrl(room, push, undefined, undefined, hashFragment);
  const iframe = document.createElement('iframe');
  iframe.src = url;
  iframe.title = 'VDO.Ninja video grid';
  iframe.allow = 'camera;microphone;display-capture';
  iframe.style.width = '100%';
  iframe.style.height = '100%';
  iframe.style.border = 'none';

  // Re-apply cached buffer delay after iframe loads. The load event fires when the
  // iframe HTML is parsed, but VDO.Ninja's internal JS (chunked buffer system) needs
  // additional time to initialize its postMessage handler. Retry with increasing delays
  // to ensure the buffer delay is applied. The &buffer= URL param handles the initial
  // value; these retries catch dynamic updates (BPM/BPI change during iframe load).
  iframe.addEventListener('load', () => {
    reapplyCachedBufferDelay();
    setTimeout(reapplyCachedBufferDelay, 1000);
    setTimeout(reapplyCachedBufferDelay, 3000);
  });

  main.appendChild(iframe);

  // Re-add roster strip (it was removed by clearChildren)
  if (existingStrip) {
    main.appendChild(existingStrip);
  } else {
    const strip = document.createElement('div');
    strip.id = 'roster-strip';
    strip.setAttribute('role', 'status');
    strip.setAttribute('aria-label', 'Connected participants');
    strip.style.display = 'none';
    main.appendChild(strip);
  }
}

// ── Pre-Connection State (review concern #9: first-load timing) ──

export function showPreConnectionState(): void {
  const main = el('main-area');
  clearChildren(main);
  main.appendChild(makeCenterText('Connecting to JamWide plugin...'));
}

// ── Connection Lost ──

export function showConnectionLost(): void {
  const main = el('main-area');
  // If iframe is already loaded, keep it visible (VDO.Ninja continues working
  // even if plugin WS drops). Only update the footer.
  if (main.querySelector('iframe')) {
    return;
  }
  clearChildren(main);
  const msg = deactivated
    ? 'Video deactivated. Click Video in JamWide to reopen.'
    : 'Connection lost. Click Reconnect to Plugin or re-click Video in JamWide.';
  main.appendChild(makeCenterText(msg));
}

// ── Waiting for Video ──

export function showWaitingForVideo(): void {
  const main = el('main-area');
  clearChildren(main);
  main.appendChild(makeCenterText('Waiting for video...', 'pulse'));
}

// ── Empty Room State (review concern #11: empty-state behavior) ──

export function showEmptyRoom(): void {
  const main = el('main-area');
  clearChildren(main);
  main.appendChild(
    makeCenterText('No participants yet. Waiting for others to join...')
  );
}
