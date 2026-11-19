#include "analysis/byte_tracker.h"

#include <algorithm>
#include <cmath>

namespace zp {

namespace {

float iou(const QRectF &a, const QRectF &b)
{
    const QRectF inter = a.intersected(b);
    if (inter.isEmpty()) {
        return 0.f;
    }
    const double i = inter.width() * inter.height();
    const double u = a.width() * a.height() + b.width() * b.height() - i;
    return u > 0.0 ? float(i / u) : 0.f;
}

} // namespace

ByteTracker::ByteTracker() = default;

void ByteTracker::reset()
{
    m_tracks.clear();
}

QRectF ByteTracker::predict(const Track &t, double dt) const
{
    return t.rect.translated(t.vel.x() * dt, t.vel.y() * dt);
}

void ByteTracker::touch(Track &t, const AnalysisBox &box, double dt)
{
    const QPointF prevCenter = t.rect.center();
    // Blend prediction and measurement: detectors jitter a few percent, and a
    // moving average keeps the drawn box stable while the target moves.
    QRectF blended(t.rect.x() * 0.4 + box.rect.x() * 0.6,
                   t.rect.y() * 0.4 + box.rect.y() * 0.6,
                   t.rect.width() * 0.4 + box.rect.width() * 0.6,
                   t.rect.height() * 0.4 + box.rect.height() * 0.6);
    if (dt > 1e-3) {
        const QPointF v = (blended.center() - prevCenter) / dt;
        // Smooth velocity too; a single outlier detection must not teleport it.
        t.vel = t.vel * 0.5 + v * 0.5;
    }
    t.rect = blended;
    t.score = std::max(t.score, box.score);
    ++t.hits;
    t.missed = 0;
}

void ByteTracker::update(QVector<AnalysisBox> &boxes, double dt)
{
    if (dt <= 0.0 || dt > 2.0) {
        dt = 0.25;
    }

    // ---- 1. predict every track forward ----
    QVector<QRectF> predicted(m_tracks.size());
    for (int i = 0; i < m_tracks.size(); ++i) {
        predicted[i] = predict(m_tracks[i], dt);
        ++m_tracks[i].missed;
    }

    // ---- 2. split boxes by confidence ----
    QVector<int> high, low;
    for (int i = 0; i < boxes.size(); ++i) {
        (boxes[i].score >= m_highThresh ? high : low).append(i);
    }
    // Boxes below the low threshold are ignored for association.
    low.erase(std::remove_if(low.begin(), low.end(), [&](int i) {
                  return boxes[i].score < m_lowThresh;
              }),
              low.end());

    QVector<int> trackMatched(m_tracks.size(), -1);   // track -> box
    auto associate = [&](const QVector<int> &cand) {
        struct Pair { float iou; int box; int track; };
        QVector<Pair> pairs;
        for (int boxIdx : cand) {
            if (boxes[boxIdx].trackId >= 0) {
                continue;   // already matched in a previous stage
            }
            for (int t = 0; t < m_tracks.size(); ++t) {
                if (trackMatched[t] >= 0 || m_tracks[t].classId != boxes[boxIdx].classId) {
                    continue;
                }
                const float v = iou(boxes[boxIdx].rect, predicted[t]);
                if (v >= m_iouThresh) {
                    pairs.append({v, boxIdx, t});
                }
            }
        }
        std::sort(pairs.begin(), pairs.end(),
                  [](const Pair &a, const Pair &b) { return a.iou > b.iou; });
        for (const Pair &p : pairs) {
            if (trackMatched[p.track] >= 0 || boxes[p.box].trackId >= 0) {
                continue;
            }
            trackMatched[p.track] = p.box;
            boxes[p.box].trackId = m_tracks[p.track].id;
            touch(m_tracks[p.track], boxes[p.box], dt);
            // Later stages must compare against the updated position.
            predicted[p.track] = m_tracks[p.track].rect;
        }
    };

    // ---- 3. ByteTrack's two stages ----
    associate(high);
    associate(low);

    // ---- 4. unmatched high boxes start new tracks ----
    for (int i : high) {
        if (boxes[i].trackId >= 0) {
            continue;
        }
        Track t;
        t.id = m_nextId++;
        t.classId = boxes[i].classId;
        t.rect = boxes[i].rect;
        t.score = boxes[i].score;
        t.hits = 1;
        m_tracks.append(t);
        boxes[i].trackId = t.id;
    }

    // ---- 5. drop tracks nobody has seen for a while ----
    QVector<Track> alive;
    alive.reserve(m_tracks.size());
    for (const Track &t : m_tracks) {
        // A track that never got a second hit and is already missing was just
        // noise; a confirmed track survives a few misses.
        const int limit = (t.hits <= 1) ? 2 : m_maxMissed;
        if (t.missed <= limit) {
            alive.append(t);
        }
    }
    m_tracks = alive;
}

} // namespace zp
