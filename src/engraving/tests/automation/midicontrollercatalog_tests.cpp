/* SPDX-License-Identifier: GPL-3.0-only */

#include <gtest/gtest.h>

#include <string>

#include "engraving/automation/midicontrollercatalog.h"

using namespace mu::engraving;

TEST(MidiControllerCatalogTests, ContainsEveryMidiOneControllerExactlyOnce)
{
    ASSERT_EQ(MIDI_CONTROLLER_CATALOG.size(), 128u);
    for (size_t i = 0; i < MIDI_CONTROLLER_CATALOG.size(); ++i) {
        const MidiControllerInfo& info = MIDI_CONTROLLER_CATALOG[i];
        EXPECT_EQ(info.number, i);
        EXPECT_FALSE(info.canonicalName.empty());
        EXPECT_EQ(info.minimum, 0);
        EXPECT_EQ(info.maximum, 127);
        EXPECT_FALSE(info.unit.empty());
        EXPECT_EQ(info.persistenceStrategy, MidiControllerPersistenceStrategy::StableMidiLaneId);
    }
}

TEST(MidiControllerCatalogTests, DescribesFourteenBitPairsWithoutLosingEitherByte)
{
    for (int cc = 0; cc < 32; ++cc) {
        const MidiControllerInfo& msb = MIDI_CONTROLLER_CATALOG[cc];
        const MidiControllerInfo& lsb = MIDI_CONTROLLER_CATALOG[cc + 32];
        EXPECT_EQ(msb.resolution, MidiControllerResolution::FourteenBitMsb);
        EXPECT_EQ(msb.pairedLsb, cc + 32);
        EXPECT_EQ(lsb.resolution, MidiControllerResolution::FourteenBitLsb);
        EXPECT_EQ(lsb.pairedMsb, cc);
    }
    for (int msb : { 99, 101 }) {
        EXPECT_EQ(MIDI_CONTROLLER_CATALOG[msb].resolution, MidiControllerResolution::FourteenBitMsb);
        EXPECT_EQ(MIDI_CONTROLLER_CATALOG[msb].pairedLsb, msb - 1);
        EXPECT_EQ(MIDI_CONTROLLER_CATALOG[msb - 1].resolution, MidiControllerResolution::FourteenBitLsb);
        EXPECT_EQ(MIDI_CONTROLLER_CATALOG[msb - 1].pairedMsb, msb);
    }
}

TEST(MidiControllerCatalogTests, MarksChannelModeAsDiscreteAndRisky)
{
    for (int cc = 120; cc <= 127; ++cc) {
        const MidiControllerInfo& info = MIDI_CONTROLLER_CATALOG[cc];
        EXPECT_EQ(info.category, MidiControllerCategory::ChannelMode);
        EXPECT_EQ(info.behavior, MidiControllerBehavior::ChannelMode);
        EXPECT_NE(info.risk, MidiControllerRisk::None);
    }
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[120].risk, MidiControllerRisk::CanSilenceNotes);
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[121].risk, MidiControllerRisk::ResetsControllers);
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[123].risk, MidiControllerRisk::CanSilenceNotes);
}

TEST(MidiControllerCatalogTests, ClassifiesRequiredControllerFamilies)
{
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[0].category, MidiControllerCategory::BankAndProgram);
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[32].category, MidiControllerCategory::BankAndProgram);
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[48].category, MidiControllerCategory::GeneralPurpose);
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[64].behavior, MidiControllerBehavior::Switch);
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[70].category, MidiControllerCategory::SoundController);
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[91].category, MidiControllerCategory::Effects);
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[96].behavior, MidiControllerBehavior::Trigger);
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[98].category, MidiControllerCategory::ParameterSelection);
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[101].category, MidiControllerCategory::ParameterSelection);
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[122].defaultValue, 127);
}

TEST(MidiControllerCatalogTests, KeepsUndefinedControllersRoundTrippableByNumber)
{
    for (int cc : { 3, 9, 14, 15, 20, 31, 35, 41, 46, 47, 52, 63, 85, 86, 87, 89, 90, 102, 119 }) {
        const MidiControllerInfo& info = MIDI_CONTROLLER_CATALOG[cc];
        EXPECT_EQ(info.category, MidiControllerCategory::Undefined);
        EXPECT_EQ(info.canonicalName, std::string("CC ") + std::to_string(cc));
    }
}

TEST(MidiControllerCatalogTests, AcceptsOnlyCanonicalStableControllerIds)
{
    for (int cc = 0; cc < 128; ++cc) {
        const auto number = midiControllerNumber("cc:" + std::to_string(cc));
        ASSERT_TRUE(number.has_value());
        EXPECT_EQ(*number, cc);
    }
    EXPECT_EQ(midiControllerCanonicalName(128), "Invalid MIDI CC");
    EXPECT_EQ(midiControllerCanonicalName(255), "Invalid MIDI CC");
    for (const auto id : { "", "cc:", "cc:-1", "cc:128", "cc:999", "cc:000", "cc:01", "cc:007", "CC:1", "cc:1x", "cc: 1" }) {
        EXPECT_FALSE(midiControllerNumber(id).has_value()) << id;
    }
}

TEST(MidiControllerCatalogTests, ProtectsCommandsAndSequencesFromCurveEditing)
{
    for (int cc = 0; cc < 128; ++cc) {
        const bool protectedController = cc == 0 || cc == 32 || cc == 6 || cc == 38 || cc == 84 || cc == 88
                                         || (cc >= 96 && cc <= 101) || cc >= 120;
        EXPECT_EQ(MIDI_CONTROLLER_CATALOG[cc].isCurveEditable(), !protectedController) << cc;
        EXPECT_EQ(MIDI_CONTROLLER_CATALOG[cc].isPlaybackRouted(), !protectedController) << cc;
    }
}

TEST(MidiControllerCatalogTests, KeepsEachPairedByteDefaultInItsOwnDomain)
{
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[8].defaultValue, 64);
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[10].defaultValue, 64);
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[40].defaultValue, 0);
    EXPECT_EQ(MIDI_CONTROLLER_CATALOG[42].defaultValue, 0);
    for (const auto& info : MIDI_CONTROLLER_CATALOG) {
        EXPECT_GE(info.defaultValue, info.minimum);
        EXPECT_LE(info.defaultValue, info.maximum);
    }
}
