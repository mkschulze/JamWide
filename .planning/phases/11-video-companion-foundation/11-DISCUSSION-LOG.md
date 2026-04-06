# Phase 11: Video Companion Foundation - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-06
**Phase:** 11-video-companion-foundation
**Areas discussed:** Video button & plugin UI, Privacy & first-run flow, Companion page design, Room ID & connection flow, WebSocket protocol & messages, GitHub Pages hosting, NINJAM username as stream ID

---

## Video button & plugin UI

| Option | Description | Selected |
|--------|-------------|----------|
| ConnectionBar (top bar) | Next to connect/disconnect controls. Video is session-related. | ✓ |
| SessionInfoStrip (bottom) | Next to BPM/BPI/beat display. | |
| Dedicated footer icon | Small camera icon in footer, like OSC status dot. | |

**User's choice:** ConnectionBar (top bar)

| Option | Description | Selected |
|--------|-------------|----------|
| Camera icon + text label | Icon with 'Video' text. Clear purpose. | |
| Camera icon only | Compact icon, tooltip on hover. | |
| Toggle button with state | Camera icon changes color: grey=off, green=active. | ✓ |

**User's choice:** Toggle button with state

| Option | Description | Selected |
|--------|-------------|----------|
| Always visible, disabled when disconnected | Greyed out when no session. Discoverable. | ✓ |
| Only visible when connected | Appears/disappears with connection. Cleaner. | |
| Always visible, always clickable | Can open page even without session. | |

**User's choice:** Always visible, disabled when disconnected

| Option | Description | Selected |
|--------|-------------|----------|
| Green camera icon | Simple green when launched. | |
| Green icon + pulse animation | Green with subtle pulse. | |
| Green icon, click again to re-open | Green when active, re-click re-launches. | ✓ |

**User's choice:** Green icon, click again to re-open

---

## Privacy & first-run flow

| Option | Description | Selected |
|--------|-------------|----------|
| Modal dialog on first click | Modal with IP warning, must click "I understand". Shown once. | ✓ |
| Inline warning in companion page | Banner in browser page. Non-blocking. | |
| Modal with 'don't show again' | Same modal with suppress checkbox. | |

**User's choice:** Modal dialog on first click
**Notes:** Later decided no persistence — modal shows every session, not just first.

| Option | Description | Selected |
|--------|-------------|----------|
| Detect + warn in same privacy modal | One modal covers IP privacy + Chromium warning. | ✓ |
| Separate warning, only if non-Chromium | Two dialogs, each focused. | |
| Warning text in companion page | Client-side browser detection in page. | |

**User's choice:** Detect + warn in same privacy modal

| Option | Description | Selected |
|--------|-------------|----------|
| Platform API detection | macOS Launch Services, Windows registry. | ✓ |
| Skip detection, always warn | Always recommend Chromium. | |
| You decide | Claude's choice. | |

**User's choice:** Platform API detection

| Option | Description | Selected |
|--------|-------------|----------|
| Reuse LicenseDialog pattern | Dark modal like NINJAM license dialog. Consistent. | ✓ |
| Simpler alert-style popup | Smaller, lighter popup. | |
| You decide | Claude's choice. | |

**User's choice:** Reuse LicenseDialog pattern

---

## Companion page design

| Option | Description | Selected |
|--------|-------------|----------|
| Minimal iframe wrapper | Dark bg, full-viewport VDO.Ninja iframe. | |
| Styled page with session header | Dark page with thin header showing server/users. | |
| Branded JamWide page | Logo, session info, connection status. Polished. | ✓ |

**User's choice:** Branded JamWide page

| Option | Description | Selected |
|--------|-------------|----------|
| &noaudio URL parameter | VDO.Ninja never plays audio. Cleanest. | ✓ |
| Muted iframes + user warning | Mute via JS. Can enable for troubleshooting. | |
| You decide | Claude's choice. | |

**User's choice:** &noaudio URL parameter

| Option | Description | Selected |
|--------|-------------|----------|
| URL hash parameters | All config in URL fragment. Simple, stateless. | |
| Local WebSocket from plugin | Plugin WS server, companion WS client. Two-way. | ✓ |
| URL hash + optional WebSocket | Hash for initial, WS for runtime updates later. | |

**User's choice:** Local WebSocket from plugin

| Option | Description | Selected |
|--------|-------------|----------|
| JUCE WebSocket in plugin process | Lightweight WS lib compiled into plugin. | ✓ |
| JUCE StreamingSocket + custom upgrade | JUCE built-in socket, manual WS handshake. | |
| You decide | Claude's choice. | |

**User's choice:** JUCE WebSocket in plugin process

---

## WebSocket protocol & messages

| Option | Description | Selected |
|--------|-------------|----------|
| JSON messages | Structured JSON, easy to parse in JS. | ✓ |
| Simple key=value text | Easier C++ construction, less structured. | |
| You decide | Claude's choice. | |

**User's choice:** JSON messages

| Option | Description | Selected |
|--------|-------------|----------|
| Config only (minimal) | Single config message. Phase 12 adds more. | |
| Config + roster | Config + user roster updates. Labels in grid. | ✓ |
| Config + status heartbeat | Config + periodic heartbeat. Connection state. | |

**User's choice:** Config + roster

| Option | Description | Selected |
|--------|-------------|----------|
| Companion page auto-reconnects | Auto-retry with exponential backoff. | |
| No auto-reconnect, user re-clicks | Show reconnect button or re-click Video. | ✓ |
| You decide | Claude's choice. | |

**User's choice:** No auto-reconnect, user re-clicks

---

## GitHub Pages hosting

| Option | Description | Selected |
|--------|-------------|----------|
| Same repo, /docs folder | /docs/video/ in JamWide repo. GitHub Pages from /docs. | ✓ |
| Same repo, gh-pages branch | Source in main, deploy to gh-pages via Actions. | |
| Separate repo | Dedicated jamwide-video repo. | |

**User's choice:** Same repo, /docs folder

| Option | Description | Selected |
|--------|-------------|----------|
| Plain HTML/JS/CSS (no build) | Single index.html, inline JS/CSS. | |
| Simple bundler (Vite) | TypeScript, hot reload, minified output. | ✓ |
| You decide | Claude's choice. | |

**User's choice:** Simple bundler (Vite)

| Option | Description | Selected |
|--------|-------------|----------|
| Default GitHub Pages URL | mkschulze.github.io/JamWide/video/. Free HTTPS. | |
| Custom subdomain | video.jamwide.app. Branded, professional. | ✓ |
| You decide later | Start default, add custom later. | |

**User's choice:** Custom subdomain

---

## NINJAM username as stream ID

| Option | Description | Selected |
|--------|-------------|----------|
| Alphanumeric + underscores only | Strip non-[a-zA-Z0-9_]. Simple, URL-safe. | ✓ |
| URL-encode the username | Percent-encoding. Lossless but uglier. | |
| Hash-based stream ID | Short hash. No sanitization issues. Not readable. | |

**User's choice:** Alphanumeric + underscores only

| Option | Description | Selected |
|--------|-------------|----------|
| Append index suffix | 'Dave' and 'Dave' → 'Dave' and 'Dave_2'. | ✓ |
| Append server-assigned user ID | Use NINJAM internal ID as suffix. | |
| You decide | Claude's choice. | |

**User's choice:** Append index suffix

---

## Claude's Discretion

- WebSocket library choice (libwebsockets, ixwebsocket, or header-only alternative)
- Exact companion page visual design within JamWide branding
- WebSocket port selection strategy
- VDO.Ninja URL parameter fine-tuning
- Hash function for room ID derivation

## Deferred Ideas

- Popout mode — Phase 13 (VID-07)
- setBufferDelay sync — Phase 12 (VID-08)
- Room password hardening — Phase 12 (VID-09)
- Advanced roster discovery via external API — Phase 12 (VID-10)
- OSC video control — Phase 13 (VID-11)
- Bandwidth profiles — Phase 12 (VID-12)
- Auto-reconnect WebSocket — future enhancement
- Auto-launch video preference — future enhancement
