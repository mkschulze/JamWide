---
layout: default
title: FAQ
---

# Frequently Asked Questions

---

## General

### What is JamWide?

JamWide is a NINJAM client built with JUCE that runs as an audio plugin (VST3, AU, CLAP) in your DAW or as a standalone application. It lets you join online jam sessions with musicians around the world.

### What is NINJAM?

NINJAM (Novel Intervallic Network Jamming Architecture for Music) is an open-source protocol created by Cockos (makers of REAPER) for real-time musical collaboration over the internet.

### How is JamWide different from other NINJAM clients?

JamWide offers:

- **Multichannel output** — Route each remote musician to a separate stereo track in your DAW (17 stereo buses)
- **FLAC lossless audio** — Send and receive uncompressed audio quality
- **DAW transport sync** — Plugin only broadcasts when the DAW is playing
- **Cross-platform** — macOS, Windows, and Linux with VST3, AU, CLAP, and Standalone
- **Standalone mode** — Use without a DAW

### Is JamWide free?

Yes! JamWide is open-source software released under the GPL-2.0 license.

---

## How It Works

### Why is there a delay when playing with others?

NINJAM uses a unique approach: instead of trying to eliminate latency (impossible over the internet), it embraces it. Everyone plays along with what others recorded in the previous "interval."

This means there's always a one-interval delay (typically 4-16 beats), but everyone is perfectly synchronized to the same beat.

### What's an "interval"?

An interval is a fixed number of beats (BPI - Beats Per Interval) at a set tempo (BPM). Common settings:
- 120 BPM, 16 BPI = 8 second intervals
- 100 BPM, 8 BPI = 4.8 second intervals

You can vote to change BPM or BPI by clicking the values in the beat bar.

### Can I hear myself in real-time?

Yes! Your local monitoring is instant. Only the audio you send to others (and receive from them) has the interval delay.

---

## Compatibility

### Which DAWs are supported?

JamWide works with any DAW that supports VST3, Audio Unit, or CLAP plugins:

| DAW | VST3 | AU | CLAP |
|-----|------|-----|------|
| Ableton Live | yes | yes | — |
| Logic Pro | — | yes | — |
| REAPER | yes | yes | yes |
| Bitwig Studio | yes | — | yes |
| Cubase | yes | — | — |
| Studio One | yes | — | — |
| FL Studio | yes | — | yes |
| GarageBand | — | yes | — |

### Which operating systems are supported?

- **macOS**: 10.15 (Catalina) and later (Intel and Apple Silicon universal binary)
- **Windows**: Windows 10 and later (64-bit)
- **Linux**: Ubuntu 22.04+ or equivalent (X11, ALSA/JACK)

### Can I use JamWide standalone (without a DAW)?

Yes! JamWide includes a standalone application with built-in audio device selection. In standalone mode, audio broadcasts continuously (no transport sync needed).

### Can I use JamWide with my audio interface?

Yes! As a plugin, JamWide uses whatever audio setup your DAW is configured with. In standalone mode, you select your audio device directly.

---

## Audio

### What audio codecs are supported?

- **FLAC** (default) — Lossless audio quality, higher bandwidth
- **Vorbis** — Compressed audio, lower bandwidth

You can switch codecs using the selector in the connection bar. FLAC and Vorbis users can coexist in the same session.

### What sample rates are supported?

JamWide works with common sample rates: 44.1 kHz, 48 kHz, 88.2 kHz, and 96 kHz.

### How does multichannel routing work?

JamWide provides 17 stereo output buses. Click the **Route** button to choose:
- **Manual** — All audio on main mix (bus 0)
- **Assign by User** — Each user on a separate stereo bus
- **Assign by Channel** — Each remote channel on a separate bus

Enable additional output buses in your DAW's plugin I/O settings to access them.

### How many channels can I send?

Up to 4 stereo local input channels. Expand the local channel strip to access channels 2-4, each with independent volume, pan, mute, and input bus selection.

---

## Servers

### Where can I find NINJAM servers?

JamWide includes a built-in server browser that shows active public servers. You can also check [ninbot.com](https://ninbot.com) for a server list.

### Can I run my own server?

Yes! NINJAM server software is available from [Cockos](https://www.cockos.com/ninjam/). You'll need to configure port forwarding if you want others to connect from outside your network.

### Are private servers supported?

Yes, you can connect to password-protected servers by entering the password in the connection bar.

---

## Troubleshooting

### The plugin isn't showing up in my DAW

1. Make sure you copied it to the correct folder
2. Rescan plugins in your DAW settings
3. On macOS, check System Preferences > Security if you see a warning
4. Verify you have the right format for your DAW

### I can't connect to any server

1. Check your internet connection
2. Make sure no firewall is blocking the connection
3. Try a different server from the browser
4. Check if the server requires a password

### No multichannel output

1. Enable additional output buses in the plugin I/O configuration
2. Create aux/return tracks receiving from JamWide's output buses
3. Set routing mode to "Assign by User" or "Assign by Channel"

### I can hear myself delayed

This is normal for audio coming back from the server. Your local monitoring is always instant — the delay only applies to what others hear.

---

## Development

### How can I contribute?

Check out the [GitHub repository](https://github.com/mkschulze/JamWide) for:
- Reporting bugs
- Suggesting features
- Submitting pull requests

### What technologies does JamWide use?

- **JUCE** 8.0.12 — Plugin framework and UI
- **libFLAC** 1.5.0 — Lossless audio codec
- **WDL** — Networking and utilities (from Cockos)
- **libogg/libvorbis** — Vorbis audio encoding

---

## Still have questions?

Open an issue on [GitHub](https://github.com/mkschulze/JamWide/issues) and we'll help you out!
