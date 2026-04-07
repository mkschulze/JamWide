// ── DOM Manipulation & UI State ──
// Matches the UI-SPEC layout: header badge, footer status, VDO.Ninja iframe,
// empty state, pre-connection state.

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

export function updateFooterStatus(connected: boolean): void {
  const dot = el('ws-dot');
  const text = el('ws-status-text');
  const btn = el<HTMLButtonElement>('reconnect-btn');

  if (connected) {
    dot.classList.add('dot-connected');
    dot.classList.remove('dot-disconnected');
    text.textContent = 'Connected to plugin';
    btn.style.display = 'none';
  } else {
    dot.classList.add('dot-disconnected');
    dot.classList.remove('dot-connected');
    text.textContent = 'Disconnected';
    btn.style.display = 'inline-block';
  }
}

// ── Session Info ──

export function setSessionInfo(roomId: string): void {
  const info = el('session-info');
  info.textContent = `Room: ${roomId}`;
}

// ── Background Effects ──

export type BgEffect = 'none' | 'blur' | 'greenscreen' | 'image';

const EFFECT_STORAGE_KEY = 'jamwide-bg-effect';

export function getSavedEffect(): BgEffect {
  return (localStorage.getItem(EFFECT_STORAGE_KEY) as BgEffect) || 'none';
}

export function saveEffect(effect: BgEffect): void {
  localStorage.setItem(EFFECT_STORAGE_KEY, effect);
}

function effectToParam(effect: BgEffect): string {
  switch (effect) {
    case 'blur': return '&effects=3';
    case 'greenscreen': return '&effects=5';
    case 'image': return '&effects=4';
    default: return '';
  }
}

// ── VDO.Ninja URL Builder ──

export function buildVdoNinjaUrl(room: string, push: string, effect?: BgEffect): string {
  const base = `https://vdo.ninja/?room=${encodeURIComponent(room)}&push=${encodeURIComponent(push)}&noaudio&cleanoutput&webcam`;
  const fx = effectToParam(effect ?? getSavedEffect());
  return base + fx;
}

// ── VDO.Ninja Iframe ──

export function loadVdoNinjaIframe(room: string, push: string): void {
  const main = el('main-area');
  clearChildren(main);

  const url = buildVdoNinjaUrl(room, push);
  const iframe = document.createElement('iframe');
  iframe.src = url;
  iframe.title = 'VDO.Ninja video grid';
  iframe.allow = 'camera;microphone;display-capture';
  iframe.style.width = '100%';
  iframe.style.height = '100%';
  iframe.style.border = 'none';
  main.appendChild(iframe);
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
  main.appendChild(
    makeCenterText('Connection lost. Click Reconnect to Plugin or re-click Video in JamWide.')
  );
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
