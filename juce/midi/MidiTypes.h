#pragma once

/** Distinguishes CC messages from Note On/Off messages in the MIDI mapping system. */
enum class MidiMsgType : int { CC = 0, Note = 1 };
