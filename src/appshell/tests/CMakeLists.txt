# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.21)
project(MillerScoreDawTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

get_filename_component(MILLERSCORE_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(MILLERSCORE_TEST_DEPS_ROOT "" CACHE PATH "Existing Muse dependency source directory (_deps)")
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml)
find_package(GTest CONFIG QUIET)
if(NOT TARGET GTest::gtest)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    find_path(MILLERSCORE_GTEST_SOURCE_DIR CMakeLists.txt
        HINTS "${MILLERSCORE_TEST_DEPS_ROOT}/googletest/googletest" NO_DEFAULT_PATH REQUIRED)
    add_subdirectory("${MILLERSCORE_GTEST_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)
endif()
find_path(MILLERSCORE_PICOJSON_INCLUDE_DIR picojson.h
    HINTS "${MILLERSCORE_TEST_DEPS_ROOT}/picojson/picojson" REQUIRED)
find_path(MILLERSCORE_UTFCPP_INCLUDE_DIR utf8.h
    HINTS "${MILLERSCORE_TEST_DEPS_ROOT}/utfcpp/utfcpp/source" REQUIRED)

# Compile the actual state-independent catalog projection without constructing
# DawProjectModel's application services. Selector tests execute the unchanged
# QML functions in QJSEngine; they do not claim to exercise rendered gestures.
set(catalog_model "${MILLERSCORE_SOURCE_ROOT}/src/appshell/qml/MuseScore/AppShell/dawprojectmodel.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${catalog_model}")
file(READ "${catalog_model}" catalog_source)
string(FIND "${catalog_source}" "QVariantList DawProjectModel::midiControllerCatalog() const" catalog_start)
string(FIND "${catalog_source}" "QString DawProjectModel::newId()" catalog_end)
if(catalog_start LESS 0 OR catalog_end LESS catalog_start)
    message(FATAL_ERROR "Cannot locate the production MIDI catalog projection")
endif()
math(EXPR catalog_length "${catalog_end}-${catalog_start}")
string(SUBSTRING "${catalog_source}" ${catalog_start} ${catalog_length} catalog_method)
file(WRITE "${CMAKE_BINARY_DIR}/catalog_projection.cpp"
    "#include <QVariant>\n#include <QString>\n#include \"engraving/automation/midicontrollercatalog.h\"\n"
    "using namespace mu;\nclass DawProjectModel { public: QVariantList midiControllerCatalog() const; };\n"
    "${catalog_method}\nQVariantList midiControllerSelectorTestCatalog() { return DawProjectModel().midiControllerCatalog(); }\n")

set(MILLERSCORE_AUDIO_MODEL_SOURCE_OVERRIDE "" CACHE FILEPATH "Optional audio-model source revision for a focused regression comparison")
set(audio_model "${MILLERSCORE_SOURCE_ROOT}/src/appshell/qml/MuseScore/AppShell/dawaudiotracksmodel.cpp")
if(MILLERSCORE_AUDIO_MODEL_SOURCE_OVERRIDE)
    set(audio_model "${MILLERSCORE_AUDIO_MODEL_SOURCE_OVERRIDE}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${audio_model}")
file(READ "${audio_model}" audio_source)
string(FIND "${audio_source}" "constexpr int PEAK_FRAMES" audio_helpers_start)
string(FIND "${audio_source}" "DawAudioTracksModel::DawAudioTracksModel(QObject* parent)" audio_helpers_end)
math(EXPR audio_helpers_length "${audio_helpers_end}-${audio_helpers_start}")
string(SUBSTRING "${audio_source}" ${audio_helpers_start} ${audio_helpers_length} audio_helpers)
set(audio_fixture "#include \"dawmissingmedia_fixture.h\"\nusing namespace mu;\nusing namespace mu::appshell;\nusing namespace muse;\nusing namespace muse::audio;\nnamespace {\n${audio_helpers}\n")
foreach(audio_method
    "void DawAudioTracksModel::readFromProject()"
    "void DawAudioTracksModel::writeToProject()"
    "std::vector<double> DawAudioTracksModel::clipStartSeconds(const Track& track) const"
    "int DawAudioTracksModel::endTickFor(const Clip& clip) const"
    "ByteArray DawAudioTracksModel::renderTrack(const Track& track, const std::vector<double>& starts) const"
    "void DawAudioTracksModel::syncTrack(Track& track, bool force)"
    "QVariantList DawAudioTracksModel::makePeaks(const DecodedAudio& audio)"
    "QVariantList DawAudioTracksModel::clipsForQml(const Track& track) const")
    string(FIND "${audio_source}" "${audio_method}" method_start)
    if(method_start LESS 0)
        message(FATAL_ERROR "Cannot locate production audio method: ${audio_method}")
    endif()
    string(SUBSTRING "${audio_source}" ${method_start} -1 method_tail)
    string(FIND "${method_tail}" "\n}\n" method_end)
    if(method_end LESS 0)
        message(FATAL_ERROR "Cannot locate end of production audio method: ${audio_method}")
    endif()
    math(EXPR method_length "${method_end}+3")
    string(SUBSTRING "${method_tail}" 0 ${method_length} method_source)
    string(APPEND audio_fixture "${method_source}\n")
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/missingmedia_methods.cpp" "${audio_fixture}")

function(millerscore_append_method source signature output_name)
    string(FIND "${source}" "${signature}" method_start)
    if(method_start LESS 0)
        message(FATAL_ERROR "Cannot locate production method: ${signature}")
    endif()
    string(SUBSTRING "${source}" ${method_start} -1 method_tail)
    string(FIND "${method_tail}" "\n}\n" method_end)
    if(method_end LESS 0)
        message(FATAL_ERROR "Cannot locate production method end: ${signature}")
    endif()
    math(EXPR method_length "${method_end}+3")
    string(SUBSTRING "${method_tail}" 0 ${method_length} method_source)
    set(${output_name} "${${output_name}}${method_source}\n" PARENT_SCOPE)
endfunction()
set(notation_source_path "${MILLERSCORE_SOURCE_ROOT}/src/notation/internal/notationautomation.cpp")
set(master_source_path "${MILLERSCORE_SOURCE_ROOT}/src/engraving/dom/masterscore.cpp")
set(undo_source_path "${MILLERSCORE_SOURCE_ROOT}/src/engraving/editing/editautomationpoints.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${notation_source_path}" "${master_source_path}" "${undo_source_path}")
file(READ "${notation_source_path}" notation_source)
file(READ "${master_source_path}" master_source)
file(READ "${undo_source_path}" undo_source)
set(notation_fixture "#include \"notationautomation_fixture.h\"\nusing namespace mu;\nusing namespace mu::engraving;\nusing namespace mu::notation;\n")
string(FIND "${undo_source}" "static std::optional<ChangedRange> computeChangedRange" undo_start)
if(undo_start LESS 0)
    message(FATAL_ERROR "Cannot locate production automation undo implementation")
endif()
string(SUBSTRING "${undo_source}" ${undo_start} -1 undo_methods)
string(APPEND notation_fixture "${undo_methods}\n")
foreach(signature
    "AutomationDataConstPtr MasterScore::automationData() const"
    "void MasterScore::editAutomationPoints(const AutomationCurveKey& key, AutomationPointEdits& edits, bool undoable)")
    millerscore_append_method("${master_source}" "${signature}" notation_fixture)
endforeach()
foreach(signature
    "NotationAutomation::NotationAutomation(INotationUndoStackPtr undoStack)"
    "AutomationDataConstPtr NotationAutomation::automationData() const"
    "void NotationAutomation::editPoints(const AutomationCurveKey& key, AutomationPointEdits& edits)"
    "void NotationAutomation::setMasterScore(engraving::MasterScore* masterScore)")
    millerscore_append_method("${notation_source}" "${signature}" notation_fixture)
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/notationautomation_methods.cpp" "${notation_fixture}")

set(MILLERSCORE_PERFORMANCE_MODEL_SOURCE_OVERRIDE "" CACHE FILEPATH "Optional model source revision for numeric edit regression comparison")
set(performance_model "${catalog_model}")
if(MILLERSCORE_PERFORMANCE_MODEL_SOURCE_OVERRIDE)
    set(performance_model "${MILLERSCORE_PERFORMANCE_MODEL_SOURCE_OVERRIDE}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${performance_model}")
file(READ "${performance_model}" performance_source)
set(performance_fixture "#include \"dawperformance_fixture.h\"\nusing namespace mu;\nusing namespace mu::appshell;\nusing namespace mu::appshell::dawautomation;\n#define DawProjectModel PerformanceModelFixture\n")
foreach(signature
    "bool DawProjectModel::setPerformanceVelocity(const QString& noteId, int velocity)"
    "bool DawProjectModel::setPerformanceStartOffset(const QString& noteId, int offsetTicks)"
    "bool DawProjectModel::setPerformanceVelocities(const QVariantMap& velocities)"
    "bool DawProjectModel::setPerformancePitchOffsets(const QVariantMap& centsByNote)"
    "bool DawProjectModel::setPerformanceStartOffsets(const QVariantMap& offsetsByNote)")
    millerscore_append_method("${performance_source}" "${signature}" performance_fixture)
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/performance_methods.cpp" "${performance_fixture}")

# Exercise the unchanged trailing Note-property dispatch and spanner helper.
# Earlier note-specific branches and real XML/engraving services are excluded.
set(MILLERSCORE_NOTE_READER_SOURCE_OVERRIDE "" CACHE FILEPATH "Optional Note-reader revision for focused dispatch regression comparison")
set(note_reader "${MILLERSCORE_SOURCE_ROOT}/src/engraving/rw/read500/tread.cpp")
if(MILLERSCORE_NOTE_READER_SOURCE_OVERRIDE)
    set(note_reader "${MILLERSCORE_NOTE_READER_SOURCE_OVERRIDE}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${note_reader}")
file(READ "${note_reader}" note_reader_source)
set(note_dispatch "#include \"notereaderdispatch_fixture.h\"\nusing namespace mu::engraving;\n")
millerscore_append_method("${note_reader_source}"
    "static Spanner* tryCreateSpanner(const AsciiStringView& tag, ReadContext& ctx, bool requireNoteAnchor)" note_dispatch)
string(FIND "${note_reader_source}" "bool TRead::readProperties(Note* n, XmlReader& e, ReadContext& ctx)" note_start)
if(note_start LESS 0)
    message(FATAL_ERROR "Cannot locate production Note property reader")
endif()
string(SUBSTRING "${note_reader_source}" ${note_start} -1 note_tail)
set(note_marker "        n->add(pt);\n    ")
string(FIND "${note_tail}" "${note_marker}" dispatch_start)
string(FIND "${note_tail}" "\n}\n" dispatch_end)
if(dispatch_start LESS 0 OR dispatch_end LESS dispatch_start)
    message(FATAL_ERROR "Cannot locate final Note reader dispatch")
endif()
string(LENGTH "${note_marker}" note_marker_length)
math(EXPR dispatch_start "${dispatch_start}+${note_marker_length}")
math(EXPR dispatch_length "${dispatch_end}+3-${dispatch_start}")
string(SUBSTRING "${note_tail}" ${dispatch_start} ${dispatch_length} dispatch_source)
string(REGEX REPLACE "^} else if" "if" dispatch_source "${dispatch_source}")
string(APPEND note_dispatch
    "bool TRead::trailingNoteProperties(Note* n, XmlReader& e, ReadContext& ctx) {\n"
    "    const AsciiStringView tag(e.tag);\n    ${dispatch_source}\n")
file(WRITE "${CMAKE_BINARY_DIR}/notereaderdispatch_methods.cpp" "${note_dispatch}")

add_executable(daw_tests
    testmain.cpp dawautomation_tests.cpp dawautomationlane_tests.cpp midicontrollerselector_tests.cpp dawmissingmedia_tests.cpp
    notationautomation_tests.cpp dawperformance_tests.cpp notereaderdispatch_tests.cpp midicontrollerplayback_tests.cpp
    "${CMAKE_BINARY_DIR}/catalog_projection.cpp"
    "${CMAKE_BINARY_DIR}/missingmedia_methods.cpp"
    "${CMAKE_BINARY_DIR}/notationautomation_methods.cpp"
    "${CMAKE_BINARY_DIR}/performance_methods.cpp"
    "${CMAKE_BINARY_DIR}/notereaderdispatch_methods.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/src/engraving/tests/automation/midicontrollercatalog_tests.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/src/engraving/tests/automation/automationrw_tests.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/src/engraving/tests/automation/automation_tests.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/src/engraving/automation/automationdata.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/src/engraving/automation/internal/automationrw.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/src/engraving/infrastructure/eid.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/src/engraving/dom/tempotimeline.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/muse/framework/global/types/string.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/muse/framework/global/types/id.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/muse/framework/global/types/bytearray.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/muse/framework/global/serialization/json.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/muse/framework/global/thirdparty/kors_async/async/internal/conf.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/muse/framework/global/thirdparty/kors_async/async/internal/queuepool.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/muse/framework/global/thirdparty/kors_logger/src/logger.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/muse/framework/global/thirdparty/kors_logger/src/logdefdest.cpp"
    "${MILLERSCORE_SOURCE_ROOT}/muse/framework/global/thirdparty/kors_profiler/profiler/profiler.cpp"
)
target_include_directories(daw_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${MILLERSCORE_SOURCE_ROOT}/src" "${MILLERSCORE_SOURCE_ROOT}/src/engraving"
    "${MILLERSCORE_SOURCE_ROOT}/src/engraving/tests" "${MILLERSCORE_SOURCE_ROOT}/muse/framework"
    "${MILLERSCORE_SOURCE_ROOT}/muse/framework/global"
    "${MILLERSCORE_PICOJSON_INCLUDE_DIR}" "${MILLERSCORE_UTFCPP_INCLUDE_DIR}"
)
target_compile_definitions(daw_tests PRIVATE KORS_LOGGER_QT_SUPPORT MILLERSCORE_TEST_SOURCE_ROOT="${MILLERSCORE_SOURCE_ROOT}")
if(MSVC)
    target_compile_options(daw_tests PRIVATE /utf-8 /EHsc)
endif()
target_link_libraries(daw_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml GTest::gtest)
add_test(NAME daw_tests COMMAND daw_tests)

add_executable(midi_controller_coverage midicontrollercoverage.cpp)
target_include_directories(midi_controller_coverage PRIVATE "${MILLERSCORE_SOURCE_ROOT}/src")
target_link_libraries(midi_controller_coverage PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(midi_controller_coverage PRIVATE /utf-8 /EHsc)
endif()
add_test(NAME midi_controller_coverage COMMAND midi_controller_coverage "${CMAKE_BINARY_DIR}/midi-cc-coverage.json")
