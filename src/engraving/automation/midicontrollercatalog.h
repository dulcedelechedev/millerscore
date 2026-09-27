/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace mu::engraving {

enum class MidiControllerCategory : uint8_t {
    BankAndProgram,
    Performance,
    GeneralPurpose,
    PedalAndSwitch,
    SoundController,
    Effects,
    DataEntry,
    ParameterSelection,
    Undefined,
    ChannelMode,
};

enum class MidiControllerResolution : uint8_t {
    SevenBit,
    FourteenBitMsb,
    FourteenBitLsb,
};

enum class MidiControllerBehavior : uint8_t {
    Continuous,
    Switch,
    Stepped,
    Trigger,
    ChannelMode,
};

enum class MidiControllerRisk : uint8_t {
    None,
    ParameterState,
    CanSilenceNotes,
    ResetsControllers,
    ChangesChannelMode,
};

enum class MidiControllerIoStrategy : uint8_t {
    PreserveOrderedEvent,
    PreserveSemanticParameterSequence,
};

enum class MidiControllerPersistenceStrategy : uint8_t {
    StableMidiLaneId,
};

struct MidiControllerInfo {
    uint8_t number = 0;
    std::string_view canonicalName;
    MidiControllerCategory category = MidiControllerCategory::Undefined;
    int defaultValue = 0;
    uint8_t minimum = 0;
    uint8_t maximum = 127;
    std::string_view unit = "MIDI value";
    MidiControllerResolution resolution = MidiControllerResolution::SevenBit;
    int8_t pairedMsb = -1;
    int8_t pairedLsb = -1;
    MidiControllerBehavior behavior = MidiControllerBehavior::Continuous;
    MidiControllerRisk risk = MidiControllerRisk::None;
    MidiControllerIoStrategy importStrategy = MidiControllerIoStrategy::PreserveOrderedEvent;
    MidiControllerIoStrategy exportStrategy = MidiControllerIoStrategy::PreserveOrderedEvent;
    MidiControllerPersistenceStrategy persistenceStrategy = MidiControllerPersistenceStrategy::StableMidiLaneId;

    constexpr bool isDefined() const { return category != MidiControllerCategory::Undefined; }
    constexpr bool isChannelMode() const { return behavior == MidiControllerBehavior::ChannelMode; }
};

constexpr std::string_view midiControllerCanonicalName(uint8_t cc)
{
    constexpr std::array<std::string_view, 128> NAMES {
        "Bank Select", "Modulation Wheel", "Breath Controller", "CC 3", "Foot Controller", "Portamento Time",
        "Data Entry", "Channel Volume", "Balance", "CC 9", "Pan", "Expression Controller", "Effect Control 1",
        "Effect Control 2", "CC 14", "CC 15", "General Purpose Controller 1", "General Purpose Controller 2",
        "General Purpose Controller 3", "General Purpose Controller 4", "CC 20", "CC 21", "CC 22", "CC 23",
        "CC 24", "CC 25", "CC 26", "CC 27", "CC 28", "CC 29", "CC 30", "CC 31",
        "Bank Select LSB", "Modulation Wheel LSB", "Breath Controller LSB", "CC 35", "Foot Controller LSB",
        "Portamento Time LSB", "Data Entry LSB", "Channel Volume LSB", "Balance LSB", "CC 41", "Pan LSB",
        "Expression Controller LSB", "Effect Control 1 LSB", "Effect Control 2 LSB", "CC 46", "CC 47",
        "General Purpose Controller 1 LSB", "General Purpose Controller 2 LSB", "General Purpose Controller 3 LSB",
        "General Purpose Controller 4 LSB", "CC 52", "CC 53", "CC 54", "CC 55", "CC 56", "CC 57",
        "CC 58", "CC 59", "CC 60", "CC 61", "CC 62", "CC 63",
        "Damper Pedal (Sustain)", "Portamento On/Off", "Sostenuto", "Soft Pedal", "Legato Footswitch", "Hold 2",
        "Sound Controller 1 (Sound Variation)", "Sound Controller 2 (Timbre/Harmonic Intensity)",
        "Sound Controller 3 (Release Time)", "Sound Controller 4 (Attack Time)",
        "Sound Controller 5 (Brightness)", "Sound Controller 6 (Decay Time)",
        "Sound Controller 7 (Vibrato Rate)", "Sound Controller 8 (Vibrato Depth)",
        "Sound Controller 9 (Vibrato Delay)", "Sound Controller 10", "General Purpose Controller 5",
        "General Purpose Controller 6", "General Purpose Controller 7", "General Purpose Controller 8",
        "Portamento Control", "CC 85", "CC 86", "CC 87", "High Resolution Velocity Prefix", "CC 89", "CC 90",
        "Effects 1 Depth (Reverb Send)", "Effects 2 Depth (Tremolo)", "Effects 3 Depth (Chorus Send)",
        "Effects 4 Depth (Celeste/Detune)", "Effects 5 Depth (Phaser)", "Data Increment", "Data Decrement",
        "NRPN LSB", "NRPN MSB", "RPN LSB", "RPN MSB", "CC 102", "CC 103", "CC 104", "CC 105", "CC 106",
        "CC 107", "CC 108", "CC 109", "CC 110", "CC 111", "CC 112", "CC 113", "CC 114", "CC 115",
        "CC 116", "CC 117", "CC 118", "CC 119", "All Sound Off", "Reset All Controllers", "Local Control On/Off",
        "All Notes Off", "Omni Mode Off", "Omni Mode On", "Mono Mode On", "Poly Mode On"
    };
    return NAMES[cc];
}

constexpr MidiControllerInfo makeMidiControllerInfo(uint8_t cc)
{
    MidiControllerInfo result;
    result.number = cc;
    result.canonicalName = midiControllerCanonicalName(cc);

    if (cc <= 31) {
        result.resolution = MidiControllerResolution::FourteenBitMsb;
        result.pairedLsb = static_cast<int8_t>(cc + 32);
    } else if (cc <= 63) {
        result.resolution = MidiControllerResolution::FourteenBitLsb;
        result.pairedMsb = static_cast<int8_t>(cc - 32);
    }

    if (cc == 0 || cc == 32) {
        result.category = MidiControllerCategory::BankAndProgram;
        result.behavior = MidiControllerBehavior::Stepped;
    } else if (cc == 6 || cc == 38 || (cc >= 96 && cc <= 97)) {
        result.category = MidiControllerCategory::DataEntry;
        result.behavior = cc >= 96 ? MidiControllerBehavior::Trigger : MidiControllerBehavior::Stepped;
        result.risk = MidiControllerRisk::ParameterState;
        result.importStrategy = MidiControllerIoStrategy::PreserveSemanticParameterSequence;
        result.exportStrategy = MidiControllerIoStrategy::PreserveSemanticParameterSequence;
    } else if (cc >= 98 && cc <= 101) {
        result.category = MidiControllerCategory::ParameterSelection;
        result.behavior = MidiControllerBehavior::Stepped;
        result.risk = MidiControllerRisk::ParameterState;
        result.importStrategy = MidiControllerIoStrategy::PreserveSemanticParameterSequence;
        result.exportStrategy = MidiControllerIoStrategy::PreserveSemanticParameterSequence;
    } else if ((cc >= 64 && cc <= 69)) {
        result.category = MidiControllerCategory::PedalAndSwitch;
        result.defaultValue = 0;
        result.behavior = MidiControllerBehavior::Switch;
        result.unit = "Off/On";
    } else if (cc >= 70 && cc <= 79) {
        result.category = MidiControllerCategory::SoundController;
        result.defaultValue = 64;
    } else if ((cc >= 16 && cc <= 19) || (cc >= 48 && cc <= 51) || (cc >= 80 && cc <= 83)) {
        result.category = MidiControllerCategory::GeneralPurpose;
    } else if (cc == 12 || cc == 13 || cc == 44 || cc == 45 || (cc >= 91 && cc <= 95)) {
        result.category = MidiControllerCategory::Effects;
    } else if (cc >= 120) {
        result.category = MidiControllerCategory::ChannelMode;
        result.behavior = MidiControllerBehavior::ChannelMode;
        result.unit = "Command";
        result.resolution = MidiControllerResolution::SevenBit;
        result.pairedMsb = -1;
        result.pairedLsb = -1;
        if (cc == 120 || cc == 123) {
            result.risk = MidiControllerRisk::CanSilenceNotes;
        } else if (cc == 121) {
            result.risk = MidiControllerRisk::ResetsControllers;
        } else {
            result.risk = MidiControllerRisk::ChangesChannelMode;
        }
    } else {
        switch (cc) {
        case 1: case 2: case 4: case 5: case 7: case 8: case 10: case 11:
        case 33: case 34: case 36: case 37: case 39: case 40: case 42: case 43:
        case 84: case 88:
            result.category = MidiControllerCategory::Performance;
            break;
        default:
            result.category = MidiControllerCategory::Undefined;
            break;
        }
    }

    if (cc == 8 || cc == 10 || cc == 40 || cc == 42) {
        result.defaultValue = 64;
    }
    if (cc == 122) {
        result.defaultValue = 127;
    }
    return result;
}

constexpr std::array<MidiControllerInfo, 128> makeMidiControllerCatalog()
{
    std::array<MidiControllerInfo, 128> result {};
    for (uint16_t i = 0; i < result.size(); ++i) {
        result[i] = makeMidiControllerInfo(static_cast<uint8_t>(i));
    }
    return result;
}

inline constexpr std::array<MidiControllerInfo, 128> MIDI_CONTROLLER_CATALOG = makeMidiControllerCatalog();

} // namespace mu::engraving
