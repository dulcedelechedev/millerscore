/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

#include <map>

#include <QObject>
#include <QString>
#include <qqmlintegration.h>

#include "modularity/ioc.h"

#include "audio/main/iplayback.h"
#include "audio/common/audiotypes.h"

#include "abstractaudioresourceitem.h"

#if (defined(_MSCVER) || defined(_MSC_VER))
// unreferenced function with internal linkage has been removed
#pragma warning(disable: 4505)
#endif

namespace mu::playback {
class OutputResourceItem : public AbstractAudioResourceItem
{
    Q_OBJECT

    Q_PROPERTY(QString id READ id NOTIFY fxParamsChanged)
    Q_PROPERTY(bool isActive READ isActive WRITE setIsActive NOTIFY isActiveChanged)
    Q_PROPERTY(bool isMissing READ isMissing NOTIFY isMissingChanged)

    QML_ELEMENT;
    QML_UNCREATABLE("Must be created in C++ only")

    muse::ContextInject<muse::audio::IPlayback> playback = { this };

public:
    explicit OutputResourceItem(QObject* parent, const muse::audio::AudioFxParams& params);

    void requestAvailableResources() override;
    void handleMenuItem(const QString& menuItemId) override;

    const muse::audio::AudioFxParams& params() const;
    void setParams(const muse::audio::AudioFxParams& params);

    QString title() const override;
    bool isBlank() const override;
    bool isActive() const override;
    bool hasNativeEditorSupport() const override;

    QString id() const;

    //! True when the slot keeps an effect that the installed catalog no
    //! longer provides. The slot, its settings and its opaque state are kept;
    //! only an explicit user choice replaces or removes it.
    bool isMissing() const;
    void refreshAvailability();

    //! Whether the channel has a slot above/below this one to move the effect to.
    void setMoveAvailability(bool canMoveUp, bool canMoveDown);

public slots:
    void setIsActive(bool newIsActive);

signals:
    void fxParamsChanged();
    void isMissingChanged();
    //! -1 moves the effect one slot up (processed earlier), +1 one slot down.
    void moveRequested(int delta);

private:
    void updateCurrentFxParams(const muse::audio::AudioResourceMeta& newMeta);
    void updateAvailableFxVendorsMap(const muse::audio::AudioResourceMetaList& availableFxResources);
    void updateMissing(const muse::audio::AudioResourceMetaList& availableFxResources);

    std::map<muse::audio::AudioResourceVendor, muse::audio::AudioResourceMetaList> m_fxByVendorMap;

    muse::audio::AudioFxParams m_currentFxParams;
    bool m_missing = false;
    bool m_canMoveUp = false;
    bool m_canMoveDown = false;
};
}
