#include "outputresourceitem.h"

#include <algorithm>

#include <QList>

#include "audio/common/audioutils.h"
#include "vst/vstpluginattrs.h"

#include "log.h"
#include "translation.h"
#include "stringutils.h"

using namespace mu::playback;
using namespace muse;
using namespace muse::audio;

static const QString& NO_FX_MENU_ITEM_ID()
{
    static std::string id = muse::trc("playback", "No effect");
    static QString resultStr = QString::fromStdString(id);
    return resultStr;
}

static const QString MOVE_UP_MENU_ITEM_ID("moveEffectUp");
static const QString MOVE_DOWN_MENU_ITEM_ID("moveEffectDown");

namespace {
bool sameResourceIdentity(const AudioResourceMeta& left, const AudioResourceMeta& right)
{
    // Resolvers locate an effect by backend type and opaque id; descriptive
    // metadata may change after a rescan without the effect going missing.
    return left.type == right.type && left.id == right.id;
}
}

OutputResourceItem::OutputResourceItem(QObject* parent, const audio::AudioFxParams& params)
    : AbstractAudioResourceItem(parent),
    m_currentFxParams(params)
{
}

void OutputResourceItem::requestAvailableResources()
{
    playback()->availableOutputResources()
    .onResolve(this, [this](const AudioResourceMetaList& availableFxResources) {
        updateAvailableFxVendorsMap(availableFxResources);
        updateMissing(availableFxResources);

        QVariantList result;

        if (!isBlank()) {
            const QString& currentResourceId = QString::fromStdString(m_currentFxParams.resourceMeta.id);
            result << buildMenuItem(currentResourceId,
                                    title(),
                                    /*checked*/ true,
                                    /*subItems*/ QVariantList(),
                                    /*includeInFilteredLists*/ false);

            result << buildSeparator();

            // Effects run from the top slot down; moving changes that order.
            if (m_canMoveUp) {
                result << buildMenuItem(MOVE_UP_MENU_ITEM_ID, muse::qtrc("playback", "Move up"),
                                        /*checked*/ false, QVariantList(), /*includeInFilteredLists*/ false);
            }
            if (m_canMoveDown) {
                result << buildMenuItem(MOVE_DOWN_MENU_ITEM_ID, muse::qtrc("playback", "Move down"),
                                        /*checked*/ false, QVariantList(), /*includeInFilteredLists*/ false);
            }
            if (m_canMoveUp || m_canMoveDown) {
                result << buildSeparator();
            }
        }

        // add "no fx" item
        result << buildMenuItem(NO_FX_MENU_ITEM_ID(),
                                NO_FX_MENU_ITEM_ID(),
                                m_currentFxParams.resourceMeta.id.empty(),
                                /*subItems*/ QVariantList(),
                                /*includeInFilteredLists*/ false);

        if (!m_fxByVendorMap.empty()) {
            result << buildSeparator();
        }

        for (const auto& pair : m_fxByVendorMap) {
            const QString& vendor = QString::fromStdString(pair.first);

            QVariantList subItems;

            for (const AudioResourceMeta& fxResourceMeta : pair.second) {
                const QString& resourceId = QString::fromStdString(fxResourceMeta.id);
                subItems << buildMenuItem(resourceId,
                                          resourceId,
                                          m_currentFxParams.resourceMeta.id == fxResourceMeta.id);
            }

            result << buildMenuItem(vendor,
                                    vendor,
                                    m_currentFxParams.resourceMeta.vendor == pair.first,
                                    subItems,
                                    /*includeInFilteredLists*/ false,
                                    /*isFilterCategory*/ true);
        }

        // No store/promotion entry: MillerScore never opens a remote page
        // from the effect menu.
        emit availableResourceListResolved(result);
    })
    .onReject(this, [](const int errCode, const std::string& errText) {
        LOGE() << "Unable to resolve available output resources"
               << " , errCode:" << errCode
               << " , errText:" << errText;
    });
}

void OutputResourceItem::handleMenuItem(const QString& menuItemId)
{
    if (menuItemId == NO_FX_MENU_ITEM_ID()) {
        updateCurrentFxParams(AudioResourceMeta());
        return;
    }

    if (menuItemId == MOVE_UP_MENU_ITEM_ID) {
        emit moveRequested(-1);
        return;
    }

    if (menuItemId == MOVE_DOWN_MENU_ITEM_ID) {
        emit moveRequested(+1);
        return;
    }

    const audioplugins::PluginResourceId& newSelectedResourceId = menuItemId.toStdString();

    for (const auto& pair : m_fxByVendorMap) {
        for (const AudioResourceMeta& fxResourceMeta : pair.second) {
            if (newSelectedResourceId != fxResourceMeta.id) {
                continue;
            }

            updateCurrentFxParams(fxResourceMeta);
        }
    }
}

const AudioFxParams& OutputResourceItem::params() const
{
    return m_currentFxParams;
}

void OutputResourceItem::setParams(const audio::AudioFxParams& params)
{
    if (m_currentFxParams == params) {
        return;
    }

    bool activeChanged = m_currentFxParams.active != params.active;
    bool resourceChanged = m_currentFxParams.resourceMeta.id != params.resourceMeta.id;
    bool blankChanged = m_currentFxParams.isValid() != params.isValid();

    m_currentFxParams = params;
    emit fxParamsChanged();

    if (activeChanged) {
        emit isActiveChanged();
    }

    if (resourceChanged) {
        emit titleChanged();
        refreshAvailability();
    }

    if (blankChanged) {
        emit isBlankChanged();
    }
}

QString OutputResourceItem::title() const
{
    const QString name = QString::fromStdString(m_currentFxParams.resourceMeta.id);
    // The prefix comes first so it survives eliding in narrow mixer slots.
    return m_missing ? muse::qtrc("playback", "Missing: %1").arg(name) : name;
}

void OutputResourceItem::setMoveAvailability(bool canMoveUp, bool canMoveDown)
{
    m_canMoveUp = canMoveUp;
    m_canMoveDown = canMoveDown;
}

bool OutputResourceItem::isMissing() const
{
    return m_missing;
}

void OutputResourceItem::refreshAvailability()
{
    if (isBlank()) {
        updateMissing({});
        return;
    }

    playback()->availableOutputResources()
    .onResolve(this, [this](const AudioResourceMetaList& availableFxResources) {
        updateMissing(availableFxResources);
    })
    .onReject(this, [](const int errCode, const std::string& errText) {
        // An unreadable catalog is not evidence that the effect is absent.
        LOGE() << "Unable to check effect availability, errCode: " << errCode << ", errText: " << errText;
    });
}

void OutputResourceItem::updateMissing(const AudioResourceMetaList& availableFxResources)
{
    const AudioResourceMeta& current = m_currentFxParams.resourceMeta;
    const bool missing = current.isValid()
                         && std::none_of(availableFxResources.cbegin(), availableFxResources.cend(),
                                         [&current](const AudioResourceMeta& resource) {
        return sameResourceIdentity(resource, current);
    });

    if (m_missing == missing) {
        return;
    }

    m_missing = missing;
    emit isMissingChanged();
    emit titleChanged();
    emit hasNativeEditorSupportChanged();
}

bool OutputResourceItem::isActive() const
{
    return m_currentFxParams.active;
}

QString OutputResourceItem::id() const
{
    return QString::number(m_currentFxParams.chainOrder);
}

void OutputResourceItem::setIsActive(bool newIsActive)
{
    if (m_currentFxParams.active == newIsActive) {
        return;
    }

    m_currentFxParams.active = newIsActive;

    emit isActiveChanged();
    emit fxParamsChanged();
}

void OutputResourceItem::updateCurrentFxParams(const AudioResourceMeta& newMeta)
{
    if (m_currentFxParams.resourceMeta == newMeta) {
        return;
    }

    requestToCloseNativeEditorView();

    audio::AudioFxParams newParams = m_currentFxParams;
    newParams.categories = audio::audioFxCategoriesFromString(newMeta.attributeVal(vst::CATEGORIES_ATTRIBUTE));
    newParams.resourceMeta = newMeta;
    newParams.configuration.clear();
    newParams.active = newMeta.isValid();

    setParams(newParams);
    requestToLaunchNativeEditorView();
}

void OutputResourceItem::updateAvailableFxVendorsMap(const audio::AudioResourceMetaList& availableFxResources)
{
    m_fxByVendorMap.clear();

    for (const auto& meta : availableFxResources) {
        AudioResourceMetaList& fxResourceList = m_fxByVendorMap[meta.vendor];
        fxResourceList.push_back(meta);
    }

    for (auto& [vendor, fxResourceList] : m_fxByVendorMap) {
        sortResourcesList(fxResourceList);
    }
}

bool OutputResourceItem::isBlank() const
{
    return !m_currentFxParams.isValid();
}

bool OutputResourceItem::hasNativeEditorSupport() const
{
    return !m_missing && muse::audio::hasNativeEditorSupport(m_currentFxParams.resourceMeta);
}
