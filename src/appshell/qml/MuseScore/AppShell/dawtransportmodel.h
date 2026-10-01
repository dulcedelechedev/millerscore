/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <memory>

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "modularity/ioc.h"
#include "playback/iplaybackcontroller.h"

namespace mu::engraving {
class MasterScore;
class Measure;
}

namespace mu::appshell {
//! Presentation adapter over the one shared transport and the score's own
//! measure/time-signature map. It owns no clock: the tick is converted from the
//! shared player's position, and seeking is delegated to PlaybackController.
//! Arranger and piano roll both draw their grid from `measures`, so they
//! cannot disagree about bars, meters or pickups.
class DawTransportModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(int tick READ tick NOTIFY tickChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY statusChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY statusChanged)
    Q_PROPERTY(QVariantList measures READ measures NOTIFY measuresChanged)
    Q_PROPERTY(int endTick READ endTick NOTIFY measuresChanged)

    QML_ELEMENT

public:
    explicit DawTransportModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void seekToTick(int tick);
    Q_INVOKABLE QString positionTextForTick(int tick) const;
    //! Snaps relative to the containing measure's start, so grids stay aligned
    //! after pickups and odd meters. `roundToNearest=false` floors to the cell.
    Q_INVOKABLE int snapTick(int tick, int snapTicks, bool roundToNearest) const;
    Q_INVOKABLE int measureIndexAt(int tick) const;

    int tick() const;
    bool playing() const;
    bool paused() const;
    QVariantList measures() const;
    int endTick() const;

signals:
    void tickChanged();
    void statusChanged();
    void measuresChanged();

private:
    struct MeasureInfo {
        int startTick = 0;
        int endTick = 0;
        int numerator = 4;
        int denominator = 4;
        int beatTicks = 480;
        QString label;
    };

    void subscribeToProject();
    void scheduleRebuild();
    void rebuildMeasures();
    void setTick(int tick);
    const MeasureInfo* measureAt(int tick) const;

    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<playback::IPlaybackController> playbackController = { this };

    std::unique_ptr<muse::async::Asyncable> m_projectScope;
    QList<MeasureInfo> m_measures;
    QVariantList m_measuresForQml;
    int m_tick = 0;
    bool m_rebuildPending = false;
};
}
