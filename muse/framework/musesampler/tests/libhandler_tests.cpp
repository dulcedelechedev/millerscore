/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include <functional>
#include <gtest/gtest.h>
#include <QtGlobal>
#include <QFile>
#include <QTemporaryDir>

#include "musesampler/internal/libhandler.h"

using namespace muse;
using namespace muse::musesampler;

namespace {
struct FakeLibrary {
    bool available = true;
    std::string missingExport;
    int major = 0;
    int minor = 105;
    int revision = 0;
    ms_Result initResult = ms_Result_OK;
    ms_Result deinitResult = ms_Result_OK;
    ms_Result samplerResult = ms_Result_OK;
    int loads = 0;
    int closes = 0;
    int initializations = 0;
    int shutdowns = 0;
    int samplerInitializations = 0;
    int noteEvents = 0;
    int auditionEvents = 0;
};

FakeLibrary* activeLibrary = nullptr;

MuseSamplerLib fakeLoad(const io::path_t&)
{
    ++activeLibrary->loads;
    return activeLibrary->available ? activeLibrary : nullptr;
}

void fakeClose(MuseSamplerLib handle)
{
    ++static_cast<FakeLibrary*>(handle)->closes;
}

int fakeMajor() { return activeLibrary->major; }
int fakeMinor() { return activeLibrary->minor; }
int fakeRevision() { return activeLibrary->revision; }
ms_Result fakeInit() { ++activeLibrary->initializations; return activeLibrary->initResult; }
ms_Result fakeDeinit() { ++activeLibrary->shutdowns; return activeLibrary->deinitResult; }
ms_Result fakeDisableReverb() { return ms_Result_OK; }
void unusedExport() {}

ms_Result fakeSamplerInit(ms_MuseSampler, double, int, int)
{
    ++activeLibrary->samplerInitializations;
    return activeLibrary->samplerResult;
}

ms_Result fakeNoteEvent(ms_MuseSampler, ms_Track, NoteEvent, long long& eventId)
{
    ++activeLibrary->noteEvents;
    eventId = 42;
    return activeLibrary->samplerResult;
}

ms_Result fakeAuditionEvent(ms_MuseSampler, ms_Track, ms_AuditionStartNoteEvent_5)
{
    ++activeLibrary->auditionEvents;
    return activeLibrary->samplerResult;
}

void* fakeResolve(MuseSamplerLib, const char* name)
{
    if (activeLibrary->missingExport == name) {
        return nullptr;
    }
    if (std::strcmp(name, "ms_get_version_major") == 0) { return reinterpret_cast<void*>(fakeMajor); }
    if (std::strcmp(name, "ms_get_version_minor") == 0) { return reinterpret_cast<void*>(fakeMinor); }
    if (std::strcmp(name, "ms_get_version_revision") == 0) { return reinterpret_cast<void*>(fakeRevision); }
    if (std::strcmp(name, "ms_get_version_build_number") == 0) { return nullptr; }
    if (std::strcmp(name, "ms_init_2") == 0) { return reinterpret_cast<void*>(fakeInit); }
    if (std::strcmp(name, "ms_deinit") == 0) { return reinterpret_cast<void*>(fakeDeinit); }
    if (std::strcmp(name, "ms_disable_reverb") == 0) { return reinterpret_cast<void*>(fakeDisableReverb); }
    if (std::strcmp(name, "ms_MuseSampler_init_2") == 0 || std::strcmp(name, "ms_MuseSampler_init") == 0) {
        return reinterpret_cast<void*>(fakeSamplerInit);
    }
    if (std::strcmp(name, "ms_MuseSampler_add_track_note_event_6") == 0) { return reinterpret_cast<void*>(fakeNoteEvent); }
    if (std::strcmp(name, "ms_MuseSampler_start_audition_note_5") == 0) { return reinterpret_cast<void*>(fakeAuditionEvent); }
    // These remaining exports are checked for presence, never called by the tests.
    return reinterpret_cast<void*>(unusedExport);
}

const MuseSamplerLibHandler::LibraryLoader fakeLoader { fakeLoad, fakeResolve, fakeClose };
const Version minimumVersion(0, 105, 0);

class MuseSamplerLibHandlerTests : public ::testing::Test
{
protected:
    void SetUp() override { activeLibrary = &library; }
    void TearDown() override { activeLibrary = nullptr; }
    FakeLibrary library;
};
}

TEST_F(MuseSamplerLibHandlerTests, AbsentOptionalLibraryIsNotValid)
{
    library.available = false;
    MuseSamplerLibHandler handler(fakeLoader);
    EXPECT_FALSE(handler.loadLib("optional-library"));
    EXPECT_FALSE(handler.isValid());
    EXPECT_FALSE(handler.init());
    EXPECT_EQ(library.initializations, 0);
    EXPECT_EQ(library.closes, 0);
}

TEST_F(MuseSamplerLibHandlerTests, CompatibleApiInitializesAndClosesOnce)
{
    {
        MuseSamplerLibHandler handler(fakeLoader);
        ASSERT_TRUE(handler.loadLib("optional-library"));
        ASSERT_TRUE(handler.loadApi(minimumVersion));
        EXPECT_EQ(handler.buildNumber(), -1);
        EXPECT_TRUE(handler.init());
        EXPECT_TRUE(handler.init());
        handler.deinit();
        handler.deinit();
        EXPECT_FALSE(handler.isValid());
    }
    EXPECT_EQ(library.initializations, 1);
    EXPECT_EQ(library.shutdowns, 1);
    EXPECT_EQ(library.closes, 1);
}

TEST_F(MuseSamplerLibHandlerTests, DestroyingLiveHandlerShutsDownLibrary)
{
    {
        MuseSamplerLibHandler handler(fakeLoader);
        ASSERT_TRUE(handler.loadLib("optional-library"));
        ASSERT_TRUE(handler.loadApi(minimumVersion));
        ASSERT_TRUE(handler.init());
    }
    EXPECT_EQ(library.shutdowns, 1);
    EXPECT_EQ(library.closes, 1);
}

TEST_F(MuseSamplerLibHandlerTests, FailedInitializationClosesWithoutShutdown)
{
    library.initResult = ms_Result_Error;
    {
        MuseSamplerLibHandler handler(fakeLoader);
        ASSERT_TRUE(handler.loadLib("optional-library"));
        ASSERT_TRUE(handler.loadApi(minimumVersion));
        EXPECT_FALSE(handler.init());
    }
    EXPECT_EQ(library.initializations, 1);
    EXPECT_EQ(library.shutdowns, 0);
    EXPECT_EQ(library.closes, 1);
}

TEST_F(MuseSamplerLibHandlerTests, UnsupportedVersionClosesWithoutInitialization)
{
    for (const auto& version : { std::pair { 0, 104 }, std::pair { 1, 105 } }) {
        library.major = version.first;
        library.minor = version.second;
        {
            MuseSamplerLibHandler handler(fakeLoader);
            ASSERT_TRUE(handler.loadLib("optional-library"));
            EXPECT_FALSE(handler.loadApi(minimumVersion));
            EXPECT_FALSE(handler.isValid());
            EXPECT_FALSE(handler.init());
        }
    }
    EXPECT_EQ(library.initializations, 0);
    EXPECT_EQ(library.shutdowns, 0);
    EXPECT_EQ(library.closes, 2);
}

TEST_F(MuseSamplerLibHandlerTests, MissingVersionExportsRejectApi)
{
    for (const auto* name : { "ms_get_version_major", "ms_get_version_minor", "ms_get_version_revision" }) {
        library.missingExport = name;
        MuseSamplerLibHandler handler(fakeLoader);
        ASSERT_TRUE(handler.loadLib("optional-library"));
        EXPECT_FALSE(handler.loadApi(minimumVersion));
        EXPECT_FALSE(handler.isValid());
    }
    EXPECT_EQ(library.shutdowns, 0);
    EXPECT_EQ(library.closes, 3);
}

TEST_F(MuseSamplerLibHandlerTests, MissingWrappedExportsRejectApi)
{
    for (const auto* name : { "ms_MuseSampler_init_2", "ms_MuseSampler_add_track_note_event_6",
                             "ms_MuseSampler_start_audition_note_5" }) {
        SCOPED_TRACE(name);
        library.missingExport = name;
        MuseSamplerLibHandler handler(fakeLoader);
        ASSERT_TRUE(handler.loadLib("optional-library"));
        EXPECT_FALSE(handler.loadApi(minimumVersion));
        EXPECT_FALSE(handler.isValid());
        EXPECT_FALSE(handler.init());
    }
    EXPECT_EQ(library.initializations, 0);
    EXPECT_EQ(library.closes, 3);
}

TEST_F(MuseSamplerLibHandlerTests, MissingLegacyInitializerRejectsApi)
{
    library.missingExport = "ms_MuseSampler_init";
    MuseSamplerLibHandler handler(fakeLoader);
    ASSERT_TRUE(handler.loadLib("optional-library"));
    EXPECT_FALSE(handler.loadApi(minimumVersion, true));
    EXPECT_FALSE(handler.isValid());
}

TEST_F(MuseSamplerLibHandlerTests, WrappedExportsPropagateSuccessAndFailure)
{
    for (const bool legacy : { false, true }) {
        MuseSamplerLibHandler handler(fakeLoader);
        ASSERT_TRUE(handler.loadLib("optional-library"));
        ASSERT_TRUE(handler.loadApi(minimumVersion, legacy));
        for (const ms_Result result : { ms_Result_OK, ms_Result_Error }) {
            library.samplerResult = result;
            const bool success = result == ms_Result_OK;
            long long eventId = 0;
            EXPECT_EQ(handler.initSampler(nullptr, 48000, 512, 2), success);
            EXPECT_EQ(handler.addNoteEvent(nullptr, nullptr, {}, eventId), success);
            EXPECT_EQ(eventId, 42);
            EXPECT_EQ(handler.startAuditionNote(nullptr, nullptr, {}), success);
        }
    }
    EXPECT_EQ(library.samplerInitializations, 4);
    EXPECT_EQ(library.noteEvents, 4);
    EXPECT_EQ(library.auditionEvents, 4);
}

TEST_F(MuseSamplerLibHandlerTests, ShutdownFailureStillClosesLibrary)
{
    library.deinitResult = ms_Result_Error;
    {
        MuseSamplerLibHandler handler(fakeLoader);
        ASSERT_TRUE(handler.loadLib("optional-library"));
        ASSERT_TRUE(handler.loadApi(minimumVersion));
        ASSERT_TRUE(handler.init());
        handler.deinit();
        EXPECT_FALSE(handler.isValid());
    }
    EXPECT_EQ(library.shutdowns, 1);
    EXPECT_EQ(library.closes, 1);
}

TEST_F(MuseSamplerLibHandlerTests, ClosingUninitializedApiDoesNotCallShutdown)
{
    MuseSamplerLibHandler handler(fakeLoader);
    ASSERT_TRUE(handler.loadLib("optional-library"));
    ASSERT_TRUE(handler.loadApi(minimumVersion));
    handler.deinit();
    EXPECT_EQ(library.shutdowns, 0);
    EXPECT_EQ(library.closes, 1);
}

TEST_F(MuseSamplerLibHandlerTests, CannotReplaceLiveLibraryOrInitializedApi)
{
    MuseSamplerLibHandler handler(fakeLoader);
    ASSERT_TRUE(handler.loadLib("optional-library"));
    ASSERT_TRUE(handler.loadApi(minimumVersion));
    ASSERT_TRUE(handler.init());
    EXPECT_FALSE(handler.loadLib("replacement-library"));
    EXPECT_FALSE(handler.loadApi(minimumVersion));
    EXPECT_TRUE(handler.isValid());
    EXPECT_EQ(library.loads, 1);
    EXPECT_EQ(library.initializations, 1);
}

TEST_F(MuseSamplerLibHandlerTests, FailedApiReloadInvalidatesPreviousExports)
{
    MuseSamplerLibHandler handler(fakeLoader);
    ASSERT_TRUE(handler.loadLib("optional-library"));
    ASSERT_TRUE(handler.loadApi(minimumVersion));
    library.missingExport = "ms_MuseSampler_init_2";
    EXPECT_FALSE(handler.loadApi(minimumVersion));
    EXPECT_FALSE(handler.isValid());
    EXPECT_FALSE(handler.init());
    EXPECT_EQ(library.initializations, 0);
}

TEST_F(MuseSamplerLibHandlerTests, ReopeningLibraryRequiresFreshApiValidation)
{
    MuseSamplerLibHandler handler(fakeLoader);
    ASSERT_TRUE(handler.loadLib("optional-library"));
    ASSERT_TRUE(handler.loadApi(minimumVersion));
    handler.deinit();
    ASSERT_TRUE(handler.loadLib("replacement-library"));
    EXPECT_FALSE(handler.isValid());
    EXPECT_FALSE(handler.init());
    EXPECT_EQ(handler.buildNumber(), -1);
    EXPECT_TRUE(handler.loadApi(minimumVersion));
    EXPECT_TRUE(handler.init());
}

TEST_F(MuseSamplerLibHandlerTests, SharedSamplerOwnershipDefersLibraryShutdown)
{
    auto resolverOwner = std::make_shared<MuseSamplerLibHandler>(fakeLoader);
    ASSERT_TRUE(resolverOwner->loadLib("optional-library"));
    ASSERT_TRUE(resolverOwner->loadApi(minimumVersion));
    ASSERT_TRUE(resolverOwner->init());
    auto samplerOwner = resolverOwner;
    resolverOwner.reset();
    EXPECT_EQ(library.shutdowns, 0);
    EXPECT_TRUE(samplerOwner->isValid());
    samplerOwner.reset();
    EXPECT_EQ(library.shutdowns, 1);
    EXPECT_EQ(library.closes, 1);
}

TEST_F(MuseSamplerLibHandlerTests, InstrumentReloadExportIsOptional)
{
    library.missingExport = "ms_reload_all_instruments";
    MuseSamplerLibHandler handler(fakeLoader);
    ASSERT_TRUE(handler.loadLib("optional-library"));
    ASSERT_TRUE(handler.loadApi(minimumVersion));
    EXPECT_EQ(handler.reloadAllInstruments, nullptr);
    EXPECT_TRUE(handler.init());
}

TEST(MuseSamplerNativeLoaderTests, MissingOptionalLibraryDoesNotLoad)
{
    QTemporaryDir fixture;
    ASSERT_TRUE(fixture.isValid());
    MuseSamplerLibHandler handler;
    EXPECT_FALSE(handler.loadLib(io::path_t(fixture.path() + "/missing-optional-sampler.dll")));
    EXPECT_FALSE(handler.isValid());
    EXPECT_FALSE(handler.init());
}

TEST(MuseSamplerNativeLoaderTests, CorruptLibraryAtUnicodePathDoesNotLoad)
{
    QTemporaryDir fixture(QDir::tempPath() + "/millerscore sampler Å 测试-XXXXXX");
    ASSERT_TRUE(fixture.isValid());
    const QString filePath = fixture.path() + "/MuseSamplerCoreLib.dll";
    QFile file(filePath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write("Invalid library fixture"), 23);
    file.close();
    MuseSamplerLibHandler handler;
    EXPECT_FALSE(handler.loadLib(io::path_t(filePath)));
    EXPECT_FALSE(handler.isValid());
    EXPECT_FALSE(handler.init());
}

class MuseSamplerRequiredExportTests : public MuseSamplerLibHandlerTests, public ::testing::WithParamInterface<const char*>
{
};

TEST_P(MuseSamplerRequiredExportTests, PartialLibraryIsRejectedBeforeInitialization)
{
    library.missingExport = GetParam();
    {
        MuseSamplerLibHandler handler(fakeLoader);
        ASSERT_TRUE(handler.loadLib("optional-library"));
        EXPECT_FALSE(handler.loadApi(minimumVersion));
        EXPECT_FALSE(handler.isValid());
        EXPECT_FALSE(handler.init());
    }
    EXPECT_EQ(library.initializations, 0);
    EXPECT_EQ(library.shutdowns, 0);
    EXPECT_EQ(library.closes, 1);
}

INSTANTIATE_TEST_SUITE_P(RequiredApi, MuseSamplerRequiredExportTests, ::testing::Values(
                            "ms_get_version_major", "ms_get_version_minor", "ms_get_version_revision",
                            "ms_init_2", "ms_deinit", "ms_get_instrument_list", "ms_get_matching_instrument_list",
                            "ms_InstrumentList_get_next", "ms_Instrument_get_id", "ms_Instrument_get_name",
                            "ms_Instrument_get_category", "ms_Instrument_get_musicxml_sound", "ms_Instrument_get_mpe_sound",
                            "ms_Instrument_get_preset_list", "ms_PresetList_get_next", "ms_MuseSampler_create", "ms_MuseSampler_destroy",
                            "ms_MuseSampler_init_2", "ms_MuseSampler_add_track", "ms_MuseSampler_finalize_track",
                            "ms_MuseSampler_clear_track", "ms_disable_reverb", "ms_Instrument_get_reverb_level",
                            "ms_MuseSampler_add_track_dynamics_event_2", "ms_MuseSampler_add_track_pedal_event_2",
                            "ms_MuseSampler_add_track_note_event_6", "ms_MuseSampler_start_audition_note_5",
                            "ms_MuseSampler_is_ranged_articulation", "ms_MuseSampler_add_track_event_range_start",
                            "ms_MuseSampler_add_track_event_range_end", "ms_MuseSampler_stop_audition_note",
                            "ms_MuseSampler_add_track_syllable_event_2", "ms_Instrument_get_vendor_name", "ms_Instrument_get_pack_name",
                            "ms_Instrument_get_info_json", "ms_MuseSampler_create_preset_change", "ms_MuseSampler_add_preset",
                            "ms_get_text_articulations", "ms_MuseSampler_add_track_text_articulation_event", "ms_get_drum_mapping",
                            "ms_MuseSampler_add_pitch_bend", "ms_MuseSampler_add_vibrato", "ms_MuseSampler_start_offline_mode",
                            "ms_MuseSampler_stop_offline_mode", "ms_MuseSampler_process_offline", "ms_MuseSampler_set_position",
                            "ms_MuseSampler_set_playing", "ms_MuseSampler_process", "ms_MuseSampler_all_notes_off",
                            "ms_MuseSampler_ready_to_play", "ms_set_logging_callback", "ms_Instrument_is_online",
                            "ms_MuseSampler_set_score_id", "ms_MuseSampler_set_auto_render_interval", "ms_MuseSampler_trigger_render",
                            "ms_MuseSampler_clear_online_cache", "ms_MuseSampler_add_audition_cc_event",
                            "ms_MuseSampler_set_rendering_state_changed_callback_2", "ms_RenderProgressInfo2_get_next",
                            "ms_MuseSampler_set_lazy_render"));
