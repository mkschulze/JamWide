#!/usr/bin/env python3
"""Generate JamWide.tosc TouchOSC template.

Run: python3 scripts/generate_tosc.py
Output: assets/JamWide.tosc

This is a build-time utility for reproducible template generation.
The primary asset is assets/JamWide.tosc. Users can also edit the
template in TouchOSC Editor and re-import; this script can regenerate
the baseline if needed.

Layout targets iPad landscape (1024x768 base resolution).
All OSC addresses must match docs/osc.md exactly (single source of truth).
"""
import xml.etree.ElementTree as ET
import zlib
import os
import sys
import uuid

# ---------------------------------------------------------------------------
# Color palette (Voicemeeter dark theme per user preference)
# ---------------------------------------------------------------------------
COLOR_BG       = (0.102, 0.102, 0.102, 1.0)  # #1a1a1a
COLOR_PANEL    = (0.15, 0.15, 0.15, 1.0)      # dark panel bg
COLOR_FADER    = (0.0, 0.8, 0.4, 1.0)         # #00cc66 green
COLOR_MUTE     = (0.8, 0.2, 0.2, 1.0)         # #cc3333 red
COLOR_SOLO     = (0.8, 0.8, 0.2, 1.0)         # #cccc33 yellow
COLOR_PAN      = (0.2, 0.8, 0.8, 1.0)         # #33cccc cyan
COLOR_LABEL    = (1.0, 1.0, 1.0, 1.0)         # white
COLOR_VU       = (0.2, 0.8, 0.2, 1.0)         # #33cc33 green
COLOR_INFO     = (0.6, 0.6, 0.6, 1.0)         # grey for info labels
COLOR_CONNECT  = (0.2, 0.6, 1.0, 1.0)         # blue for connect
COLOR_DISCON   = (0.8, 0.3, 0.1, 1.0)         # orange for disconnect
COLOR_TITLE    = (0.0, 0.8, 0.4, 1.0)         # green title


# ---------------------------------------------------------------------------
# XML helpers
# ---------------------------------------------------------------------------
def make_id():
    """Generate a unique UUID for a TouchOSC node."""
    return str(uuid.uuid4())


def add_property_s(props_el, key, value):
    """Add a string property."""
    p = ET.SubElement(props_el, "property", type="s")
    ET.SubElement(p, "key").text = key
    ET.SubElement(p, "value").text = str(value)


def add_property_i(props_el, key, value):
    """Add an integer property."""
    p = ET.SubElement(props_el, "property", type="i")
    ET.SubElement(p, "key").text = key
    ET.SubElement(p, "value").text = str(int(value))


def add_property_f(props_el, key, value):
    """Add a float property."""
    p = ET.SubElement(props_el, "property", type="f")
    ET.SubElement(p, "key").text = key
    ET.SubElement(p, "value").text = str(float(value))


def add_property_b(props_el, key, value):
    """Add a boolean property."""
    p = ET.SubElement(props_el, "property", type="b")
    ET.SubElement(p, "key").text = key
    ET.SubElement(p, "value").text = "1" if value else "0"


def add_property_frame(props_el, x, y, w, h):
    """Add a frame (rect) property for position and size."""
    p = ET.SubElement(props_el, "property", type="r")
    ET.SubElement(p, "key").text = "frame"
    v = ET.SubElement(p, "value")
    ET.SubElement(v, "x").text = str(int(x))
    ET.SubElement(v, "y").text = str(int(y))
    ET.SubElement(v, "w").text = str(int(w))
    ET.SubElement(v, "h").text = str(int(h))


def add_property_color(props_el, key, rgba):
    """Add a color property. rgba is a tuple (r, g, b, a) with 0-1 values."""
    p = ET.SubElement(props_el, "property", type="c")
    ET.SubElement(p, "key").text = key
    v = ET.SubElement(p, "value")
    ET.SubElement(v, "r").text = str(rgba[0])
    ET.SubElement(v, "g").text = str(rgba[1])
    ET.SubElement(v, "b").text = str(rgba[2])
    ET.SubElement(v, "a").text = str(rgba[3])


def add_osc_message(msgs_el, path, send=True, receive=True,
                    var="x", conversion="FLOAT",
                    scale_min=0, scale_max=1,
                    trigger_condition="ANY"):
    """Add an OSC message configuration to a node's messages element."""
    osc = ET.SubElement(msgs_el, "osc")
    ET.SubElement(osc, "enabled").text = "1"
    ET.SubElement(osc, "send").text = "1" if send else "0"
    ET.SubElement(osc, "receive").text = "1" if receive else "0"
    ET.SubElement(osc, "feedback").text = "0"
    ET.SubElement(osc, "connections").text = "00001"
    triggers = ET.SubElement(osc, "triggers")
    trigger = ET.SubElement(triggers, "trigger")
    ET.SubElement(trigger, "var").text = var
    ET.SubElement(trigger, "condition").text = trigger_condition
    ET.SubElement(osc, "path").text = path
    args = ET.SubElement(osc, "arguments")
    partial = ET.SubElement(args, "partial")
    ET.SubElement(partial, "type").text = "VALUE"
    ET.SubElement(partial, "conversion").text = conversion
    ET.SubElement(partial, "value").text = var
    ET.SubElement(partial, "scaleMin").text = str(scale_min)
    ET.SubElement(partial, "scaleMax").text = str(scale_max)
    return osc


def add_osc_string_message(msgs_el, path, send=True, receive=True):
    """Add an OSC message that sends/receives a string value."""
    osc = ET.SubElement(msgs_el, "osc")
    ET.SubElement(osc, "enabled").text = "1"
    ET.SubElement(osc, "send").text = "1" if send else "0"
    ET.SubElement(osc, "receive").text = "1" if receive else "0"
    ET.SubElement(osc, "feedback").text = "0"
    ET.SubElement(osc, "connections").text = "00001"
    triggers = ET.SubElement(osc, "triggers")
    trigger = ET.SubElement(triggers, "trigger")
    ET.SubElement(trigger, "var").text = "text"
    ET.SubElement(trigger, "condition").text = "ANY"
    ET.SubElement(osc, "path").text = path
    args = ET.SubElement(osc, "arguments")
    partial = ET.SubElement(args, "partial")
    ET.SubElement(partial, "type").text = "VALUE"
    ET.SubElement(partial, "conversion").text = "STRING"
    ET.SubElement(partial, "value").text = "text"
    ET.SubElement(partial, "scaleMin").text = "0"
    ET.SubElement(partial, "scaleMax").text = "1"
    return osc


def create_node(parent_children, node_type, name, x, y, w, h,
                color=None, background=False, interactive=True,
                outline=False):
    """Create a TouchOSC node element and return (node_element, children_element)."""
    node = ET.SubElement(parent_children, "node", ID=make_id(), type=node_type)
    props = ET.SubElement(node, "properties")
    add_property_s(props, "name", name)
    add_property_frame(props, x, y, w, h)
    if color:
        add_property_color(props, "color", color)
    if background:
        add_property_b(props, "background", True)
    if not interactive:
        add_property_b(props, "interactive", False)
    if outline:
        add_property_b(props, "outline", True)
    ET.SubElement(node, "values")
    msgs = ET.SubElement(node, "messages")
    children = ET.SubElement(node, "children")
    return node, props, msgs, children


# ---------------------------------------------------------------------------
# Component builders
# ---------------------------------------------------------------------------

def make_label(parent_children, name, x, y, w, h, text="",
               color=COLOR_LABEL, osc_path=None, receive_only=False,
               text_size=14, background=False):
    """Create a label, optionally bound to an OSC address for receive-only display."""
    node, props, msgs, children = create_node(
        parent_children, "LABEL", name, x, y, w, h,
        color=color, background=background, interactive=False
    )
    if text:
        add_property_s(props, "text", text)
    add_property_i(props, "textSize", text_size)
    if osc_path:
        add_osc_message(msgs, osc_path, send=False, receive=True)
    return node


def make_fader_v(parent_children, name, x, y, w, h, osc_path,
                 color=COLOR_FADER, send=True, receive=True,
                 interactive=True, scale_min=0, scale_max=1):
    """Create a vertical fader (height > width) bound to an OSC address."""
    node, props, msgs, children = create_node(
        parent_children, "FADER", name, x, y, w, h,
        color=color, interactive=interactive
    )
    add_osc_message(msgs, osc_path, send=send, receive=receive,
                    scale_min=scale_min, scale_max=scale_max)
    return node


def make_button(parent_children, name, x, y, w, h, osc_path,
                color=COLOR_MUTE, send=True, receive=True,
                button_type=0):
    """Create a button bound to an OSC address.
    button_type: 0 = momentary, 1 = toggle (for mute/solo)."""
    node, props, msgs, children = create_node(
        parent_children, "BUTTON", name, x, y, w, h,
        color=color
    )
    add_property_s(props, "text", name.split("_")[-1] if "_" in name else name)
    add_property_i(props, "buttonType", button_type)
    add_osc_message(msgs, osc_path, send=send, receive=receive)
    return node


def make_encoder(parent_children, name, x, y, w, h, osc_path,
                 color=COLOR_PAN, send=True, receive=True):
    """Create an encoder (knob) bound to an OSC address."""
    node, props, msgs, children = create_node(
        parent_children, "ENCODER", name, x, y, w, h,
        color=color
    )
    add_osc_message(msgs, osc_path, send=send, receive=receive)
    return node


def make_text_input(parent_children, name, x, y, w, h, osc_path,
                    color=COLOR_CONNECT, default_text=""):
    """Create a text input field bound to an OSC address (sends string)."""
    node, props, msgs, children = create_node(
        parent_children, "TEXT", name, x, y, w, h,
        color=color, outline=True
    )
    if default_text:
        add_property_s(props, "text", default_text)
    add_property_i(props, "textSize", 14)
    add_osc_string_message(msgs, osc_path, send=True, receive=False)
    return node


# ---------------------------------------------------------------------------
# Strip builders
# ---------------------------------------------------------------------------

def build_vu_pair(parent_children, prefix, x, y, h):
    """Build a pair of narrow VU meter faders (receive-only, no interaction)."""
    vu_w = 8
    gap = 2
    make_fader_v(parent_children, f"{prefix}_vu_L", x, y, vu_w, h,
                 f"{prefix}/vu/left", color=COLOR_VU,
                 send=False, receive=True, interactive=False)
    make_fader_v(parent_children, f"{prefix}_vu_R", x + vu_w + gap, y, vu_w, h,
                 f"{prefix}/vu/right", color=COLOR_VU,
                 send=False, receive=True, interactive=False)


def build_local_strip(parent_children, n, x, y, strip_w, strip_h):
    """Build a local channel strip with volume, pan, mute, solo, VU.

    Addresses (from docs/osc.md):
      /JamWide/local/{n}/volume   float 0-1
      /JamWide/local/{n}/pan      float 0-1
      /JamWide/local/{n}/mute     float 0/1
      /JamWide/local/{n}/solo     float 0/1
      /JamWide/local/{n}/vu/left  float 0-1
      /JamWide/local/{n}/vu/right float 0-1
    """
    prefix = f"/JamWide/local/{n}"
    pad = 4
    btn_h = 28
    knob_h = 40
    label_h = 20
    vu_h = 120

    # Create group container
    _, _, _, children = create_node(
        parent_children, "GROUP", f"Local_{n}", x, y, strip_w, strip_h,
        color=COLOR_PANEL, background=True
    )

    # Label at top
    make_label(children, f"lbl_local_{n}", pad, pad, strip_w - 2 * pad, label_h,
               text=f"Local {n}", color=COLOR_LABEL, text_size=12)

    cur_y = pad + label_h + pad

    # Solo button
    make_button(children, f"local_{n}_S", pad, cur_y, strip_w - 2 * pad, btn_h,
                f"{prefix}/solo", color=COLOR_SOLO, button_type=1)
    cur_y += btn_h + pad

    # Mute button
    make_button(children, f"local_{n}_M", pad, cur_y, strip_w - 2 * pad, btn_h,
                f"{prefix}/mute", color=COLOR_MUTE, button_type=1)
    cur_y += btn_h + pad

    # Pan knob
    knob_size = min(strip_w - 2 * pad, knob_h)
    knob_x = pad + (strip_w - 2 * pad - knob_size) // 2
    make_encoder(children, f"local_{n}_pan", knob_x, cur_y, knob_size, knob_size,
                 f"{prefix}/pan", color=COLOR_PAN)
    cur_y += knob_size + pad

    # Volume fader (fill remaining space minus VU area)
    fader_h = strip_h - cur_y - vu_h - pad * 2
    fader_w = strip_w - 2 * pad - 22  # leave room for VU
    if fader_h < 60:
        fader_h = 60
    make_fader_v(children, f"local_{n}_vol", pad, cur_y, fader_w, fader_h,
                 f"{prefix}/volume", color=COLOR_FADER)

    # VU meters next to fader
    build_vu_pair(children, prefix, pad + fader_w + 2, cur_y, fader_h)

    cur_y += fader_h + pad


def build_remote_strip(parent_children, idx, x, y, strip_w, strip_h):
    """Build a remote user strip with name, volume, pan, mute, solo.

    Addresses (from docs/osc.md planned Phase 10):
      /JamWide/remote/{idx}/name     string (receive only)
      /JamWide/remote/{idx}/volume   float 0-1
      /JamWide/remote/{idx}/pan      float 0-1
      /JamWide/remote/{idx}/mute     float 0/1
      /JamWide/remote/{idx}/solo     float 0/1
    Remote VU intentionally omitted per design decision D-16.
    """
    prefix = f"/JamWide/remote/{idx}"
    pad = 4
    btn_h = 26
    knob_h = 36
    label_h = 20

    # Create group container
    _, _, _, children = create_node(
        parent_children, "GROUP", f"Remote_{idx}", x, y, strip_w, strip_h,
        color=COLOR_PANEL, background=True
    )

    # Name label (receive-only, shows username from roster broadcast)
    make_label(children, f"lbl_remote_{idx}", pad, pad, strip_w - 2 * pad, label_h,
               text=f"Remote {idx}", color=COLOR_LABEL,
               osc_path=f"{prefix}/name", receive_only=True, text_size=11)

    cur_y = pad + label_h + pad

    # Solo button
    make_button(children, f"remote_{idx}_S", pad, cur_y, strip_w - 2 * pad, btn_h,
                f"{prefix}/solo", color=COLOR_SOLO, button_type=1)
    cur_y += btn_h + pad

    # Mute button
    make_button(children, f"remote_{idx}_M", pad, cur_y, strip_w - 2 * pad, btn_h,
                f"{prefix}/mute", color=COLOR_MUTE, button_type=1)
    cur_y += btn_h + pad

    # Pan knob
    knob_size = min(strip_w - 2 * pad, knob_h)
    knob_x = pad + (strip_w - 2 * pad - knob_size) // 2
    make_encoder(children, f"remote_{idx}_pan", knob_x, cur_y, knob_size, knob_size,
                 f"{prefix}/pan", color=COLOR_PAN)
    cur_y += knob_size + pad

    # Volume fader (fill remaining height)
    fader_h = strip_h - cur_y - pad
    if fader_h < 40:
        fader_h = 40
    make_fader_v(children, f"remote_{idx}_vol", pad, cur_y, strip_w - 2 * pad, fader_h,
                 f"{prefix}/volume", color=COLOR_FADER)


# ---------------------------------------------------------------------------
# Main template builder
# ---------------------------------------------------------------------------

def build_template():
    """Build the complete JamWide TouchOSC template.

    Layout (iPad landscape 1024x768):
    +-----------------------------------------------------------------+
    | Session Info (top-left)  | Master | Metro | (top-right padding)  |
    |   Title, BPM, BPI, etc  | Vol+VU | Vol   |                      |
    |   Connect/Disconnect     | Mute   | Pan   |                      |
    |                          |        | Mute  |                      |
    +--------------------------+--------+-------+----------------------+
    | Local 1 | Local 2 | Local 3 | Local 4 | (local channels)       |
    |  vol    |  vol    |  vol    |  vol    |                          |
    |  pan    |  pan    |  pan    |  pan    |                          |
    |  m/s    |  m/s    |  m/s    |  m/s    |                          |
    +---------+---------+---------+---------+---------+---------+-----+
    | Rem 1 | Rem 2 | Rem 3 | Rem 4 | Rem 5 | Rem 6 | Rem 7 | Rem 8 |
    | name  | name  | name  | name  | name  | name  | name  | name   |
    | vol   | vol   | vol   | vol   | vol   | vol   | vol   | vol    |
    | pan   | pan   | pan   | pan   | pan   | pan   | pan   | pan    |
    | m/s   | m/s   | m/s   | m/s   | m/s   | m/s   | m/s   | m/s    |
    +-------+-------+-------+-------+-------+-------+-------+--------+
    """
    root = ET.Element("lexml", version="4")

    # Root page (GROUP)
    page = ET.SubElement(root, "node", type="GROUP")
    page_props = ET.SubElement(page, "properties")
    add_property_s(page_props, "name", "JamWide")
    add_property_frame(page_props, 0, 0, 1024, 768)
    add_property_color(page_props, "color", COLOR_BG)
    add_property_b(page_props, "background", True)
    ET.SubElement(page, "values")
    ET.SubElement(page, "messages")
    page_children = ET.SubElement(page, "children")

    # -----------------------------------------------------------------------
    # LAYOUT CONSTANTS
    # -----------------------------------------------------------------------
    W = 1024
    H = 768

    # Top bar height (session info + master + metro)
    top_h = 200
    # Local strip area
    local_area_y = top_h + 4
    local_area_h = 280
    # Remote strip area
    remote_area_y = local_area_y + local_area_h + 4
    remote_area_h = H - remote_area_y - 4

    # -----------------------------------------------------------------------
    # SESSION INFO PANEL (top-left)
    # -----------------------------------------------------------------------
    session_w = 310
    session_h = top_h
    _, _, _, session_children = create_node(
        page_children, "GROUP", "Session", 4, 4, session_w, session_h - 8,
        color=COLOR_PANEL, background=True
    )

    # Title
    make_label(session_children, "title", 8, 4, 160, 24,
               text="JamWide", color=COLOR_TITLE, text_size=20)

    # Session telemetry labels
    info_x = 8
    info_w = 90
    val_x = 100
    val_w = 80
    row_h = 20
    row_y = 32

    # BPM
    make_label(session_children, "lbl_bpm_t", info_x, row_y, info_w, row_h,
               text="BPM:", color=COLOR_INFO, text_size=12)
    make_label(session_children, "lbl_bpm_v", val_x, row_y, val_w, row_h,
               color=COLOR_LABEL, osc_path="/JamWide/session/bpm", text_size=12)
    row_y += row_h + 2

    # BPI
    make_label(session_children, "lbl_bpi_t", info_x, row_y, info_w, row_h,
               text="BPI:", color=COLOR_INFO, text_size=12)
    make_label(session_children, "lbl_bpi_v", val_x, row_y, val_w, row_h,
               color=COLOR_LABEL, osc_path="/JamWide/session/bpi", text_size=12)
    row_y += row_h + 2

    # Beat
    make_label(session_children, "lbl_beat_t", info_x, row_y, info_w, row_h,
               text="Beat:", color=COLOR_INFO, text_size=12)
    make_label(session_children, "lbl_beat_v", val_x, row_y, val_w, row_h,
               color=COLOR_LABEL, osc_path="/JamWide/session/beat", text_size=12)
    row_y += row_h + 2

    # Status
    make_label(session_children, "lbl_stat_t", info_x, row_y, info_w, row_h,
               text="Status:", color=COLOR_INFO, text_size=12)
    make_label(session_children, "lbl_stat_v", val_x, row_y, val_w, row_h,
               color=COLOR_LABEL, osc_path="/JamWide/session/status", text_size=12)
    row_y += row_h + 2

    # Users
    make_label(session_children, "lbl_users_t", info_x, row_y, info_w, row_h,
               text="Users:", color=COLOR_INFO, text_size=12)
    make_label(session_children, "lbl_users_v", val_x, row_y, val_w, row_h,
               color=COLOR_LABEL, osc_path="/JamWide/session/users", text_size=12)

    # --- Connect/Disconnect (right side of session panel) ---
    conn_x = 190
    conn_w = 110

    # Server address text input
    make_label(session_children, "lbl_server", conn_x, 4, conn_w, 16,
               text="Server:", color=COLOR_INFO, text_size=10)
    make_text_input(session_children, "server_addr", conn_x, 22, conn_w, 28,
                    "/JamWide/session/connect", color=COLOR_CONNECT,
                    default_text="ninbot.com:2049")

    # Connect button (sends text field value on press)
    connect_btn, c_props, c_msgs, _ = create_node(
        session_children, "BUTTON", "Connect", conn_x, 54, conn_w, 32,
        color=COLOR_CONNECT
    )
    add_property_s(c_props, "text", "Connect")
    add_property_i(c_props, "textSize", 13)
    # The connect button sends the /session/connect path with a trigger
    # In practice, the TEXT field above sends the string on confirm/enter.
    # This button provides a visual affordance. It sends a float 1.0 trigger
    # to the same path as a "confirm" signal.
    add_osc_message(c_msgs, "/JamWide/session/connect",
                    send=True, receive=False, scale_min=0, scale_max=1)

    # Disconnect button
    discon_btn, d_props, d_msgs, _ = create_node(
        session_children, "BUTTON", "Disconnect", conn_x, 90, conn_w, 32,
        color=COLOR_DISCON
    )
    add_property_s(d_props, "text", "Disconnect")
    add_property_i(d_props, "textSize", 13)
    add_osc_message(d_msgs, "/JamWide/session/disconnect",
                    send=True, receive=False, scale_min=0, scale_max=1)

    # -----------------------------------------------------------------------
    # MASTER SECTION (top, next to session info)
    # -----------------------------------------------------------------------
    master_x = session_w + 8
    master_w = 100
    master_h = top_h - 8
    _, _, _, master_children = create_node(
        page_children, "GROUP", "Master", master_x, 4, master_w, master_h,
        color=COLOR_PANEL, background=True
    )

    make_label(master_children, "lbl_master", 4, 4, master_w - 8, 20,
               text="Master", color=COLOR_LABEL, text_size=13)

    # Mute button
    make_button(master_children, "master_M", 4, 28, master_w - 8, 28,
                "/JamWide/master/mute", color=COLOR_MUTE, button_type=1)

    # Volume fader + VU meters
    fader_area_y = 60
    fader_h = master_h - fader_area_y - 4
    fader_w = master_w - 8 - 22  # leave room for VU
    make_fader_v(master_children, "master_vol", 4, fader_area_y, fader_w, fader_h,
                 "/JamWide/master/volume", color=COLOR_FADER)

    # VU meters
    build_vu_pair(master_children, "/JamWide/master",
                  4 + fader_w + 2, fader_area_y, fader_h)

    # -----------------------------------------------------------------------
    # METRONOME SECTION (top, next to master)
    # -----------------------------------------------------------------------
    metro_x = master_x + master_w + 4
    metro_w = 100
    metro_h = top_h - 8
    _, _, _, metro_children = create_node(
        page_children, "GROUP", "Metro", metro_x, 4, metro_w, metro_h,
        color=COLOR_PANEL, background=True
    )

    make_label(metro_children, "lbl_metro", 4, 4, metro_w - 8, 20,
               text="Metro", color=COLOR_LABEL, text_size=13)

    # Mute button
    make_button(metro_children, "metro_M", 4, 28, metro_w - 8, 28,
                "/JamWide/metro/mute", color=COLOR_MUTE, button_type=1)

    # Pan knob
    knob_size = 44
    knob_x = 4 + (metro_w - 8 - knob_size) // 2
    make_encoder(metro_children, "metro_pan", knob_x, 60, knob_size, knob_size,
                 "/JamWide/metro/pan", color=COLOR_PAN)

    # Volume fader
    vol_y = 60 + knob_size + 4
    vol_h = metro_h - vol_y - 4
    make_fader_v(metro_children, "metro_vol", 4, vol_y, metro_w - 8, vol_h,
                 "/JamWide/metro/volume", color=COLOR_FADER)

    # -----------------------------------------------------------------------
    # LOCAL CHANNELS 1-4 (middle row)
    # -----------------------------------------------------------------------
    local_total_w = W - 8  # full width with margins
    local_strip_w = local_total_w // 4

    for n in range(1, 5):
        strip_x = 4 + (n - 1) * local_strip_w
        build_local_strip(page_children, n, strip_x, local_area_y,
                          local_strip_w - 4, local_area_h)

    # -----------------------------------------------------------------------
    # REMOTE USERS 1-8 (bottom row)
    # -----------------------------------------------------------------------
    remote_total_w = W - 8
    remote_strip_w = remote_total_w // 8

    for idx in range(1, 9):
        strip_x = 4 + (idx - 1) * remote_strip_w
        build_remote_strip(page_children, idx, strip_x, remote_area_y,
                           remote_strip_w - 4, remote_area_h)

    return root


def main():
    """Generate the JamWide.tosc template and self-validate."""
    root = build_template()
    xml_bytes = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    compressed = zlib.compress(xml_bytes)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    output_dir = os.path.join(project_root, "assets")
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, "JamWide.tosc")

    with open(output_path, "wb") as f:
        f.write(compressed)

    # Self-validation: decompress and check key addresses
    xml_str = zlib.decompress(compressed).decode("utf-8")

    required_addresses = [
        "/JamWide/master/volume",
        "/JamWide/master/mute",
        "/JamWide/master/vu/left",
        "/JamWide/master/vu/right",
        "/JamWide/metro/volume",
        "/JamWide/metro/pan",
        "/JamWide/metro/mute",
        "/JamWide/local/1/volume",
        "/JamWide/local/1/pan",
        "/JamWide/local/1/mute",
        "/JamWide/local/1/solo",
        "/JamWide/local/1/vu/left",
        "/JamWide/local/1/vu/right",
        "/JamWide/local/2/volume",
        "/JamWide/local/3/volume",
        "/JamWide/local/4/volume",
        "/JamWide/local/4/pan",
        "/JamWide/local/4/mute",
        "/JamWide/local/4/solo",
        "/JamWide/local/4/vu/left",
        "/JamWide/remote/1/volume",
        "/JamWide/remote/1/pan",
        "/JamWide/remote/1/mute",
        "/JamWide/remote/1/solo",
        "/JamWide/remote/1/name",
        "/JamWide/remote/2/volume",
        "/JamWide/remote/3/volume",
        "/JamWide/remote/4/volume",
        "/JamWide/remote/5/volume",
        "/JamWide/remote/6/volume",
        "/JamWide/remote/7/volume",
        "/JamWide/remote/8/volume",
        "/JamWide/remote/8/pan",
        "/JamWide/remote/8/mute",
        "/JamWide/remote/8/solo",
        "/JamWide/remote/8/name",
        "/JamWide/session/bpm",
        "/JamWide/session/bpi",
        "/JamWide/session/beat",
        "/JamWide/session/status",
        "/JamWide/session/users",
        "/JamWide/session/connect",
        "/JamWide/session/disconnect",
    ]

    missing = [a for a in required_addresses if a not in xml_str]
    if missing:
        print(f"ERROR: Missing addresses in template: {missing}", file=sys.stderr)
        sys.exit(1)

    # Verify remote VU is NOT in template (intentional omission per D-16)
    if "/JamWide/remote/1/vu/" in xml_str:
        print(
            "WARNING: Remote VU found in template (should be omitted per design)",
            file=sys.stderr,
        )

    # Verify no out-of-range remote slots (template has max 8 per D-13)
    if "/JamWide/remote/9/volume" in xml_str:
        print(
            "ERROR: Template has remote slot 9+ (should have max 8 per D-13)",
            file=sys.stderr,
        )
        sys.exit(1)

    print(f"Generated {output_path} ({len(compressed)} bytes)")
    print(f"XML size: {len(xml_bytes)} bytes (before compression)")
    print(f"Validated {len(required_addresses)} required addresses")
    print("All checks passed.")


if __name__ == "__main__":
    main()
