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
