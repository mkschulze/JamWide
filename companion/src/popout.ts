// ── JamWide Video Popout — Per-Participant Window (VID-07) ──
// Entry point for popout windows. Reads URL params, builds a solo VDO.Ninja
// iframe URL filtered to one participant via &view=streamId, and listens for
// postMessage roster updates from the main companion page.

import type { RosterUser } from './types';
import { buildVdoNinjaUrl } from './ui';
import type { BandwidthProfile } from './ui';

const params = new URLSearchParams(window.location.search);
const room = params.get('room') ?? '';
const push = params.get('push') ?? '';
const viewStreamId = params.get('view') ?? '';
const username = params.get('name') ?? viewStreamId;
const password = params.get('password') ?? '';
const quality = (params.get('quality') ?? 'balanced') as BandwidthProfile;

document.title = `${username} - JamWide Video`;

const nameLabel = document.getElementById('name-label');
if (nameLabel) nameLabel.textContent = username;

const overlayUsername = document.getElementById('overlay-username');
if (overlayUsername) overlayUsername.textContent = username;

// Build solo VDO.Ninja URL -- view-only (no &push=), filtered to single stream
const url = buildVdoNinjaUrl(room, push, undefined, quality, password, viewStreamId);

const videoArea = document.getElementById('video-area');
if (videoArea) {
  const iframe = document.createElement('iframe');
  iframe.src = url;
  iframe.title = `${username} - Video`;
  iframe.allow = 'autoplay';  // View-only: no camera/mic needed (addresses review MEDIUM concern)
  iframe.style.width = '100%';
  iframe.style.height = '100%';
  iframe.style.border = 'none';
  videoArea.insertBefore(iframe, videoArea.firstChild);
}

// Listen for roster updates from main companion via postMessage (D-07)
// Addresses review HIGH concern: postMessage origin validation.
// Validate event.source === window.opener to ensure messages come from
// the opener (main companion page), not arbitrary windows/iframes.
const overlay = document.getElementById('disconnect-overlay');

window.addEventListener('message', (event: MessageEvent) => {
  // SECURITY: Only accept messages from the opener window (main companion).
  // Addresses Codex review HIGH concern: "postMessage origin validation".
  // event.source identity check is stronger than event.origin for same-origin
  // windows because it validates the exact window instance, not just the domain.
  if (event.source !== window.opener) return;

  if (event.data?.type === 'roster' && Array.isArray(event.data.users)) {
    const users = event.data.users as RosterUser[];
    const present = users.some(u => u.streamId === viewStreamId);
    if (overlay) {
      overlay.style.display = present ? 'none' : 'flex';
    }
  }
});
