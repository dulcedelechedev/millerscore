/* SPDX-License-Identifier: GPL-3.0-only */

#include "dawinstrumentmodel.h"

#include <algorithm>

#include "actions/actiontypes.h"
#include "audio/common/audioutils.h"
#include "global/translation.h"
#include "project/inotationproject.h"
#include "project/iprojectaudiosettings.h"
#include "types/val.h"

using namespace muse;
using namespace muse::audio;
using namespace mu;
using namespace mu::appshell;

namespace {
const std::string VST_INSTRUMENT_EDITOR_ACTION("action://vst/instrument_editor");

int backendOrder(const AudioResourceMeta& resource)
{
    switch (resourceTypeFromString(resource.type)) {
    case AudioResourceType::MuseSamplerSoundPack: return 0;
    case AudioResourceType::FluidSoundfont: return 1;
    case AudioResourceType::VstPlugin: return 2;
    case AudioResourceType::NativeEffect:
    case AudioResourceType::Undefined: return 3;
    }
    return 3;
}

bool sameResourceIdentity(const AudioResourceMeta& left, const AudioResourceMeta& right)
{
    // Descriptive attributes (name, category, editor support) may legitimately
    // change after a rescan or plug-in update. The resolvers use the opaque id
    // and backend type to locate a resource, so missing/selected state must use
    // that same stable identity instead of full metadata equality.
    return left.type == right.type && left.id == right.id;
}
}

DawInstrumentModel::DawInstrumentModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void DawInstrumentModel::load()
{
    refreshResources();

    soundFontInstallScenario()->soundFontInstalled().onNotify(this, [this]() {
        refreshResources();
    });

    // The track itself is chosen by DawTracksModel; never reset it here, the
    // tracks model may already have selected a track of the new project.
    globalContext()->currentProjectChanged().onNotify(this, [this]() {
        reloadCurrentParams();
        refreshResources();
    });

    // Assignments are applied when playback tracks are created after loading.
    playbackController()->trackAdded().onReceive(this, [this](TrackId) {
        reloadCurrentParams();
        emit trackChanged();
        updateState();
        emit resourcesChanged();
        emit currentInstrumentChanged();
    });
    playbackController()->trackRemoved().onReceive(this, [this](TrackId) {
        emit trackChanged();
        updateState();
    });
    audioPlayback()->sourceParamsChanged().onReceive(this, [this](TrackId trackId, const AudioSourceParams& params) {
        if (trackId != runtimeTrackId()) {
            return;
        }
        m_currentParams = params;
        m_switching = false;
        updateState();
        emit resourcesChanged();
        emit currentInstrumentChanged();
    });
}

bool DawInstrumentModel::setTrack(const QString& partId, const QString& instrumentId)
{
    const engraving::InstrumentTrackId candidate { muse::ID(partId), muse::String::fromQString(instrumentId) };
    if (candidate == m_track) {
        return true;
    }
    m_track = candidate;
    m_switching = false;
    reloadCurrentParams();
    emit trackChanged();
    updateState();
    emit resourcesChanged();
    emit currentInstrumentChanged();
    return true;
}

void DawInstrumentModel::reloadCurrentParams()
{
    const project::INotationProjectPtr project = globalContext()->currentProject();
    m_currentParams = project && project->audioSettings() && m_track.isValid()
                      ? project->audioSettings()->trackInputParams(m_track) : AudioInputParams {};
}

void DawInstrumentModel::installSoundFont()
{
    interactive()->selectOpeningFile(muse::trc("appshell", "Install SoundFont"), {}, {
        muse::trc("appshell", "SoundFont files (*.sf2 *.sf3)")
    }).onResolve(this, [this](const io::path_t& path) {
        if (!path.empty()) {
            soundFontInstallScenario()->installSoundFont(synth::SoundFontUri::fromLocalFile(path));
        }
    });
}

void DawInstrumentModel::refreshResources()
{
    m_resourcesResolved = false;
    m_resourcesFailed = false;
    updateState();

    audioPlayback()->availableInputResources()
    .onResolve(this, [this](const AudioResourceMetaList& resources) {
        m_resources = resources;
        std::stable_sort(m_resources.begin(), m_resources.end(), [](const AudioResourceMeta& left, const AudioResourceMeta& right) {
            if (backendOrder(left) != backendOrder(right)) {
                return backendOrder(left) < backendOrder(right);
            }
            const int vendor = QString::fromStdString(left.vendor).compare(QString::fromStdString(right.vendor), Qt::CaseInsensitive);
            if (vendor != 0) {
                return vendor < 0;
            }
            return resourceTitle(left).compare(resourceTitle(right), Qt::CaseInsensitive) < 0;
        });
        m_resourcesResolved = true;
        m_resourcesFailed = false;
        emit resourcesChanged();
        updateState();
        emit currentInstrumentChanged();
    })
    .onReject(this, [this](int, const std::string&) {
        m_resources.clear();
        m_resourcesResolved = true;
        m_resourcesFailed = true;
        emit resourcesChanged();
        updateState();
        emit currentInstrumentChanged();
    });
}

QVariantList DawInstrumentModel::resources() const
{
    QVariantList result;
    const AudioResourceMeta& current = m_currentParams.resourceMeta;
    const bool currentInstalled = !current.isValid() || !m_resourcesResolved || m_resourcesFailed
                                  || std::any_of(m_resources.cbegin(), m_resources.cend(), [&current](const AudioResourceMeta& resource) {
        return sameResourceIdentity(resource, current);
    });
    // A missing assignment stays visible and selected instead of silently
    // falling back to another sound.
    if (!currentInstalled) {
        result.append(QVariantMap {
            { "value", QString::fromStdString(current.id) },
            { "text", tr("%1 (not installed)").arg(resourceTitle(current)) },
            { "title", resourceTitle(current) },
            { "backend", backendTitle(current) },
            { "vendor", QString::fromStdString(current.vendor) },
            { "group", QString::fromStdString(current.vendor) },
            { "category", current.attributeVal(u"museCategory").toQString() },
            { "selected", true }, { "missing", true }
        });
    }
    for (size_t index = 0; index < m_resources.size(); ++index) {
        const AudioResourceMeta& resource = m_resources[index];
        const QString vendor = QString::fromStdString(resource.vendor);
        const QString title = resourceTitle(resource);
        const QString prefix = backendTitle(resource);
        const AudioResourceType type = resourceTypeFromString(resource.type);
        const QString group = type == AudioResourceType::FluidSoundfont
                              ? resource.attributeVal(u"soundFontName").toQString() : vendor;
        result.append(QVariantMap {
            // The catalog may contain the same opaque id in different
            // backends. A per-snapshot index keeps the menu id unambiguous;
            // the selected metadata is still copied from the shared catalog.
            { "value", QStringLiteral("resource:%1").arg(int(index)) },
            { "text", vendor.isEmpty() || vendor == title
              ? QStringLiteral("%1 · %2").arg(prefix, title)
              : QStringLiteral("%1 · %2 · %3").arg(prefix, vendor, title) },
            { "title", title }, { "backend", prefix }, { "vendor", vendor },
            { "group", group }, { "category", resource.attributeVal(u"museCategory").toQString() },
            { "selected", sameResourceIdentity(resource, current) }, { "missing", false }
        });
    }
    return result;
}

int DawInstrumentModel::currentResourceIndex() const
{
    const AudioResourceMeta& current = m_currentParams.resourceMeta;
    if (!current.isValid()) {
        return -1;
    }
    const auto it = std::find_if(m_resources.cbegin(), m_resources.cend(), [&current](const AudioResourceMeta& resource) {
        return sameResourceIdentity(resource, current);
    });
    if (it == m_resources.cend()) {
        return m_resourcesResolved ? 0 : -1;
    }
    return int(it - m_resources.cbegin());
}

bool DawInstrumentModel::selectInstrument(const QString& resourceId)
{
    static const QString RESOURCE_PREFIX = QStringLiteral("resource:");
    bool indexOk = false;
    const int resourceIndex = resourceId.startsWith(RESOURCE_PREFIX)
                              ? resourceId.mid(RESOURCE_PREFIX.size()).toInt(&indexOk) : -1;
    const TrackId trackId = runtimeTrackId();
    if (!indexOk || resourceIndex < 0 || resourceIndex >= int(m_resources.size()) || trackId == INVALID_TRACK_ID) {
        return false;
    }

    const AudioResourceMeta& resource = m_resources.at(resourceIndex);
    AudioInputParams params = m_currentParams;
    if (params.resourceMeta == resource) {
        return true;
    }

    params.resourceMeta = resource;
    params.configuration.clear();
    m_currentParams = params;
    m_switching = true;
    updateState();
    emit resourcesChanged();
    emit currentInstrumentChanged();
    audioPlayback()->setSourceParams(trackId, params);
    return true;
}

bool DawInstrumentModel::openEditor()
{
    const TrackId trackId = runtimeTrackId();
    if (!editorAvailable()) {
        return false;
    }

    actions::ActionQuery action(VST_INSTRUMENT_EDITOR_ACTION);
    action.addParam("trackId", Val(trackId));
    action.addParam("resourceId", Val(m_currentParams.resourceMeta.id));
    dispatcher()->dispatch(action);
    return true;
}

QString DawInstrumentModel::currentTitle() const { return resourceTitle(m_currentParams.resourceMeta); }
QString DawInstrumentModel::currentBackend() const
{
    return m_currentParams.resourceMeta.isValid() ? backendTitle(m_currentParams.resourceMeta) : QString();
}

QString DawInstrumentModel::currentVendor() const { return QString::fromStdString(m_currentParams.resourceMeta.vendor); }
DawInstrumentModel::LoadState DawInstrumentModel::loadState() const { return m_loadState; }

bool DawInstrumentModel::editorAvailable() const
{
    return m_loadState == LoadState::Assigned && m_currentParams.type() == AudioSourceType::Vsti
           && hasNativeEditorSupport(m_currentParams.resourceMeta) && runtimeTrackId() != INVALID_TRACK_ID;
}

bool DawInstrumentModel::trackAvailable() const
{
    return runtimeTrackId() != INVALID_TRACK_ID;
}

void DawInstrumentModel::updateState()
{
    if (!trackAvailable()) {
        setLoadState(LoadState::NoTrack);
    } else if (!m_resourcesResolved) {
        setLoadState(LoadState::Scanning);
    } else if (m_resourcesFailed) {
        setLoadState(LoadState::Failed);
    } else if (m_switching) {
        setLoadState(LoadState::Switching);
    } else {
        const AudioResourceMeta& current = m_currentParams.resourceMeta;
        const bool missing = current.isValid()
                             && std::none_of(m_resources.cbegin(), m_resources.cend(), [&current](const AudioResourceMeta& resource) {
            return sameResourceIdentity(resource, current);
        });
        setLoadState(missing ? LoadState::Missing : LoadState::Assigned);
    }
}

void DawInstrumentModel::setLoadState(LoadState state)
{
    if (m_loadState == state) {
        return;
    }
    m_loadState = state;
    emit loadStateChanged();
}

TrackId DawInstrumentModel::runtimeTrackId() const
{
    const auto controller = playbackController();
    if (!controller) {
        return INVALID_TRACK_ID;
    }

    const auto& map = controller->instrumentTrackIdMap();
    const auto it = map.find(m_track);
    return it == map.end() ? INVALID_TRACK_ID : it->second;
}

QString DawInstrumentModel::resourceTitle(const AudioResourceMeta& resource)
{
    if (!resource.isValid()) {
        return {};
    }
    const muse::String& museName = resource.attributeVal(u"museName");
    if (!museName.empty()) {
        return museName.toQString();
    }
    if (resourceTypeFromString(resource.type) == AudioResourceType::FluidSoundfont) {
        const muse::String& presetName = resource.attributeVal(u"presetName");
        if (!presetName.empty()) {
            return presetName.toQString();
        }
        const muse::String& soundFontName = resource.attributeVal(u"soundFontName");
        if (!soundFontName.empty()) {
            return soundFontName.toQString();
        }
    }
    return QString::fromStdString(resource.id);
}

QString DawInstrumentModel::backendTitle(const AudioResourceMeta& resource)
{
    switch (resourceTypeFromString(resource.type)) {
    case AudioResourceType::MuseSamplerSoundPack: return QStringLiteral("Muse Sounds");
    case AudioResourceType::FluidSoundfont: return QStringLiteral("SoundFont");
    case AudioResourceType::VstPlugin: return QStringLiteral("VST3");
    case AudioResourceType::NativeEffect:
    case AudioResourceType::Undefined: return tr("Other");
    }
    return tr("Other");
}
