/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <memory>
#include <cstring>
#include <functional>

#include "dlib.h"
#include "apitypes.h"

#include "types/version.h"

namespace muse::musesampler {
struct MuseSamplerLibHandler
{
public:
    struct LibraryLoader {
        MuseSamplerLib (*load)(const io::path_t&) = muse::loadLib;
        void* (*resolve)(MuseSamplerLib, const char*) = muse::getLibFunc;
        void (*close)(MuseSamplerLib) = muse::closeLib;
    };

    MuseSamplerLibHandler() = default;
    explicit MuseSamplerLibHandler(const LibraryLoader& loader)
        : m_loader(loader)
    {
    }

    ~MuseSamplerLibHandler()
    {
        deinit();
    }

    MuseSamplerLibHandler(const MuseSamplerLibHandler&) = delete;
    MuseSamplerLibHandler& operator=(const MuseSamplerLibHandler&) = delete;

    bool loadLib(const io::path_t& path)
    {
        if (m_lib) {
            return false;
        }

        m_apiLoaded = false;
        m_version = Version();
        m_buildNumber = -1;
        m_lib = m_loader.load(path);
        return m_lib != nullptr;
    }

    bool loadApi(const Version& minSupportedVersion, bool useLegacyAudition = false)
    {
        if (m_initialized) {
            return false;
        }

        IF_ASSERT_FAILED(m_lib) {
            return false;
        }

        m_apiLoaded = false;

        auto getVersionMajor = (ms_get_version_major)libFunc("ms_get_version_major");
        auto getVersionMinor = (ms_get_version_minor)libFunc("ms_get_version_minor");
        auto getVersionRevision = (ms_get_version_revision)libFunc("ms_get_version_revision");
        auto getBuildNumber = (ms_get_version_build_number)libFunc("ms_get_version_build_number", false);

        // Invalid...
        if (!getVersionMajor || !getVersionMinor || !getVersionRevision) {
            return false;
        }

        m_version = Version(getVersionMajor(), getVersionMinor(), getVersionRevision());

        if (getBuildNumber) {
            m_buildNumber = getBuildNumber();
        }

        if (m_version < minSupportedVersion) {
            LOGW() << "MuseSampler " << m_version.toString() << " is not supported (too old -- update MuseSampler); ignoring";
            return false;
        }

        // Major versions have incompatible changes; we can only support interfaces we know about
        constexpr int maximumMajorVersion = 0;

        if (m_version.major() > maximumMajorVersion) {
            LOGW() << "MuseSampler " << m_version.toString() << " is not supported (too new -- update MillerScore); ignoring";
            return false;
        }

        // Stable MuseScore installations support 0.101; newer functions must be selected by API version.
        const bool atLeast102 = m_version.minor() >= 102;
        const bool atLeast103 = m_version.minor() >= 103;
        const bool atLeast104 = m_version.minor() >= 104;
        const bool atLeast105 = m_version.minor() >= 105;

        initLib = (ms_init_2)libFunc(atLeast105 ? "ms_init_2" : "ms_init");
        // Global shutdown was introduced in 0.105. Older libraries own their process lifetime.
        deinitLib = atLeast105 ? (ms_deinit)libFunc("ms_deinit") : nullptr;

        getInstrumentList = (ms_get_instrument_list)libFunc("ms_get_instrument_list");
        getMatchingInstrumentList = (ms_get_matching_instrument_list)libFunc("ms_get_matching_instrument_list");
        getNextInstrument = (ms_InstrumentList_get_next)libFunc("ms_InstrumentList_get_next");
        getInstrumentId = (ms_Instrument_get_id)libFunc("ms_Instrument_get_id");
        getInstrumentName = (ms_Instrument_get_name)libFunc("ms_Instrument_get_name");
        getInstrumentCategory = (ms_Instrument_get_category)libFunc("ms_Instrument_get_category");

        getMusicXmlSoundId = (ms_Instrument_get_musicxml_sound)libFunc("ms_Instrument_get_musicxml_sound");
        getMpeSoundId = (ms_Instrument_get_mpe_sound)libFunc("ms_Instrument_get_mpe_sound");

        getPresetList = (ms_Instrument_get_preset_list)libFunc("ms_Instrument_get_preset_list");
        getNextPreset = (ms_PresetList_get_next)libFunc("ms_PresetList_get_next");

        create = (ms_MuseSampler_create)libFunc("ms_MuseSampler_create");
        destroy = (ms_MuseSampler_destroy)libFunc("ms_MuseSampler_destroy");

        if (useLegacyAudition) {
            LOGI() << "Use legacy audition (ms_MuseSampler_init)";

            auto initSamplerFunc = (ms_MuseSampler_init)libFunc("ms_MuseSampler_init");
            if (!initSamplerFunc) {
                return false;
            }
            initSampler = [initSamplerFunc](ms_MuseSampler ms, double sample_rate, int block_size, int channel_count) {
                return initSamplerFunc(ms, sample_rate, block_size, channel_count) == ms_Result_OK;
            };
        } else {
            auto initSamplerFunc = (ms_MuseSampler_init_2)libFunc("ms_MuseSampler_init_2");
            if (!initSamplerFunc) {
                return false;
            }
            initSampler = [initSamplerFunc](ms_MuseSampler ms, double sample_rate, int block_size, int channel_count) {
                return initSamplerFunc(ms, sample_rate, block_size, channel_count) == ms_Result_OK;
            };
        }

        addTrack = (ms_MuseSampler_add_track)libFunc("ms_MuseSampler_add_track");
        finalizeTrack = (ms_MuseSampler_finalize_track)libFunc("ms_MuseSampler_finalize_track");
        clearTrack = (ms_MuseSampler_clear_track)libFunc("ms_MuseSampler_clear_track");

        disableReverb = (ms_disable_reverb)libFunc("ms_disable_reverb");
        getReverbLevel = (ms_Instrument_get_reverb_level)libFunc("ms_Instrument_get_reverb_level");

        addDynamicsEvent = (ms_MuseSampler_add_track_dynamics_event_2)libFunc(
            "ms_MuseSampler_add_track_dynamics_event_2");
        addPedalEvent = (ms_MuseSampler_add_track_pedal_event_2)libFunc("ms_MuseSampler_add_track_pedal_event_2");

        if (atLeast102) {
            auto addNoteEventFunc = (ms_MuseSampler_add_track_note_event_6)libFunc("ms_MuseSampler_add_track_note_event_6");
            auto startAuditionNoteFunc = (ms_MuseSampler_start_audition_note_5)libFunc("ms_MuseSampler_start_audition_note_5");
            if (!addNoteEventFunc || !startAuditionNoteFunc) {
                return false;
            }
            addNoteEvent = [addNoteEventFunc](ms_MuseSampler ms, ms_Track track, const NoteEvent& ev, long long& eventId) {
                return addNoteEventFunc(ms, track, ev, eventId) == ms_Result_OK;
            };
            startAuditionNote = [startAuditionNoteFunc](ms_MuseSampler ms, ms_Track track, ms_AuditionStartNoteEvent_5 ev) {
                return startAuditionNoteFunc(ms, track, ev) == ms_Result_OK;
            };
        } else {
            auto addNoteEventFunc = (ms_MuseSampler_add_track_note_event_5)libFunc("ms_MuseSampler_add_track_note_event_5");
            auto startAuditionNoteFunc = (ms_MuseSampler_start_audition_note_4)libFunc("ms_MuseSampler_start_audition_note_4");
            if (!addNoteEventFunc || !startAuditionNoteFunc) {
                return false;
            }
            addNoteEvent = [addNoteEventFunc](ms_MuseSampler ms, ms_Track track, const NoteEvent& ev, long long& eventId) {
                const ms_NoteEvent_4 legacyEvent { ev._voice, ev._location_us, ev._duration_us, ev._pitch, ev._tempo,
                                                  ev._offset_cents, ev._articulation, ev._notehead };
                return addNoteEventFunc(ms, track, legacyEvent, eventId) == ms_Result_OK;
            };
            startAuditionNote = [startAuditionNoteFunc](ms_MuseSampler ms, ms_Track track, ms_AuditionStartNoteEvent_5 ev) {
                const ms_AuditionStartNoteEvent_4 legacyEvent { ev._pitch, ev._offset_cents, ev._articulation, ev._notehead,
                                                               ev._dynamics, ev._active_presets, ev._active_text_articulation,
                                                               ev._active_syllable, ev._articulation_text_starts_at_note,
                                                               ev._syllable_starts_at_note };
                return startAuditionNoteFunc(ms, track, legacyEvent) == ms_Result_OK;
            };
        }

        isRangedArticulation = (ms_MuseSampler_is_ranged_articulation)libFunc("ms_MuseSampler_is_ranged_articulation");
        addTrackEventRangeStart
            = (ms_MuseSampler_add_track_event_range_start)libFunc("ms_MuseSampler_add_track_event_range_start");
        addTrackEventRangeEnd
            = (ms_MuseSampler_add_track_event_range_end)libFunc("ms_MuseSampler_add_track_event_range_end");

        stopAuditionNote = (ms_MuseSampler_stop_audition_note)libFunc("ms_MuseSampler_stop_audition_note");

        if (atLeast102) {
            auto addSyllableEventFunc = (ms_MuseSampler_add_track_syllable_event_2)libFunc("ms_MuseSampler_add_track_syllable_event_2");
            if (!addSyllableEventFunc) {
                return false;
            }
            addSyllableEvent = [addSyllableEventFunc](ms_MuseSampler ms, ms_Track track, SyllableEvent ev) {
                return addSyllableEventFunc(ms, track, ev);
            };
        } else {
            auto addSyllableEventFunc = (ms_MuseSampler_add_track_syllable_event)libFunc("ms_MuseSampler_add_track_syllable_event");
            if (!addSyllableEventFunc) {
                return false;
            }
            addSyllableEvent = [addSyllableEventFunc](ms_MuseSampler ms, ms_Track track, SyllableEvent ev) {
                return addSyllableEventFunc(ms, track, ms_SyllableEvent { ev._text, ev._position_us });
            };
        }

        getInstrumentVendorName = (ms_Instrument_get_vendor_name)libFunc("ms_Instrument_get_vendor_name");
        getInstrumentPackName = (ms_Instrument_get_pack_name)libFunc("ms_Instrument_get_pack_name");
        getInstrumentInfoJson = (ms_Instrument_get_info_json)libFunc("ms_Instrument_get_info_json");
        createPresetChange = (ms_MuseSampler_create_preset_change)libFunc("ms_MuseSampler_create_preset_change");
        addPreset = (ms_MuseSampler_add_preset)libFunc("ms_MuseSampler_add_preset");
        getTextArticulations = (ms_get_text_articulations)libFunc("ms_get_text_articulations");
        addTextArticulationEvent = (ms_MuseSampler_add_track_text_articulation_event)
                                   libFunc("ms_MuseSampler_add_track_text_articulation_event");
        getDrumMapping = (ms_get_drum_mapping)libFunc("ms_get_drum_mapping");

        addPitchBend = (ms_MuseSampler_add_pitch_bend)libFunc("ms_MuseSampler_add_pitch_bend");
        addVibrato = (ms_MuseSampler_add_vibrato)libFunc("ms_MuseSampler_add_vibrato");

        startOfflineMode = (ms_MuseSampler_start_offline_mode)libFunc("ms_MuseSampler_start_offline_mode");
        stopOfflineMode = (ms_MuseSampler_stop_offline_mode)libFunc("ms_MuseSampler_stop_offline_mode");
        processOffline = (ms_MuseSampler_process_offline)libFunc("ms_MuseSampler_process_offline");

        setPosition = (ms_MuseSampler_set_position)libFunc("ms_MuseSampler_set_position");
        setPlaying = (ms_MuseSampler_set_playing)libFunc("ms_MuseSampler_set_playing");
        process = (ms_MuseSampler_process)libFunc("ms_MuseSampler_process");
        allNotesOff = (ms_MuseSampler_all_notes_off)libFunc("ms_MuseSampler_all_notes_off");

        reloadAllInstruments = (ms_reload_all_instruments)libFunc("ms_reload_all_instruments", false);
        readyToPlay = (ms_MuseSampler_ready_to_play)libFunc("ms_MuseSampler_ready_to_play");

        if (atLeast102) {
            setLoggingCallback = (ms_set_logging_callback)libFunc("ms_set_logging_callback");
            isOnlineInstrument = (ms_Instrument_is_online)libFunc("ms_Instrument_is_online");
            setScoreId = (ms_MuseSampler_set_score_id)libFunc("ms_MuseSampler_set_score_id");
            setAutoRenderInterval = (ms_MuseSampler_set_auto_render_interval)libFunc("ms_MuseSampler_set_auto_render_interval");
            triggerRender = (ms_MuseSampler_trigger_render)libFunc("ms_MuseSampler_trigger_render");
            clearOnlineCache = (ms_MuseSampler_clear_online_cache)libFunc("ms_MuseSampler_clear_online_cache");
            addAuditionCCEvent = (ms_MuseSampler_add_audition_cc_event)libFunc("ms_MuseSampler_add_audition_cc_event");
        } else {
            // Online instruments did not exist in 0.101; the documented offline API remains usable.
            setLoggingCallback = [](ms_logging_callback) {};
            isOnlineInstrument = [](ms_InstrumentInfo) { return false; };
            setScoreId = [](ms_MuseSampler, const char*) {};
            setAutoRenderInterval = [](ms_MuseSampler, double) {};
            triggerRender = [](ms_MuseSampler) {};
            clearOnlineCache = [](ms_MuseSampler) {};
            addAuditionCCEvent = [](ms_MuseSampler, ms_Track, int, float) { return ms_Result_Error; };
        }

        if (atLeast105) {
            setRenderingStateChangedCallback = (ms_MuseSampler_set_rendering_state_changed_callback_2)libFunc(
                "ms_MuseSampler_set_rendering_state_changed_callback_2");
            setLazyRender = (ms_MuseSampler_set_lazy_render)libFunc("ms_MuseSampler_set_lazy_render");
        } else {
            setRenderingStateChangedCallback = atLeast103
                ? (ms_MuseSampler_set_rendering_state_changed_callback_2)libFunc("ms_MuseSampler_set_rendering_state_changed_callback")
                : [](ms_MuseSampler, ms_rendering_state_changed_callback, void*) {};
            setLazyRender = [](ms_MuseSampler, bool) {};
        }

        if (atLeast104) {
            getNextRenderProgressInfo = (ms_RenderProgressInfo2_get_next)libFunc("ms_RenderProgressInfo2_get_next");
        } else if (atLeast102) {
            auto getNextRenderProgressInfoFunc = (ms_RenderProgressInfo_get_next)libFunc("ms_RenderProgressInfo_get_next");
            if (!getNextRenderProgressInfoFunc) {
                return false;
            }
            getNextRenderProgressInfo = [getNextRenderProgressInfoFunc](ms_RenderingRangeList list) {
                const ms_RenderRangeInfo info = getNextRenderProgressInfoFunc(list);
                return RenderRangeInfo { info._start_us, info._end_us, info._state, nullptr };
            };
        } else {
            getNextRenderProgressInfo = [](ms_RenderingRangeList) {
                return RenderRangeInfo { 0, 0, ms_RenderingState_ErrorRendering, nullptr };
            };
        }

        m_apiLoaded = true;
        if (!isValid()) {
            m_apiLoaded = false;
            printApiStatus();
            return false;
        }

        return true;
    }

    bool init()
    {
        if (!isValid()) {
            LOGW() << "Optional MuseSampler API is unavailable or incomplete; continuing without Muse Sounds";
            return false;
        }

        if (m_initialized) {
            return true;
        }

        if (initLib() != ms_Result_OK) {
            LOGE() << "Could not init lib";
            return false;
        }

        m_initialized = true;

        if (disableReverb) {
            if (disableReverb() != ms_Result_OK) {
                LOGW() << "Could not disable reverb";
            }
        }

        return true;
    }

    void deinit()
    {
        if (!m_lib) {
            return;
        }

        if (m_initialized && deinitLib) {
            if (deinitLib() != ms_Result_OK) {
                LOGE() << "Could not deinit lib";
            }
        }

        m_initialized = false;
        m_apiLoaded = false;
        m_loader.close(m_lib);
        m_lib = nullptr;
    }

    bool isValid() const
    {
        return m_lib
               && m_apiLoaded
               && initLib
               && (m_version.minor() < 105 || deinitLib)
               && disableReverb
               && getInstrumentList
               && getMatchingInstrumentList
               && getNextInstrument
               && getInstrumentId
               && getInstrumentName
               && getInstrumentCategory
               && getInstrumentPackName
               && getInstrumentVendorName
               && getInstrumentInfoJson
               && getMusicXmlSoundId
               && getMpeSoundId
               && getPresetList
               && getNextPreset
               && getReverbLevel
               && getTextArticulations
               && getDrumMapping
               && create
               && destroy
               && initSampler
               && addTrack
               && finalizeTrack
               && clearTrack
               && addDynamicsEvent
               && addPedalEvent
               && addNoteEvent
               && addSyllableEvent
               && addPitchBend
               && addVibrato
               && addTextArticulationEvent
               && addPreset
               && createPresetChange
               && setPosition
               && setPlaying
               && isRangedArticulation
               && addTrackEventRangeStart
               && addTrackEventRangeEnd
               && startAuditionNote
               && stopAuditionNote
               && startOfflineMode
               && stopOfflineMode
               && processOffline
               && process
               && allNotesOff
               && readyToPlay
               && setLoggingCallback
               && setRenderingStateChangedCallback
               && isOnlineInstrument
               && setScoreId
               && getNextRenderProgressInfo
               && setAutoRenderInterval
               && triggerRender
               && clearOnlineCache
               && addAuditionCCEvent
               && setLazyRender;
    }

    const Version& version() const
    {
        return m_version;
    }

    int buildNumber() const
    {
        return m_buildNumber;
    }

    void printApiStatus() const
    {
        LOGI() << "MuseSampler API status:"
               << "\n ms_init / ms_init_2 - " << static_cast<bool>(initLib)
               << "\n ms_deinit - " << static_cast<bool>(deinitLib)
               << "\n ms_disable_reverb - " << static_cast<bool>(disableReverb)
               << "\n ms_get_instrument_list - " << static_cast<bool>(getInstrumentList)
               << "\n ms_get_matching_instrument_list - " << static_cast<bool>(getMatchingInstrumentList)
               << "\n ms_get_drum_mapping - " << static_cast<bool>(getDrumMapping)
               << "\n ms_InstrumentList_get_next - " << static_cast<bool>(getNextInstrument)
               << "\n ms_Instrument_get_info_json - " << static_cast<bool>(getInstrumentInfoJson)
               << "\n ms_Instrument_get_id - " << static_cast<bool>(getInstrumentId)
               << "\n ms_Instrument_get_name - " << static_cast<bool>(getInstrumentName)
               << "\n ms_Instrument_get_vendor_name - " << static_cast<bool>(getInstrumentVendorName)
               << "\n ms_Instrument_get_category - " << static_cast<bool>(getInstrumentCategory)
               << "\n ms_Instrument_get_pack_name - " << static_cast<bool>(getInstrumentPackName)
               << "\n ms_Instrument_get_musicxml_sound - " << static_cast<bool>(getMusicXmlSoundId)
               << "\n ms_Instrument_get_mpe_sound - " << static_cast<bool>(getMpeSoundId)
               << "\n ms_Instrument_get_reverb_level - " << static_cast<bool>(getReverbLevel)
               << "\n ms_Instrument_get_preset_list - " << static_cast<bool>(getPresetList)
               << "\n ms_PresetList_get_next - " << static_cast<bool>(getNextPreset)
               << "\n ms_MuseSampler_create_preset_change - " << static_cast<bool>(createPresetChange)
               << "\n ms_MuseSampler_add_preset - " << static_cast<bool>(addPreset)
               << "\n ms_get_text_articulations - " << static_cast<bool>(getTextArticulations)
               << "\n ms_MuseSampler_add_track_text_articulation_event - " << static_cast<bool>(addTextArticulationEvent)
               << "\n ms_MuseSampler_create - " << static_cast<bool>(create)
               << "\n ms_MuseSampler_destroy - " << static_cast<bool>(destroy)
               << "\n ms_MuseSampler_add_track - " << static_cast<bool>(addTrack)
               << "\n ms_MuseSampler_clear_track - " << static_cast<bool>(clearTrack)
               << "\n ms_MuseSampler_finalize_track - " << static_cast<bool>(finalizeTrack)
               << "\n ms_MuseSampler_add_track_dynamics_event_2 - " << static_cast<bool>(addDynamicsEvent)
               << "\n ms_MuseSampler_add_track_pedal_event_2 - " << static_cast<bool>(addPedalEvent)
               << "\n ms_MuseSampler_add_pitch_bend - " << static_cast<bool>(addPitchBend)
               << "\n ms_MuseSampler_stop_audition_note - " << static_cast<bool>(stopAuditionNote)
               << "\n ms_MuseSampler_add_pitch_bend - " << static_cast<bool>(addPitchBend)
               << "\n ms_MuseSampler_add_vibrato - " << static_cast<bool>(addVibrato)
               << "\n ms_MuseSampler_is_ranged_articulation - " << static_cast<bool>(isRangedArticulation)
               << "\n ms_MuseSampler_add_track_event_range_start - " << static_cast<bool>(addTrackEventRangeStart)
               << "\n ms_MuseSampler_add_track_event_range_end - " << static_cast<bool>(addTrackEventRangeEnd)
               << "\n ms_MuseSampler_add_audition_cc_event - " << static_cast<bool>(addAuditionCCEvent)
               << "\n ms_MuseSampler_add_track_syllable_event_2 - " << static_cast<bool>(addSyllableEvent)
               << "\n ms_MuseSampler_start_offline_mode - " << static_cast<bool>(startOfflineMode)
               << "\n ms_MuseSampler_stop_offline_mode - " << static_cast<bool>(stopOfflineMode)
               << "\n ms_MuseSampler_process_offline - " << static_cast<bool>(processOffline)
               << "\n ms_MuseSampler_set_position - " << static_cast<bool>(setPosition)
               << "\n ms_MuseSampler_set_playing - " << static_cast<bool>(setPlaying)
               << "\n ms_MuseSampler_process - " << static_cast<bool>(process)
               << "\n ms_MuseSampler_all_notes_off - " << static_cast<bool>(allNotesOff)
               << "\n ms_MuseSampler_ready_to_play - " << static_cast<bool>(readyToPlay)
               << "\n ms_Instrument_is_online - " << static_cast<bool>(isOnlineInstrument)
               << "\n ms_MuseSampler_trigger_render - " << static_cast<bool>(triggerRender)
               << "\n ms_reload_all_instruments - " << static_cast<bool>(reloadAllInstruments)
               << "\n ms_set_logging_callback - " << static_cast<bool>(setLoggingCallback)
               << "\n ms_MuseSampler_set_rendering_state_changed_callback_2 - " <<
            static_cast<bool>(setRenderingStateChangedCallback)
               << "\n ms_MuseSampler_set_score_id - " << static_cast<bool>(setScoreId)
               << "\n ms_MuseSampler_clear_online_cache - " << static_cast<bool>(clearOnlineCache)
               << "\n ms_RenderProgressInfo2_get_next - " << static_cast<bool>(getNextRenderProgressInfo)
               << "\n ms_MuseSampler_set_auto_render_interval - " << static_cast<bool>(setAutoRenderInterval)
               << "\n ms_MuseSampler_set_lazy_render - " << static_cast<bool>(setLazyRender);
    }

    ms_get_instrument_list getInstrumentList = nullptr;
    ms_get_matching_instrument_list getMatchingInstrumentList = nullptr;
    ms_InstrumentList_get_next getNextInstrument = nullptr;
    ms_Instrument_get_id getInstrumentId = nullptr;
    ms_Instrument_get_name getInstrumentName = nullptr;
    ms_Instrument_get_category getInstrumentCategory = nullptr;
    ms_Instrument_get_pack_name getInstrumentPackName = nullptr;
    ms_Instrument_get_info_json getInstrumentInfoJson = nullptr;
    ms_Instrument_get_vendor_name getInstrumentVendorName = nullptr;
    ms_Instrument_get_musicxml_sound getMusicXmlSoundId = nullptr;
    ms_Instrument_get_mpe_sound getMpeSoundId = nullptr;
    ms_Instrument_get_reverb_level getReverbLevel = nullptr;

    ms_Instrument_get_preset_list getPresetList = nullptr;
    ms_PresetList_get_next getNextPreset = nullptr;
    ms_MuseSampler_create_preset_change createPresetChange = nullptr;
    ms_MuseSampler_add_preset addPreset = nullptr;
    ms_get_text_articulations getTextArticulations = nullptr;
    ms_MuseSampler_add_track_text_articulation_event addTextArticulationEvent = nullptr;
    ms_get_drum_mapping getDrumMapping = nullptr;

    ms_MuseSampler_create create = nullptr;
    ms_MuseSampler_destroy destroy = nullptr;

    std::function<bool(ms_MuseSampler ms, double sample_rate, int block_size, int channel_count)> initSampler = nullptr;

    ms_MuseSampler_add_track addTrack = nullptr;
    ms_MuseSampler_finalize_track finalizeTrack = nullptr;
    ms_MuseSampler_clear_track clearTrack = nullptr;

    ms_MuseSampler_add_track_dynamics_event_2 addDynamicsEvent = nullptr;
    ms_MuseSampler_add_track_pedal_event_2 addPedalEvent = nullptr;
    std::function<bool(ms_MuseSampler ms, ms_Track track, const NoteEvent& ev, long long& event_id)> addNoteEvent = nullptr;

    ms_MuseSampler_is_ranged_articulation isRangedArticulation = nullptr;
    ms_MuseSampler_add_track_event_range_start addTrackEventRangeStart = nullptr;
    ms_MuseSampler_add_track_event_range_end addTrackEventRangeEnd = nullptr;

    ms_MuseSampler_add_pitch_bend addPitchBend = nullptr;
    ms_MuseSampler_add_vibrato addVibrato = nullptr;

    std::function<ms_Result(ms_MuseSampler, ms_Track, SyllableEvent)> addSyllableEvent = nullptr;

    std::function<bool(ms_MuseSampler ms, ms_Track track, ms_AuditionStartNoteEvent_5)> startAuditionNote = nullptr;
    ms_MuseSampler_stop_audition_note stopAuditionNote = nullptr;
    ms_MuseSampler_add_audition_cc_event addAuditionCCEvent = nullptr;

    ms_MuseSampler_start_offline_mode startOfflineMode = nullptr;
    ms_MuseSampler_stop_offline_mode stopOfflineMode = nullptr;
    ms_MuseSampler_process_offline processOffline = nullptr;

    ms_MuseSampler_ready_to_play readyToPlay = nullptr;

    ms_MuseSampler_set_position setPosition = nullptr;
    ms_MuseSampler_set_playing setPlaying = nullptr;
    ms_MuseSampler_process process = nullptr;
    ms_MuseSampler_all_notes_off allNotesOff = nullptr;

    ms_reload_all_instruments reloadAllInstruments = nullptr;

    ms_set_logging_callback setLoggingCallback = nullptr;
    ms_MuseSampler_set_rendering_state_changed_callback_2 setRenderingStateChangedCallback = nullptr;

    ms_MuseSampler_set_score_id setScoreId = nullptr;
    ms_Instrument_is_online isOnlineInstrument = nullptr;
    std::function<RenderRangeInfo(ms_RenderingRangeList)> getNextRenderProgressInfo = nullptr;
    ms_MuseSampler_set_auto_render_interval setAutoRenderInterval = nullptr;
    ms_MuseSampler_trigger_render triggerRender = nullptr;
    ms_MuseSampler_clear_online_cache clearOnlineCache = nullptr;
    ms_MuseSampler_set_lazy_render setLazyRender = nullptr;

private:
    void* libFunc(const char* funcName, bool required = true)
    {
        void* func = m_loader.resolve(m_lib, funcName);
        if (!func && required) {
            LOGW() << "Optional MuseSampler library is missing a required API function: " << funcName;
        }
        return func;
    }

    ms_init_2 initLib = nullptr;
    ms_deinit deinitLib = nullptr;
    ms_disable_reverb disableReverb = nullptr;

    MuseSamplerLib m_lib = nullptr;
    LibraryLoader m_loader;
    bool m_initialized = false;
    bool m_apiLoaded = false;
    Version m_version;
    int m_buildNumber = -1;
};

using MuseSamplerLibHandlerPtr = std::shared_ptr<MuseSamplerLibHandler>;
}
