/* SPDX-License-Identifier: GPL-3.0-only */

#include "dawtransportmodel.h"

#include <algorithm>

#include <QMetaObject>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/sig.h"
#include "notation/imasternotation.h"
#include "notation/inotationplayback.h"

using namespace muse;
using namespace mu;
using namespace mu::appshell;

DawTransportModel::DawTransportModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void DawTransportModel::load()
{
    subscribeToProject();
    globalContext()->currentMasterNotationChanged().onNotify(this, [this]() { subscribeToProject(); });

    const context::IPlaybackStatePtr state = globalContext()->playbackState();
    if (state) {
        state->playbackStatusChanged().onReceive(this, [this](audio::PlaybackStatus) { emit statusChanged(); });
        state->playbackPositionChanged().onReceive(this, [this](audio::secs_t secs) {
            const notation::IMasterNotationPtr master = globalContext()->currentMasterNotation();
            if (master && master->playback()) {
                setTick(int(master->playback()->secToTick(secs)));
            }
        });
    }
}

void DawTransportModel::subscribeToProject()
{
    m_projectScope = std::make_unique<muse::async::Asyncable>();
    rebuildMeasures();
    setTick(0);

    const notation::IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (!master || !master->masterScore()) {
        return;
    }
    master->masterScore()->changesChannel().onReceive(m_projectScope.get(), [this](const engraving::ScoreChanges&) {
        scheduleRebuild();
    });
}

void DawTransportModel::scheduleRebuild()
{
    if (m_rebuildPending) {
        return;
    }
    m_rebuildPending = true;
    QMetaObject::invokeMethod(this, [this]() {
        m_rebuildPending = false;
        rebuildMeasures();
    }, Qt::QueuedConnection);
}

void DawTransportModel::rebuildMeasures()
{
    m_measures.clear();
    m_measuresForQml.clear();

    const notation::IMasterNotationPtr master = globalContext()->currentMasterNotation();
    const engraving::MasterScore* score = master ? master->masterScore() : nullptr;
    if (score) {
        for (const engraving::Measure* measure = score->firstMeasure(); measure; measure = measure->nextMeasure()) {
            MeasureInfo info;
            info.startTick = measure->tick().ticks();
            info.endTick = measure->endTick().ticks();
            const engraving::Fraction signature = measure->timesig();
            info.numerator = std::max(1, signature.numerator());
            info.denominator = std::max(1, signature.denominator());
            info.beatTicks = std::max(1, engraving::TimeSigFrac(info.numerator, info.denominator).beatTicks());
            // Pickups and other measures excluded from numbering have no number,
            // matching what the score shows.
            info.label = measure->excludeFromNumbering() ? QString() : QString::number(measure->measureNumber() + 1);
            m_measures.append(info);
            m_measuresForQml.append(QVariantMap {
                { "startTick", info.startTick }, { "endTick", info.endTick }, { "numerator", info.numerator },
                { "denominator", info.denominator }, { "beatTicks", info.beatTicks }, { "label", info.label }
            });
        }
    }
    emit measuresChanged();
    emit tickChanged();
}

void DawTransportModel::setTick(int tick)
{
    tick = std::max(0, tick);
    if (m_tick == tick) {
        return;
    }
    m_tick = tick;
    emit tickChanged();
}

const DawTransportModel::MeasureInfo* DawTransportModel::measureAt(int tick) const
{
    if (m_measures.isEmpty()) {
        return nullptr;
    }
    const auto it = std::upper_bound(m_measures.cbegin(), m_measures.cend(), tick, [](int value, const MeasureInfo& measure) {
        return value < measure.startTick;
    });
    return it == m_measures.cbegin() ? &m_measures.first() : &*(it - 1);
}

int DawTransportModel::measureIndexAt(int tick) const
{
    const MeasureInfo* measure = measureAt(tick);
    return measure ? int(measure - m_measures.data()) : -1;
}

void DawTransportModel::seekToTick(int tick)
{
    const notation::IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (!master || m_measures.isEmpty()) {
        return;
    }
    tick = std::clamp(tick, 0, std::max(0, endTick() - 1));
    playbackController()->seekRawTick(tick);
}

QString DawTransportModel::positionTextForTick(int tick) const
{
    const MeasureInfo* measure = measureAt(tick);
    if (!measure) {
        return QStringLiteral("1.1.000");
    }
    const int offset = std::max(0, tick - measure->startTick);
    const int beat = offset / measure->beatTicks + 1;
    const int remainder = offset % measure->beatTicks;
    const QString bar = measure->label.isEmpty() ? QStringLiteral("0") : measure->label;
    return QStringLiteral("%1.%2.%3").arg(bar).arg(beat).arg(remainder, 3, 10, QChar('0'));
}

int DawTransportModel::snapTick(int tick, int snapTicks, bool roundToNearest) const
{
    tick = std::max(0, tick);
    const MeasureInfo* measure = measureAt(tick);
    if (!measure || snapTicks < 1) {
        return tick;
    }
    const int offset = tick - measure->startTick;
    const int steps = roundToNearest ? (offset + snapTicks / 2) / snapTicks : offset / snapTicks;
    return std::min(measure->startTick + steps * snapTicks, measure->endTick);
}

int DawTransportModel::tick() const { return m_tick; }

bool DawTransportModel::playing() const
{
    const context::IPlaybackStatePtr state = globalContext()->playbackState();
    return state && state->playbackStatus() == audio::PlaybackStatus::Running;
}

bool DawTransportModel::paused() const
{
    const context::IPlaybackStatePtr state = globalContext()->playbackState();
    return state && state->playbackStatus() == audio::PlaybackStatus::Paused;
}

QVariantList DawTransportModel::measures() const { return m_measuresForQml; }
int DawTransportModel::endTick() const { return m_measures.isEmpty() ? 0 : m_measures.last().endTick; }
