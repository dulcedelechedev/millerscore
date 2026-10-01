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
#include "musesampler/internal/musesamplerconfiguration.h"

using namespace muse;
using namespace muse::musesampler;

namespace {
struct FakeLibrary {
    bool available = true;
    bool versionedExports = false;
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
    int syllableEvents = 0;
    int renderingCallbacks = 0;
    int lazyRenderUpdates = 0;
    int renderRequests = 0;
    int cacheClears = 0;
    bool online = true;
    std::string scoreId;
    double renderInterval = 0.;
    NoteEvent lastNote {};
    ms_AuditionStartNoteEvent_5 lastAudition {};
    SyllableEvent lastSyllable {};
    ms_RenderingRangeList progressList = nullptr;
    std::vector<std::string> resolvedExports;
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

ms_Result fakeNoteEvent(ms_MuseSampler, ms_Track, NoteEvent ev, long long& eventId)
{
    activeLibrary->lastNote = ev;
    ++activeLibrary->noteEvents;
    eventId = 42;
    return activeLibrary->samplerResult;
}

ms_Result fakeAuditionEvent(ms_MuseSampler, ms_Track, ms_AuditionStartNoteEvent_5 ev)
{
    activeLibrary->lastAudition = ev;
    ++activeLibrary->auditionEvents;
    return activeLibrary->samplerResult;
}

ms_Result fakeLegacyNoteEvent(ms_MuseSampler ms, ms_Track track, ms_NoteEvent_4 ev, long long& eventId)
{
    const NoteEvent current { ev._voice, ev._location_us, ev._duration_us, ev._pitch, ev._tempo, ev._offset_cents,
                              ev._articulation, ms_NoteArticulation2_None, ev._notehead };
    return fakeNoteEvent(ms, track, current, eventId);
}

ms_Result fakeLegacyAuditionEvent(ms_MuseSampler ms, ms_Track track, ms_AuditionStartNoteEvent_4 ev)
{
    const ms_AuditionStartNoteEvent_5 current { ev._pitch, ev._offset_cents, ev._articulation, ms_NoteArticulation2_None,
                                               ev._notehead, ev._dynamics, ev._active_presets, ev._active_text_articulation,
                                               ev._active_syllable, ev._articulation_text_starts_at_note,
                                               ev._syllable_starts_at_note };
    return fakeAuditionEvent(ms, track, current);
}

ms_Result fakeSyllableEvent(ms_MuseSampler, ms_Track, SyllableEvent ev)
{
    activeLibrary->lastSyllable = ev;
    ++activeLibrary->syllableEvents;
    return activeLibrary->samplerResult;
}

ms_Result fakeLegacySyllableEvent(ms_MuseSampler ms, ms_Track track, ms_SyllableEvent ev)
{
    return fakeSyllableEvent(ms, track, SyllableEvent { ev._text, ev._position_us, false });
}

ms_RenderRangeInfo fakeLegacyProgress(ms_RenderingRangeList list)
{
    activeLibrary->progressList = list;
    return ms_RenderRangeInfo { 125000, 875000, ms_RenderingState_ErrorNetwork };
}

ms_RenderRangeInfo2 fakeProgress(ms_RenderingRangeList list)
{
    activeLibrary->progressList = list;
    return ms_RenderRangeInfo2 { 125000, 875000, ms_RenderingState_ErrorNetwork, "fixture network error" };
}

void fakeRenderingCallback(ms_MuseSampler, ms_rendering_state_changed_callback, void*)
{
    ++activeLibrary->renderingCallbacks;
}

void fakeLazyRender(ms_MuseSampler, bool) { ++activeLibrary->lazyRenderUpdates; }
bool fakeOnlineInstrument(ms_InstrumentInfo) { return activeLibrary->online; }
void fakeLoggingCallback(ms_logging_callback) {}
void fakeScoreId(ms_MuseSampler, const char* id) { activeLibrary->scoreId = id; }
void fakeRenderInterval(ms_MuseSampler, double interval) { activeLibrary->renderInterval = interval; }
void fakeTriggerRender(ms_MuseSampler) { ++activeLibrary->renderRequests; }
void fakeClearCache(ms_MuseSampler) { ++activeLibrary->cacheClears; }
ms_Result fakeAuditionCC(ms_MuseSampler, ms_Track, int, float) { return activeLibrary->samplerResult; }

void* fakeResolve(MuseSamplerLib, const char* name)
{
    activeLibrary->resolvedExports.emplace_back(name);
    if (activeLibrary->versionedExports) {
        const int minor = activeLibrary->minor;
        if (minor < 105 && (std::strcmp(name, "ms_init_2") == 0 || std::strcmp(name, "ms_deinit") == 0
                            || std::strcmp(name, "ms_MuseSampler_set_lazy_render") == 0
                            || std::strcmp(name, "ms_MuseSampler_set_rendering_state_changed_callback_2") == 0)) {
            return nullptr;
        }
        if (minor < 104 && std::strcmp(name, "ms_RenderProgressInfo2_get_next") == 0) {
            return nullptr;
        }
        if (minor < 103 && std::strcmp(name, "ms_MuseSampler_set_rendering_state_changed_callback") == 0) {
            return nullptr;
        }
        if (minor < 102 && (std::strcmp(name, "ms_MuseSampler_add_track_note_event_6") == 0
                            || std::strcmp(name, "ms_MuseSampler_start_audition_note_5") == 0
                            || std::strcmp(name, "ms_MuseSampler_add_track_syllable_event_2") == 0
                            || std::strcmp(name, "ms_set_logging_callback") == 0
                            || std::strcmp(name, "ms_Instrument_is_online") == 0
                            || std::strcmp(name, "ms_MuseSampler_set_score_id") == 0
                            || std::strcmp(name, "ms_MuseSampler_set_auto_render_interval") == 0
                            || std::strcmp(name, "ms_MuseSampler_trigger_render") == 0
                            || std::strcmp(name, "ms_MuseSampler_clear_online_cache") == 0
                            || std::strcmp(name, "ms_MuseSampler_add_audition_cc_event") == 0
                            || std::strcmp(name, "ms_RenderProgressInfo_get_next") == 0)) {
            return nullptr;
        }
    }
    if (activeLibrary->missingExport == name) {
        return nullptr;
    }
    if (std::strcmp(name, "ms_get_version_major") == 0) { return reinterpret_cast<void*>(fakeMajor); }
    if (std::strcmp(name, "ms_get_version_minor") == 0) { return reinterpret_cast<void*>(fakeMinor); }
    if (std::strcmp(name, "ms_get_version_revision") == 0) { return reinterpret_cast<void*>(fakeRevision); }
    if (std::strcmp(name, "ms_get_version_build_number") == 0) { return nullptr; }
    if (std::strcmp(name, "ms_init") == 0) { return reinterpret_cast<void*>(fakeInit); }
    if (std::strcmp(name, "ms_init_2") == 0) { return reinterpret_cast<void*>(fakeInit); }
    if (std::strcmp(name, "ms_deinit") == 0) { return reinterpret_cast<void*>(fakeDeinit); }
    if (std::strcmp(name, "ms_disable_reverb") == 0) { return reinterpret_cast<void*>(fakeDisableReverb); }
    if (std::strcmp(name, "ms_MuseSampler_init_2") == 0 || std::strcmp(name, "ms_MuseSampler_init") == 0) {
        return reinterpret_cast<void*>(fakeSamplerInit);
    }
    if (std::strcmp(name, "ms_MuseSampler_add_track_note_event_6") == 0) { return reinterpret_cast<void*>(fakeNoteEvent); }
    if (std::strcmp(name, "ms_MuseSampler_start_audition_note_5") == 0) { return reinterpret_cast<void*>(fakeAuditionEvent); }
    if (std::strcmp(name, "ms_MuseSampler_add_track_note_event_5") == 0) { return reinterpret_cast<void*>(fakeLegacyNoteEvent); }
    if (std::strcmp(name, "ms_MuseSampler_start_audition_note_4") == 0) { return reinterpret_cast<void*>(fakeLegacyAuditionEvent); }
    if (std::strcmp(name, "ms_MuseSampler_add_track_syllable_event_2") == 0) { return reinterpret_cast<void*>(fakeSyllableEvent); }
    if (std::strcmp(name, "ms_MuseSampler_add_track_syllable_event") == 0) { return reinterpret_cast<void*>(fakeLegacySyllableEvent); }
    if (std::strcmp(name, "ms_RenderProgressInfo_get_next") == 0) { return reinterpret_cast<void*>(fakeLegacyProgress); }
    if (std::strcmp(name, "ms_RenderProgressInfo2_get_next") == 0) { return reinterpret_cast<void*>(fakeProgress); }
    if (std::strcmp(name, "ms_MuseSampler_set_rendering_state_changed_callback") == 0
        || std::strcmp(name, "ms_MuseSampler_set_rendering_state_changed_callback_2") == 0) {
        return reinterpret_cast<void*>(fakeRenderingCallback);
    }
    if (std::strcmp(name, "ms_MuseSampler_set_lazy_render") == 0) { return reinterpret_cast<void*>(fakeLazyRender); }
    if (std::strcmp(name, "ms_Instrument_is_online") == 0) { return reinterpret_cast<void*>(fakeOnlineInstrument); }
    if (std::strcmp(name, "ms_set_logging_callback") == 0) { return reinterpret_cast<void*>(fakeLoggingCallback); }
    if (std::strcmp(name, "ms_MuseSampler_set_score_id") == 0) { return reinterpret_cast<void*>(fakeScoreId); }
    if (std::strcmp(name, "ms_MuseSampler_set_auto_render_interval") == 0) { return reinterpret_cast<void*>(fakeRenderInterval); }
    if (std::strcmp(name, "ms_MuseSampler_trigger_render") == 0) { return reinterpret_cast<void*>(fakeTriggerRender); }
    if (std::strcmp(name, "ms_MuseSampler_clear_online_cache") == 0) { return reinterpret_cast<void*>(fakeClearCache); }
    if (std::strcmp(name, "ms_MuseSampler_add_audition_cc_event") == 0) { return reinterpret_cast<void*>(fakeAuditionCC); }
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

class MuseSamplerStableVersionTests : public MuseSamplerLibHandlerTests, public ::testing::WithParamInterface<int>
{
};

TEST_P(MuseSamplerStableVersionTests, LoadsApiSupportedByMuseScoreFourSix)
{
    library.minor = GetParam();
    library.versionedExports = true;
    {
        MuseSamplerLibHandler handler(fakeLoader);
        ASSERT_TRUE(handler.loadLib("optional-library"));
        ASSERT_TRUE(handler.loadApi(Version(0, 101, 0)));
        EXPECT_TRUE(handler.init());
        EXPECT_TRUE(handler.init());
        EXPECT_EQ(handler.version(), Version(0, GetParam(), 0));
    }
    EXPECT_EQ(library.initializations, 1);
    EXPECT_EQ(library.shutdowns, GetParam() >= 105 ? 1 : 0);
    EXPECT_EQ(library.closes, 1);
}

INSTANTIATE_TEST_SUITE_P(StableApis, MuseSamplerStableVersionTests, ::testing::Values(101, 102, 103, 104, 105));

TEST_P(MuseSamplerStableVersionTests, ConvertsNoteAuditionAndLyricsWithoutChangingSupportedFields)
{
    library.minor = GetParam();
    library.versionedExports = true;
    MuseSamplerLibHandler handler(fakeLoader);
    ASSERT_TRUE(handler.loadLib("optional-library"));
    ASSERT_TRUE(handler.loadApi(Version(0, 101, 0)));
    const NoteEvent note { 3, 125000, 640000, 63, 87.5, -25, ms_NoteArticulation_Pizzicato,
                          ms_NoteArticulation2_BrushDamp, ms_NoteHead_Diamond };
    const ms_AuditionStartNoteEvent_5 audition { 63, -25, ms_NoteArticulation_Pizzicato, ms_NoteArticulation2_BrushDamp,
                                               ms_NoteHead_Diamond, 0.42, "soft|warm", "pizz.", "la", true, true };
    const SyllableEvent syllable { "la", 125000, true };
    for (const ms_Result result : { ms_Result_OK, ms_Result_Error }) {
        library.samplerResult = result;
        long long id = 0;
        EXPECT_EQ(handler.addNoteEvent(nullptr, nullptr, note, id), result == ms_Result_OK);
        EXPECT_EQ(id, 42);
        EXPECT_EQ(handler.startAuditionNote(nullptr, nullptr, audition), result == ms_Result_OK);
        EXPECT_EQ(handler.addSyllableEvent(nullptr, nullptr, syllable), result);
        EXPECT_EQ(library.lastNote._voice, note._voice);
        EXPECT_EQ(library.lastNote._location_us, note._location_us);
        EXPECT_EQ(library.lastNote._duration_us, note._duration_us);
        EXPECT_EQ(library.lastNote._pitch, note._pitch);
        EXPECT_DOUBLE_EQ(library.lastNote._tempo, note._tempo);
        EXPECT_EQ(library.lastNote._offset_cents, note._offset_cents);
        EXPECT_EQ(library.lastNote._articulation, note._articulation);
        EXPECT_EQ(library.lastNote._notehead, note._notehead);
        EXPECT_EQ(library.lastNote._articulation_2, GetParam() >= 102 ? note._articulation_2 : ms_NoteArticulation2_None);
        EXPECT_EQ(library.lastAudition._pitch, audition._pitch);
        EXPECT_EQ(library.lastAudition._offset_cents, audition._offset_cents);
        EXPECT_EQ(library.lastAudition._articulation, audition._articulation);
        EXPECT_EQ(library.lastAudition._notehead, audition._notehead);
        EXPECT_DOUBLE_EQ(library.lastAudition._dynamics, audition._dynamics);
        EXPECT_STREQ(library.lastAudition._active_presets, audition._active_presets);
        EXPECT_STREQ(library.lastAudition._active_text_articulation, audition._active_text_articulation);
        EXPECT_STREQ(library.lastAudition._active_syllable, audition._active_syllable);
        EXPECT_TRUE(library.lastAudition._articulation_text_starts_at_note);
        EXPECT_TRUE(library.lastAudition._syllable_starts_at_note);
        EXPECT_EQ(library.lastAudition._articulation_2, GetParam() >= 102 ? audition._articulation_2 : ms_NoteArticulation2_None);
        EXPECT_STREQ(library.lastSyllable._text, syllable._text);
        EXPECT_EQ(library.lastSyllable._position_us, syllable._position_us);
        EXPECT_EQ(library.lastSyllable._hyphened_to_next, GetParam() >= 102);
    }
    EXPECT_EQ(library.noteEvents, 2);
    EXPECT_EQ(library.auditionEvents, 2);
    EXPECT_EQ(library.syllableEvents, 2);
}

TEST_P(MuseSamplerStableVersionTests, PreservesRenderingStatusAndGatesNewFeaturesByVersion)
{
    library.minor = GetParam();
    library.versionedExports = true;
    MuseSamplerLibHandler handler(fakeLoader);
    ASSERT_TRUE(handler.loadLib("optional-library"));
    ASSERT_TRUE(handler.loadApi(Version(0, 101, 0)));
    EXPECT_EQ(handler.isOnlineInstrument(nullptr), GetParam() >= 102);
    handler.setLoggingCallback(nullptr);
    handler.setScoreId(nullptr, "fixture-score-id");
    handler.setAutoRenderInterval(nullptr, 3.0);
    handler.triggerRender(nullptr);
    handler.clearOnlineCache(nullptr);
    handler.setRenderingStateChangedCallback(nullptr, nullptr, nullptr);
    handler.setLazyRender(nullptr, true);
    EXPECT_EQ(library.scoreId, GetParam() >= 102 ? "fixture-score-id" : "");
    EXPECT_DOUBLE_EQ(library.renderInterval, GetParam() >= 102 ? 3.0 : 0.0);
    EXPECT_EQ(library.renderRequests, GetParam() >= 102 ? 1 : 0);
    EXPECT_EQ(library.cacheClears, GetParam() >= 102 ? 1 : 0);
    EXPECT_EQ(library.renderingCallbacks, GetParam() >= 103 ? 1 : 0);
    EXPECT_EQ(library.lazyRenderUpdates, GetParam() >= 105 ? 1 : 0);
    int progressToken = 0;
    const RenderRangeInfo info = handler.getNextRenderProgressInfo(&progressToken);
    EXPECT_EQ(info._start_us, GetParam() >= 102 ? 125000 : 0);
    EXPECT_EQ(info._end_us, GetParam() >= 102 ? 875000 : 0);
    EXPECT_EQ(info._state, GetParam() >= 102 ? ms_RenderingState_ErrorNetwork : ms_RenderingState_ErrorRendering);
    EXPECT_EQ(library.progressList, GetParam() >= 102 ? &progressToken : nullptr);
    if (GetParam() >= 104) {
        EXPECT_STREQ(info._error_message, "fixture network error");
    } else {
        EXPECT_EQ(info._error_message, nullptr);
    }
    EXPECT_EQ(handler.addAuditionCCEvent(nullptr, nullptr, 11, 0.5f), GetParam() >= 102 ? ms_Result_OK : ms_Result_Error);
}

struct VersionedRequiredExport {
    int minor;
    const char* name;
};
class MuseSamplerStableRequiredExportTests : public MuseSamplerLibHandlerTests,
    public ::testing::WithParamInterface<VersionedRequiredExport>
{
};

TEST_P(MuseSamplerStableRequiredExportTests, RejectsMissingVersionSpecificApiBeforeInitialization)
{
    library.minor = GetParam().minor;
    library.versionedExports = true;
    library.missingExport = GetParam().name;
    MuseSamplerLibHandler handler(fakeLoader);
    ASSERT_TRUE(handler.loadLib("optional-library"));
    EXPECT_FALSE(handler.loadApi(Version(0, 101, 0)));
    EXPECT_FALSE(handler.isValid());
    EXPECT_FALSE(handler.init());
    EXPECT_EQ(library.initializations, 0);
    EXPECT_EQ(library.shutdowns, 0);
}

INSTANTIATE_TEST_SUITE_P(StableRequiredApis, MuseSamplerStableRequiredExportTests, ::testing::Values(
    VersionedRequiredExport { 101, "ms_init" },
    VersionedRequiredExport { 101, "ms_MuseSampler_add_track_note_event_5" },
    VersionedRequiredExport { 101, "ms_MuseSampler_start_audition_note_4" },
    VersionedRequiredExport { 101, "ms_MuseSampler_add_track_syllable_event" },
    VersionedRequiredExport { 101, "ms_MuseSampler_init_2" },
    VersionedRequiredExport { 101, "ms_MuseSampler_destroy" },
    VersionedRequiredExport { 102, "ms_init" },
    VersionedRequiredExport { 102, "ms_MuseSampler_add_track_note_event_6" },
    VersionedRequiredExport { 102, "ms_MuseSampler_start_audition_note_5" },
    VersionedRequiredExport { 102, "ms_MuseSampler_add_track_syllable_event_2" },
    VersionedRequiredExport { 102, "ms_set_logging_callback" },
    VersionedRequiredExport { 102, "ms_Instrument_is_online" },
    VersionedRequiredExport { 102, "ms_MuseSampler_set_score_id" },
    VersionedRequiredExport { 102, "ms_MuseSampler_set_auto_render_interval" },
    VersionedRequiredExport { 102, "ms_MuseSampler_trigger_render" },
    VersionedRequiredExport { 102, "ms_MuseSampler_clear_online_cache" },
    VersionedRequiredExport { 102, "ms_MuseSampler_add_audition_cc_event" },
    VersionedRequiredExport { 102, "ms_RenderProgressInfo_get_next" },
    VersionedRequiredExport { 103, "ms_MuseSampler_set_rendering_state_changed_callback" },
    VersionedRequiredExport { 103, "ms_RenderProgressInfo_get_next" },
    VersionedRequiredExport { 104, "ms_MuseSampler_set_rendering_state_changed_callback" },
    VersionedRequiredExport { 104, "ms_RenderProgressInfo2_get_next" },
    VersionedRequiredExport { 105, "ms_init_2" },
    VersionedRequiredExport { 105, "ms_deinit" },
    VersionedRequiredExport { 105, "ms_MuseSampler_set_rendering_state_changed_callback_2" },
    VersionedRequiredExport { 105, "ms_MuseSampler_set_lazy_render" }
));

TEST(MuseSamplerConfigurationTests, SupportsStableMuseScoreSamplerApi)
{
    MuseSamplerConfiguration configuration(nullptr);
    EXPECT_EQ(configuration.minSupportedVersion(), Version(0, 101, 0));
}
