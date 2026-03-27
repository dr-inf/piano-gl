// Copyright (c) 2025-2026 dr-inf (Florian Krohs)
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <string>

namespace midi {

/// MIDI playback configuration
struct MidiPlaybackOptions {
    /// Path to MIDI file (if empty or missing, uses "assets/demo.mid")
    std::string path;

    /// If true, loops playback indefinitely until program exit
    bool loop = false;

    /// If true and file doesn't exist, generates a simple demo MIDI file
    bool writeDemoIfMissing = true;
};

/// Callback for note events: (pitch, velocity)
using NoteCallback = std::function<void(int, int)>;

/// Callback for sustain pedal: (down)
using SustainCallback = std::function<void(bool)>;

/// Launches a detached thread that plays a MIDI file and calls the given callbacks
///
/// @warning The spawned thread is detached and runs until:
///          - The MIDI file completes (if loop=false), or
///          - The program terminates (if loop=true)
/// @warning Ensure callbacks remain valid for the lifetime of the playback thread
/// @warning The callbacks are invoked from a separate thread - use proper synchronization
///
/// @param opts Playback configuration
/// @param on Note-on callback: on(pitch)
/// @param off Note-off callback: off(pitch)
void launchMidiPlayback(
    const MidiPlaybackOptions& opts,
    const std::function<void(int)>& on,
    const std::function<void(int)>& off);

/// Launches a detached thread that plays a MIDI file and calls the given callbacks
///
/// @warning The spawned thread is detached and runs until:
///          - The MIDI file completes (if loop=false), or
///          - The program terminates (if loop=true)
/// @warning Ensure callbacks remain valid for the lifetime of the playback thread
/// @warning The callbacks are invoked from a separate thread - use proper synchronization
///
/// @param opts Playback configuration
/// @param on Note-on callback: on(pitch, velocity)
/// @param off Note-off callback: off(pitch, velocity)
/// @param sustain Sustain pedal callback: sustain(down)
void launchMidiPlayback(
    const MidiPlaybackOptions& opts,
    const NoteCallback& on,
    const NoteCallback& off,
    const SustainCallback& sustain);

} // namespace midi
