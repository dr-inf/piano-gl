// Copyright (c) 2025-2026 dr-inf (Florian Krohs)
// SPDX-License-Identifier: MIT

#include "midi_player.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <thread>
#include <vector>
#include <iterator>
#include <string>

namespace midi {

namespace {

enum class MidiEventType {
    NoteOn,
    NoteOff,
    Sustain
};

struct MidiEvent {
    double timeSec = 0.0;
    MidiEventType type = MidiEventType::NoteOn;
    int pitch = 0;
    int velocity = 0;
    bool sustainDown = false;
};

uint32_t readBE32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return (static_cast<uint32_t>(data[offset]) << 24) |
           (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) |
           static_cast<uint32_t>(data[offset + 3]);
}

uint16_t readBE16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return (static_cast<uint16_t>(data[offset]) << 8) | static_cast<uint16_t>(data[offset + 1]);
}

uint32_t readVarLen(const std::vector<std::uint8_t>& data, std::size_t& offset) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        if (offset >= data.size()) break;
        std::uint8_t byte = data[offset++];
        value = (value << 7) | (byte & 0x7F);
        if ((byte & 0x80) == 0) break;
    }
    return value;
}

bool writeDemoMidi(const std::filesystem::path& path) {
    struct TimedMidiMessage {
        uint32_t tick = 0;
        std::uint8_t status = 0;
        std::uint8_t data1 = 0;
        std::uint8_t data2 = 0;
    };

    constexpr uint16_t kDivision = 480;
    constexpr uint32_t kSixteenth = kDivision / 4;
    constexpr uint32_t kQuarter = kDivision;
    constexpr uint32_t kBar = 4 * kQuarter;
    constexpr uint32_t kTempoUsecPerQuarter = 375000; // 160 BPM, close to presto agitato feel.

    std::vector<TimedMidiMessage> messages;
    auto addNote = [&](uint32_t startTick, uint32_t duration, int pitch, int velocity) {
        messages.push_back(TimedMidiMessage{startTick, 0x90, static_cast<std::uint8_t>(pitch), static_cast<std::uint8_t>(velocity)});
        messages.push_back(TimedMidiMessage{startTick + duration, 0x80, static_cast<std::uint8_t>(pitch), 0x40});
    };
    auto addBassPulse = [&](uint32_t startTick, int pitch) {
        addNote(startTick, kQuarter - 24, pitch, 88);
    };
    auto addArpeggioBar = [&](uint32_t barStart, std::initializer_list<int> pitches) {
        uint32_t tick = barStart;
        for (int pitch : pitches) {
            addNote(tick, kSixteenth - 8, pitch, 96);
            tick += kSixteenth;
        }
    };

    // A short c-sharp-minor arpeggio study inspired by the opening energy of
    // Beethoven's Moonlight Sonata, 3rd movement, but compact enough for a looping demo.
    addArpeggioBar(0 * kBar, {
        61, 68, 73, 76, 68, 73, 76, 80,
        68, 73, 76, 80, 68, 73, 76, 80,
    });
    addBassPulse(0 * kBar + 0 * kQuarter, 49);
    addBassPulse(0 * kBar + 1 * kQuarter, 56);
    addBassPulse(0 * kBar + 2 * kQuarter, 49);
    addBassPulse(0 * kBar + 3 * kQuarter, 56);

    addArpeggioBar(1 * kBar, {
        61, 69, 73, 76, 69, 73, 76, 81,
        69, 73, 76, 81, 69, 73, 76, 81,
    });
    addBassPulse(1 * kBar + 0 * kQuarter, 45);
    addBassPulse(1 * kBar + 1 * kQuarter, 52);
    addBassPulse(1 * kBar + 2 * kQuarter, 45);
    addBassPulse(1 * kBar + 3 * kQuarter, 52);

    addArpeggioBar(2 * kBar, {
        61, 66, 73, 78, 66, 73, 78, 81,
        66, 73, 78, 81, 66, 73, 78, 81,
    });
    addBassPulse(2 * kBar + 0 * kQuarter, 42);
    addBassPulse(2 * kBar + 1 * kQuarter, 49);
    addBassPulse(2 * kBar + 2 * kQuarter, 42);
    addBassPulse(2 * kBar + 3 * kQuarter, 49);

    addArpeggioBar(3 * kBar, {
        63, 68, 71, 77, 68, 71, 77, 80,
        68, 71, 77, 80, 68, 71, 77, 80,
    });
    addBassPulse(3 * kBar + 0 * kQuarter, 44);
    addBassPulse(3 * kBar + 1 * kQuarter, 51);
    addBassPulse(3 * kBar + 2 * kQuarter, 44);
    addBassPulse(3 * kBar + 3 * kQuarter, 51);

    std::sort(messages.begin(), messages.end(), [](const TimedMidiMessage& a, const TimedMidiMessage& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return a.status < b.status;
    });

    std::vector<std::uint8_t> track;
    auto appendVarLen = [&](uint32_t value) {
        std::uint8_t buffer[4]{};
        int count = 0;
        buffer[count++] = static_cast<std::uint8_t>(value & 0x7F);
        while ((value >>= 7) != 0) {
            buffer[count++] = static_cast<std::uint8_t>(0x80 | (value & 0x7F));
        }
        while (count-- > 0) {
            track.push_back(buffer[count]);
        }
    };
    auto appendBE32 = [](std::vector<std::uint8_t>& out, uint32_t value) {
        out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    };
    auto appendBE16 = [](std::vector<std::uint8_t>& out, uint16_t value) {
        out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    };

    track.insert(track.end(), {0x00, 0xFF, 0x51, 0x03,
        static_cast<std::uint8_t>((kTempoUsecPerQuarter >> 16) & 0xFF),
        static_cast<std::uint8_t>((kTempoUsecPerQuarter >> 8) & 0xFF),
        static_cast<std::uint8_t>(kTempoUsecPerQuarter & 0xFF)});

    uint32_t lastTick = 0;
    for (const auto& msg : messages) {
        appendVarLen(msg.tick - lastTick);
        track.push_back(msg.status);
        track.push_back(msg.data1);
        track.push_back(msg.data2);
        lastTick = msg.tick;
    }
    track.insert(track.end(), {0x00, 0xFF, 0x2F, 0x00});

    std::vector<std::uint8_t> file;
    file.insert(file.end(), {'M', 'T', 'h', 'd'});
    appendBE32(file, 6);
    appendBE16(file, 0);
    appendBE16(file, 1);
    appendBE16(file, kDivision);
    file.insert(file.end(), {'M', 'T', 'r', 'k'});
    appendBE32(file, static_cast<uint32_t>(track.size()));
    file.insert(file.end(), track.begin(), track.end());

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "[MIDI] Failed to write demo MIDI to " << path << "\n";
        return false;
    }
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return true;
}

bool parseFormat0(const std::vector<std::uint8_t>& data, std::vector<MidiEvent>& outEvents) {
    if (data.size() < 22) return false;
    if (std::string(reinterpret_cast<const char*>(data.data()), 4) != "MThd") return false;
    if (readBE32(data, 4) != 6) return false;
    uint16_t format = readBE16(data, 8);
    uint16_t nTracks = readBE16(data, 10);
    uint16_t division = readBE16(data, 12);
    if (format != 0 || nTracks == 0) {
        std::cerr << "[MIDI] Only format 0 demo playback is supported.\n";
        return false;
    }
    if (division & 0x8000) {
        std::cerr << "[MIDI] SMPTE time division not supported.\n";
        return false;
    }

    std::size_t offset = 14;
    if (offset + 8 > data.size()) return false;
    if (std::string(reinterpret_cast<const char*>(&data[offset]), 4) != "MTrk") return false;
    uint32_t trackLen = readBE32(data, offset + 4);
    offset += 8;
    if (offset + trackLen > data.size()) return false;
    std::size_t trackEnd = offset + trackLen;

    double secondsPerTick = 500000.0 / 1'000'000.0 / static_cast<double>(division); // default 120 bpm
    double curTime = 0.0;
    std::uint8_t runningStatus = 0;

    while (offset < trackEnd) {
        uint32_t delta = readVarLen(data, offset);
        curTime += static_cast<double>(delta) * secondsPerTick;
        if (offset >= trackEnd) break;
        std::uint8_t status = data[offset++];
        if (status == 0xFF) {
            if (offset >= trackEnd) break;
            std::uint8_t type = data[offset++];
            uint32_t len = readVarLen(data, offset);
            if (offset + len > trackEnd) break;
            if (type == 0x51 && len == 3) {
                uint32_t tempo = (static_cast<uint32_t>(data[offset]) << 16) |
                                 (static_cast<uint32_t>(data[offset + 1]) << 8) |
                                 static_cast<uint32_t>(data[offset + 2]);
                secondsPerTick = static_cast<double>(tempo) / 1'000'000.0 / static_cast<double>(division);
            }
            offset += len;
            continue;
        } else if (status == 0xF0 || status == 0xF7) {
            uint32_t len = readVarLen(data, offset);
            offset += len;
            continue;
        } else if (status < 0x80) {
            if (runningStatus == 0) continue;
            --offset;
            status = runningStatus;
        } else {
            runningStatus = status;
        }

        std::uint8_t type = status & 0xF0;
        if (type == 0x90 || type == 0x80) {
            if (offset + 2 > trackEnd) break;
            int note = data[offset++];
            int vel = data[offset++];
            MidiEvent ev{};
            ev.timeSec = curTime;
            ev.pitch = note;
            ev.velocity = vel;
            ev.type = (type == 0x90 && vel > 0) ? MidiEventType::NoteOn : MidiEventType::NoteOff;
            outEvents.push_back(ev);
        } else if (type == 0xB0) {
            if (offset + 2 > trackEnd) break;
            int controller = data[offset++];
            int value = data[offset++];
            if (controller == 64) {
                MidiEvent ev{};
                ev.timeSec = curTime;
                ev.type = MidiEventType::Sustain;
                ev.sustainDown = value >= 64;
                outEvents.push_back(ev);
            }
        } else {
            int dataBytes = (type == 0xC0 || type == 0xD0) ? 1 : 2;
            offset += dataBytes;
        }
    }
    std::sort(outEvents.begin(), outEvents.end(), [](const MidiEvent& a, const MidiEvent& b) {
        return a.timeSec < b.timeSec;
    });
    return !outEvents.empty();
}

} // namespace

void launchMidiPlayback(
    const MidiPlaybackOptions& opts,
    const NoteCallback& on,
    const NoteCallback& off,
    const SustainCallback& sustain) {

    std::filesystem::path midiPath = opts.path.empty() ? std::filesystem::path("assets/demo.mid") : std::filesystem::path(opts.path);
    if (!std::filesystem::exists(midiPath) && opts.writeDemoIfMissing) {
        if (!midiPath.parent_path().empty()) {
            std::filesystem::create_directories(midiPath.parent_path());
        }
        writeDemoMidi(midiPath);
    }

    std::vector<std::uint8_t> bytes;
    {
        std::ifstream in(midiPath, std::ios::binary);
        if (!in) {
            std::cerr << "[MIDI] Failed to open " << midiPath << "\n";
            return;
        }
        in.unsetf(std::ios::skipws);
        bytes.insert(bytes.begin(), std::istream_iterator<std::uint8_t>(in), std::istream_iterator<std::uint8_t>());
    }

    std::vector<MidiEvent> events;
    if (!parseFormat0(bytes, events)) {
        std::cerr << "[MIDI] Could not parse MIDI (format 0 expected).\n";
        return;
    }

    std::thread([events, on, off, sustain, loop = opts.loop]() {
        do {
            double lastTime = 0.0;
            for (const auto& ev : events) {
                double wait = ev.timeSec - lastTime;
                if (wait > 0.0) {
                    std::this_thread::sleep_for(std::chrono::duration<double>(wait));
                }
                lastTime = ev.timeSec;
                switch (ev.type) {
                    case MidiEventType::NoteOn:
                        if (on) on(ev.pitch, ev.velocity);
                        break;
                    case MidiEventType::NoteOff:
                        if (off) off(ev.pitch, ev.velocity);
                        break;
                    case MidiEventType::Sustain:
                        if (sustain) sustain(ev.sustainDown);
                        break;
                }
            }
        } while (loop);
    }).detach();
}

void launchMidiPlayback(
    const MidiPlaybackOptions& opts,
    const std::function<void(int)>& on,
    const std::function<void(int)>& off) {
    launchMidiPlayback(
        opts,
        [&](int pitch, int) { if (on) on(pitch); },
        [&](int pitch, int) { if (off) off(pitch); },
        SustainCallback{});
}

} // namespace midi
