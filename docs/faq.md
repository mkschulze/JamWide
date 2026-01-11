---
layout: default
title: FAQ
---

# Frequently Asked Questions

---

## General

### What is JamWide?

JamWide is a NINJAM client that runs as an audio plugin (CLAP, VST3, AU) in your DAW. It lets you join online jam sessions with musicians around the world without leaving your music production environment.

### What is NINJAM?

NINJAM (Novel Intervallic Network Jamming Architecture for Music) is an open-source protocol created by Cockos (makers of REAPER) for real-time musical collaboration over the internet.

### How is JamWide different from the official NINJAM clients?

JamWide runs as a plugin inside your DAW, while traditional NINJAM clients are standalone applications. This means:

- No external audio routing needed
- Record directly in your DAW
- Use your existing plugin effects
- Stay in your familiar workflow

### Is JamWide free?

Yes! JamWide is open-source software released under the GPL-2.0 license.

---

## How It Works

### Why is there a delay when playing with others?

NINJAM uses a unique approach: instead of trying to eliminate latency (impossible over the internet), it embraces it. Everyone plays along with what others recorded in the previous "interval."

This means there's always a one-interval delay (typically 4-16 beats), but everyone is perfectly synchronized to the same beat. It's a different way of jamming that opens up creative possibilities.

### What's an "interval"?

An interval is a fixed number of beats (BPI - Beats Per Interval) at a set tempo (BPM). Common settings:
- 120 BPM, 16 BPI = 8 second intervals
- 100 BPM, 8 BPI = 4.8 second intervals

The server administrator sets these values.

### Can I hear myself in real-time?

Yes! Your local monitoring is instant. Only the audio you send to others (and receive from them) has the interval delay.

---

## Compatibility

### Which DAWs are supported?

JamWide works with any DAW that supports CLAP, VST3, or Audio Unit plugins:

| DAW | CLAP | VST3 | AU |
|-----|------|------|-----|
| Ableton Live | — | ✅ | ✅ |
| Logic Pro | — | — | ✅ |
| REAPER | ✅ | ✅ | ✅ |
| Bitwig Studio | ✅ | ✅ | — |
| Cubase | — | ✅ | — |
| FL Studio | ✅ | ✅ | — |
| GarageBand | — | — | ✅ |

### Which operating systems are supported?

- **macOS**: 10.15 (Catalina) and later (Intel and Apple Silicon)
- **Windows**: Windows 10 and later (64-bit)
- **Linux**: Planned for a future release

### Can I use JamWide with my audio interface?

Yes! JamWide uses whatever audio setup your DAW is configured with. If your DAW works with your interface, JamWide will too.

---

## Servers

### Where can I find NINJAM servers?

JamWide includes a built-in server browser that shows active public servers. You can also check [ninbot.com](https://ninbot.com) for a server list.

### Can I run my own server?

Yes! NINJAM server software is available from [Cockos](https://www.cockos.com/ninjam/). You'll need to configure port forwarding if you want others to connect from outside your network.

### Are private servers supported?

Yes, you can connect to password-protected servers by entering the password in the connection panel.

---

## Audio

### What sample rates are supported?

JamWide works with common sample rates: 44.1 kHz, 48 kHz, 88.2 kHz, and 96 kHz.

### How many channels can I send/receive?

Currently, JamWide supports stereo (2 channels) input and output. Multi-channel support is planned for a future release.

### What audio codec is used?

NINJAM uses OGG/Vorbis encoding for efficient network streaming with good audio quality.

---

## Troubleshooting

### The plugin isn't showing up in my DAW

1. Make sure you copied it to the correct folder
2. Rescan plugins in your DAW settings
3. On macOS, check System Preferences → Security if you see a warning
4. Verify you have the right format for your DAW

### I can't connect to any server

1. Check your internet connection
2. Make sure no firewall is blocking the connection
3. Try a different server
4. Check if the server requires a password

### Audio sounds distorted

1. Lower the Master Volume
2. Check that your DAW isn't clipping
3. Reduce input gain if your signal is too hot

### I can hear myself delayed

This is normal for audio coming back from the server. Use the "Monitor Input" option to hear yourself in real-time (directly, not through the server).

---

## Development

### How can I contribute?

Check out the [GitHub repository](https://github.com/mkschulze/JamWide) for:
- Reporting bugs
- Suggesting features
- Submitting pull requests
- Development documentation

### Is the source code available?

Yes! JamWide is fully open-source. Clone the repository and build it yourself:

```bash
git clone --recursive https://github.com/mkschulze/JamWide.git
```

### What technologies does JamWide use?

- **CLAP** — Plugin API
- **Dear ImGui** — User interface
- **WDL** — Networking and utilities (from Cockos)
- **libogg/libvorbis** — Audio encoding

---

## Still have questions?

Open an issue on [GitHub](https://github.com/mkschulze/JamWide/issues) and we'll help you out!
