#pragma once

#include <cstdint>

// The same state machine can be exercised at compile time with C++17 tests.
#if __cplusplus >= 201703L
#define PAPERMONO_PAD_CX constexpr
#else
#define PAPERMONO_PAD_CX
#endif

namespace papermono {

struct PadConfig {
    int width = 800;
    int height = 480;
    int tapSlop = 8;
    int pointerStart = 1;
    int dragStart = 3;
    int scrollStart = 12;
    int scrollStep = 18;
    int maxFrameDelta = 128;
    int tipSeparation = 24;
    int edgeMargin = 36;
    int edgeTopMargin = 60;
    int edgeDragPixels = 8;
    uint32_t edgeDelayMs = 150;
    uint32_t edgeDragRepeatMs = 40;
    uint32_t edgeScrollRepeatMs = 100;
    uint32_t releaseDebounceMs = 30;
    uint32_t twoFingerGraceMs = 80;
    uint32_t doubleTapMs = 320;
    uint32_t dragHoldMs = 150;
    uint32_t holdMs = 600;
    uint32_t tipAnchorMs = 180;
    uint32_t tipTapMs = 320;
    uint32_t reportMs = 10;
    uint32_t pulseMs = 35;
    int slowGainQ8 = 256;
    int fastGainQ8 = 256;
    int slowSpeed = 180;
    int fastSpeed = 800;
    bool invertScroll = true;
    uint32_t inertiaHalfLifeMs = 0;
    uint32_t inertiaWindowMs = 80;
    uint32_t inertiaReleaseTimeoutMs = 120;
    uint32_t inertiaStartDelayMs = 20;
    uint32_t inertiaRepeatMs = 24;
    uint32_t inertiaMaxDurationMs = 1500;
    int inertiaStopSpeed = 2; // HID wheel counts/second, not sensor pixels.
    int inertiaMaxDelta = 4;
};

struct PadPoint {
    int32_t id = -1;
    int x = 0;
    int y = 0;
};

struct PadFrame {
    uint32_t sequence = 0;
    uint32_t ms = 0;
    uint8_t count = 0;
    PadPoint points[2]{};
    bool sync = true;
    bool reset = false;
};

PAPERMONO_PAD_CX int padAbs(int value) { return value < 0 ? -value : value; }
PAPERMONO_PAD_CX int padMax(int a, int b) { return a > b ? a : b; }
PAPERMONO_PAD_CX int padClamp(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}
PAPERMONO_PAD_CX int pointIndex(const PadFrame& frame, int32_t id) {
    for (int i = 0; i < frame.count; ++i) {
        if (frame.points[i].id == id) return i;
    }
    return -1;
}
PAPERMONO_PAD_CX int centerX(const PadFrame& frame) {
    return frame.count == 2 ? (frame.points[0].x + frame.points[1].x) / 2 : frame.points[0].x;
}
PAPERMONO_PAD_CX int centerY(const PadFrame& frame) {
    return frame.count == 2 ? (frame.points[0].y + frame.points[1].y) / 2 : frame.points[0].y;
}

// Established drag/scroll can arm on edge contact alone. Ordinary pointer
// motion never uses this timer.
struct PadEdge {
    int direction = 0;
    uint32_t enteredMs = 0, repeatedMs = 0;
    PAPERMONO_PAD_CX int update(int position, int extent, int delta, uint32_t now,
                               uint32_t repeatMs, int amount, const PadConfig& c,
                               bool armOnContact = false, int lowMargin = -1) {
        const int low = lowMargin < 0 ? c.edgeMargin : lowMargin;
        const int edge = c.edgeMargin <= 0 ? 0 : position < low ? -1 :
                         position >= extent - c.edgeMargin ? 1 : 0;
        if (!edge || delta * edge < 0) { direction = 0; return 0; }
        if (delta * edge > 0 || armOnContact) {
            if (direction != edge) {
                direction = edge; enteredMs = repeatedMs = now;
                return 0;
            }
        }
        if (direction != edge || now - enteredMs < c.edgeDelayMs ||
            now - repeatedMs < repeatMs) return 0;
        repeatedMs = now;
        return direction * amount;
    }
};

struct PadSnapshot {
    PadFrame frame{};
    uint32_t session = 0;
    uint32_t endMs = 0;
    bool began = false;
    bool ended = false;
    bool cancelled = false;
};

class PadSession {
public:
    PAPERMONO_PAD_CX bool consume(const PadFrame& frame, const PadConfig& cfg, PadSnapshot& out) {
        if (!frame.sync) return false;
        if (haveSequence_ && static_cast<int32_t>(frame.sequence - lastSequence_) <= 0) return false;
        lastSequence_ = frame.sequence;
        haveSequence_ = true;
        bool invalid = frame.reset || frame.count > 2;
        for (int i = 0; i < frame.count && i < 2; ++i) {
            const auto& point = frame.points[i];
            invalid |= point.id < 0 || point.x < 0 || point.x >= cfg.width ||
                       point.y < 0 || point.y >= cfg.height;
        }
        invalid |= frame.count == 2 && frame.points[0].id == frame.points[1].id;
        if (active_ && frame.count != 0 && !invalid) {
            bool shared = false;
            for (int i = 0; i < previous_.count; ++i) {
                shared |= pointIndex(frame, previous_.points[i].id) >= 0;
            }
            if (!shared && frame.ms - pairMs_ <= cfg.twoFingerGraceMs) {
                for (int i = 0; i < pair_.count; ++i)
                    shared |= pointIndex(frame, pair_.points[i].id) >= 0;
            }
            invalid = !shared;
        }
        out = PadSnapshot{};
        out.frame = frame;
        if (invalid) {
            out.session = session_;
            out.frame.count = 0;
            out.cancelled = out.ended = true;
            active_ = releasing_ = false;
            previous_ = PadFrame{};
            pair_ = PadFrame{};
            return true;
        }
        if (frame.count != 0) {
            if (!active_) {
                active_ = true;
                ++session_;
                out.began = true;
            }
            releasing_ = false;
            previous_ = frame;
            if (frame.count == 2) { pair_ = frame; pairMs_ = frame.ms; }
        } else {
            if (!active_) return false;
            if (!releasing_) {
                releasing_ = true;
                releaseMs_ = frame.ms;
            }
            if (frame.ms - releaseMs_ < cfg.releaseDebounceMs) return false;
            active_ = releasing_ = false;
            out.ended = true;
            out.endMs = releaseMs_;
            previous_ = PadFrame{};
            pair_ = PadFrame{};
        }
        out.session = session_;
        return true;
    }

    PAPERMONO_PAD_CX void cancel() {
        active_ = releasing_ = false;
        previous_ = PadFrame{};
        pair_ = PadFrame{};
    }

private:
    PadFrame previous_{};
    PadFrame pair_{};
    uint32_t pairMs_ = 0;
    uint32_t session_ = 0;
    uint32_t releaseMs_ = 0;
    uint32_t lastSequence_ = 0;
    bool active_ = false;
    bool releasing_ = false;
    bool haveSequence_ = false;
};

enum class PadRecognizer : uint8_t { TapDrag, Router, TipTap };
enum class PadOwner : uint8_t { None, Drag, Scroll, TipTap };
enum class PadPhase : uint8_t { Reserved, Active };
enum class PadActionKind : uint8_t {
    None, Motion, Scroll, Click, DeferredClick, DoubleClick, CancelClick, CommitClick, DragBegin, DragEnd
};
enum PadCapability : uint8_t { PadMotion = 1, PadWheel = 2, PadButtons = 4 };

struct PadAction {
    PadActionKind kind = PadActionKind::None;
    int x = 0;
    int y = 0;
    uint8_t button = 0;
};

struct PadProposal {
    uint32_t frameSequence = 0;
    uint32_t session = 0;
    uint32_t epoch = 0;
    PadRecognizer recognizer = PadRecognizer::TapDrag;
    PadOwner owner = PadOwner::None;
    uint8_t capabilities = 0;
    bool claim = false;
    bool withdraw = false;
    PadAction action{};
};

struct PadToken {
    uint32_t session = 0;
    uint32_t generation = 0;
    uint32_t epoch = 0;
    PadOwner owner = PadOwner::None;
    PadRecognizer recognizer = PadRecognizer::TapDrag;
    PadPhase phase = PadPhase::Reserved;
    uint8_t capabilities = 0;
    bool valid = false;
};

struct PadResolution {
    PadSnapshot snapshot{};
    PadToken token{};
    uint32_t epoch = 0;
    bool complete = false;
};

PAPERMONO_PAD_CX int ownerPriority(PadOwner owner) {
    return owner == PadOwner::Drag ? 90 : owner == PadOwner::Scroll ? 70 :
           owner == PadOwner::TipTap ? 60 : 0;
}

class PadArbiter {
public:
    PAPERMONO_PAD_CX void begin(const PadSnapshot& snapshot) {
        snapshot_ = snapshot;
        observed_ = 0;
        if (snapshot.began) token_.valid = false;
        if (snapshot.cancelled) cancel();
    }

    PAPERMONO_PAD_CX bool submit(const PadProposal& proposal) {
        const int index = static_cast<int>(proposal.recognizer);
        if (index < 0 || index >= 3 || (observed_ & (1U << index)) != 0 ||
            proposal.frameSequence != snapshot_.frame.sequence ||
            proposal.session != snapshot_.session || proposal.epoch != epoch_) return false;
        proposals_[index] = proposal;
        observed_ |= 1U << index;
        return true;
    }

    PAPERMONO_PAD_CX PadResolution resolve() {
        PadResolution result{};
        if (observed_ != 7) return result;
        if (!snapshot_.cancelled) {
            for (const auto& proposal : proposals_) {
                if (token_.valid && proposal.withdraw && proposal.recognizer == token_.recognizer &&
                    token_.phase == PadPhase::Reserved) token_.valid = false;
            }
            for (const auto& proposal : proposals_) {
                if (!proposal.claim || proposal.owner == PadOwner::None) continue;
                // Priorities choose a winner, not a replacement for a committed gesture.
                if (token_.valid && token_.phase == PadPhase::Active) continue;
                if (!token_.valid || ownerPriority(proposal.owner) > ownerPriority(token_.owner) ||
                    (ownerPriority(proposal.owner) == ownerPriority(token_.owner) &&
                     static_cast<int>(proposal.recognizer) < static_cast<int>(token_.recognizer))) {
                    token_.session = snapshot_.session;
                    token_.generation = ++generation_;
                    token_.epoch = epoch_;
                    token_.owner = proposal.owner;
                    token_.recognizer = proposal.recognizer;
                    token_.capabilities = proposal.capabilities;
                    token_.phase = proposal.owner == PadOwner::Drag ? PadPhase::Reserved : PadPhase::Active;
                    token_.valid = true;
                }
            }
        }
        result.snapshot = snapshot_;
        result.token = token_;
        result.epoch = epoch_;
        result.complete = true;
        return result;
    }

    PAPERMONO_PAD_CX bool activateDrag(const PadToken& token) {
        if (!token_.valid || token_.owner != PadOwner::Drag || token_.generation != token.generation ||
            token_.session != token.session || token_.epoch != token.epoch) return false;
        token_.phase = PadPhase::Active;
        return true;
    }
    PAPERMONO_PAD_CX PadToken token() const { return token_; }
    PAPERMONO_PAD_CX uint32_t epoch() const { return epoch_; }
    PAPERMONO_PAD_CX void endFrame() { if (snapshot_.ended) token_.valid = false; }
    PAPERMONO_PAD_CX void cancel() { token_.valid = false; ++epoch_; }

private:
    PadSnapshot snapshot_{};
    PadProposal proposals_[3]{};
    PadToken token_{};
    uint32_t observed_ = 0;
    uint32_t generation_ = 0;
    uint32_t epoch_ = 0;
};

class PadTapDrag {
public:
    PAPERMONO_PAD_CX PadProposal observe(const PadSnapshot& s, const PadToken& prior,
                                       const PadConfig& c) {
        PadProposal p{};
        if (s.began) {
            start_ = previous_ = s.frame;
            edgeX_ = edgeY_ = PadEdge{};
            moved_ = intent_ = false;
            maxCount_ = s.frame.count;
            firstLift_ = 0;
            partialLift_ = false;
            second_ = history_ && s.frame.count == 1 && s.frame.ms - clickMs_ <= c.doubleTapMs &&
                padAbs(centerX(s.frame) - clickX_) <= 36 && padAbs(centerY(s.frame) - clickY_) <= 36;
            if (history_ && !second_) p.action.kind = PadActionKind::CommitClick;
            history_ = false;
        }
        if (s.cancelled) { cancel(); return p; }
        if (s.frame.count) {
            if (s.frame.count > maxCount_) {
                maxCount_ = s.frame.count;
                for (int i = 0; i < s.frame.count; ++i)
                    if (s.frame.points[i].id != start_.points[0].id) start_.points[1] = s.frame.points[i];
                start_.count = s.frame.count;
            }
            if (maxCount_ == 2 && s.frame.count == 1 && !partialLift_) {
                partialLift_ = true; partialLiftMs_ = s.frame.ms;
            }
            if (s.frame.count == 2 && partialLift_) {
                if (s.frame.ms - partialLiftMs_ >= c.releaseDebounceMs && !firstLift_) firstLift_ = partialLiftMs_;
                partialLift_ = false;
            }
            for (int i = 0; i < s.frame.count; ++i) {
                const int k = pointIndex(start_, s.frame.points[i].id);
                if (k < 0 || padMax(padAbs(s.frame.points[i].x - start_.points[k].x),
                                     padAbs(s.frame.points[i].y - start_.points[k].y)) >= c.tapSlop) moved_ = true;
            }
        }
        if (partialLift_ && s.frame.ms - partialLiftMs_ >= c.releaseDebounceMs && !firstLift_)
            firstLift_ = partialLiftMs_;
        const bool active = prior.valid && prior.owner == PadOwner::Drag && prior.phase == PadPhase::Active;
        if (active) {
            p.action.kind = s.ended || s.frame.count != 1 ? PadActionKind::DragEnd : PadActionKind::Motion;
            if (p.action.kind == PadActionKind::Motion) {
                p.action.x = centerX(s.frame) - centerX(previous_);
                p.action.y = centerY(s.frame) - centerY(previous_);
                if (padMax(padAbs(p.action.x),padAbs(p.action.y)) > c.maxFrameDelta) {
                    p.action.x = p.action.y = 0;
                    edgeX_ = edgeY_ = PadEdge{};
                }
                p.action.x += edgeX_.update(centerX(s.frame),c.width,0,s.frame.ms,
                                           c.edgeDragRepeatMs,c.edgeDragPixels,c,true);
                p.action.y += edgeY_.update(centerY(s.frame),c.height,0,s.frame.ms,
                                           c.edgeDragRepeatMs,c.edgeDragPixels,c,true,c.edgeTopMargin);
            }
            if (p.action.kind == PadActionKind::DragEnd) edgeX_ = edgeY_ = PadEdge{};
        } else if (second_ && s.frame.count == 2) {
            second_ = false;
            p.withdraw = true;
        } else if (second_ && !s.ended) {
            p.claim = true; p.owner = PadOwner::Drag; p.capabilities = PadMotion | PadButtons;
            p.action.kind = PadActionKind::CancelClick;
            const bool dragMoved = padMax(padAbs(centerX(s.frame) - centerX(start_)),
                                         padAbs(centerY(s.frame) - centerY(start_))) >= c.dragStart;
            const bool dragHeld = s.frame.count == 1 && s.frame.ms - start_.ms >= c.dragHoldMs;
            intent_ |= dragMoved || dragHeld;
            if (dragMoved || dragHeld) {
                p.action.kind = PadActionKind::DragBegin;
                if (dragMoved) {
                    p.action.x = centerX(s.frame) - centerX(start_);
                    p.action.y = centerY(s.frame) - centerY(start_);
                }
            }
        } else if (s.ended && !moved_ && (!second_ || !intent_)) {
            p.action.kind = PadActionKind::Click;
            const uint32_t end = firstLift_ ? firstLift_ : s.endMs;
            p.action.button = second_ ? 1 : maxCount_ == 2 ? (end - start_.ms >= c.holdMs ? 4 : 2) : 1;
            // A standalone one-finger hold has no terminal click action.
            if (!second_ && maxCount_ == 1 && s.endMs - start_.ms >= c.holdMs)
                p.action.kind = PadActionKind::None;
            if (p.action.kind == PadActionKind::Click && p.action.button == 1)
                p.action.kind = second_ ? PadActionKind::DoubleClick : PadActionKind::DeferredClick;
        }
        if (s.frame.count) previous_ = s.frame;
        return p;
    }
    PAPERMONO_PAD_CX void clicked(const PadSnapshot& s, uint8_t button) {
        if (button == 1 && !second_) {
            history_ = true; clickMs_ = s.frame.ms;
            clickX_ = centerX(start_); clickY_ = centerY(start_);
        }
    }
    PAPERMONO_PAD_CX int edgeYDirection() const { return edgeY_.direction; }
    PAPERMONO_PAD_CX void cancel() {
        history_ = second_ = moved_ = intent_ = false;
        edgeX_ = edgeY_ = PadEdge{};
    }
private:
    PadEdge edgeX_{}, edgeY_{};
    PadFrame start_{}, previous_{};
    uint32_t clickMs_ = 0, firstLift_ = 0, partialLiftMs_ = 0;
    int clickX_ = 0, clickY_ = 0;
    uint8_t maxCount_ = 0;
    bool history_ = false, second_ = false, moved_ = false, intent_ = false;
    bool partialLift_ = false;
};

enum class PadScrollReason : uint8_t {
    End, Pointer, Partial, Topology, Jump, OtherOwner, Threshold, Direction, Centroid, Accumulating, Wheel
};

struct PadScrollTrace {
    PadScrollReason reason = PadScrollReason::End;
    int moveX[2]{}, moveY[2]{};
    int64_t dot = 0;
    int axis = 0;
    int wheel = 0, pan = 0;
};

struct PadTrace {
    uint32_t sequence = 0, session = 0;
    bool consumed = false, cancelled = false, ended = false;
    bool began = false, tapSubmitted = false;
    PadActionKind tapAction = PadActionKind::None;
    bool wheelAttempted = false, wheelSubmitted = false;
    PadScrollTrace scroll{};
    PadToken owner{};
    int edgeYDirection = 0;
};

class PadRouter {
public:
    PAPERMONO_PAD_CX PadProposal observe(const PadSnapshot& s, const PadToken& prior,
                                       const PadConfig& c) {
        PadProposal p{}; p.recognizer = PadRecognizer::Router;
        if (s.began) { cancel(); previous_ = origin_ = s.frame; }
        trace_ = PadScrollTrace{}; trace_.axis = axis_;
        if (s.frame.count == 2 && origin_.count == 2) {
            for (int i = 0; i < 2; ++i) {
                const int k = pointIndex(origin_,s.frame.points[i].id);
                if (k >= 0) {
                    trace_.moveX[i] = s.frame.points[i].x - origin_.points[k].x;
                    trace_.moveY[i] = s.frame.points[i].y - origin_.points[k].y;
                }
            }
            trace_.dot = static_cast<int64_t>(trace_.moveX[0]) * trace_.moveX[1] +
                         static_cast<int64_t>(trace_.moveY[0]) * trace_.moveY[1];
        }
        if (s.ended || s.cancelled) { cancel(); return p; }
        two_ |= s.frame.count == 2;
        // Do not switch to pointer or reset a pair's origin on a brief partial lift.
        if (two_ && s.frame.count == 1) {
            edge_ = PadEdge{};
            trace_.reason = PadScrollReason::Partial;
            if (!missing_) { missing_ = true; missingMs_ = s.frame.ms; }
            return p;
        }
        bool rebase = previous_.count != s.frame.count;
        if (missing_) {
            rebase |= s.frame.ms - missingMs_ > c.twoFingerGraceMs;
            missing_ = false;
        }
        for (int i = 0; i < s.frame.count; ++i)
            rebase |= pointIndex(previous_, s.frame.points[i].id) < 0;
        int dx = centerX(s.frame) - centerX(previous_);
        int dy = centerY(s.frame) - centerY(previous_);
        bool jump = false;
        for (int i = 0; i < s.frame.count && !rebase; ++i) {
            const auto& old = previous_.points[pointIndex(previous_, s.frame.points[i].id)];
            jump |= padMax(padAbs(s.frame.points[i].x - old.x),
                          padAbs(s.frame.points[i].y - old.y)) > c.maxFrameDelta;
        }
        rebase |= jump;
        if (rebase) {
            edge_ = PadEdge{};
            trace_.reason = jump ? PadScrollReason::Jump : PadScrollReason::Topology;
            origin_ = previous_ = s.frame; return p;
        }
        if (s.frame.count == 1 && !two_ && !prior.valid) {
            trace_.reason = PadScrollReason::Pointer;
            if (started_ || padMax(padAbs(centerX(s.frame) - centerX(origin_)),
                                  padAbs(centerY(s.frame) - centerY(origin_))) >= c.pointerStart) {
                p.action.kind = PadActionKind::Motion;
                p.action.x = started_ ? dx : centerX(s.frame) - centerX(origin_);
                p.action.y = started_ ? dy : centerY(s.frame) - centerY(origin_);
                started_ = true;
            }
        } else if (s.frame.count == 2 && (!prior.valid || prior.owner == PadOwner::Scroll)) {
            if (axis_ == 0) {
                const int x = padAbs(centerX(s.frame) - centerX(origin_));
                const int y = padAbs(centerY(s.frame) - centerY(origin_));
                bool coherent = origin_.count == 2;
                bool enough = true;
                for (int i = 0; i < 2; ++i) {
                    const int k = pointIndex(origin_, s.frame.points[i].id);
                    if (k < 0) { coherent = false; continue; }
                    trace_.moveX[i] = s.frame.points[i].x - origin_.points[k].x;
                    trace_.moveY[i] = s.frame.points[i].y - origin_.points[k].y;
                    enough &= padMax(padAbs(trace_.moveX[i]),padAbs(trace_.moveY[i])) >= c.scrollStart;
                }
                trace_.dot = static_cast<int64_t>(trace_.moveX[0]) * trace_.moveX[1] +
                             static_cast<int64_t>(trace_.moveY[0]) * trace_.moveY[1];
                coherent &= enough && trace_.dot > 0;
                trace_.reason = !enough ? PadScrollReason::Threshold : trace_.dot <= 0 ?
                    PadScrollReason::Direction : PadScrollReason::Centroid;
                if (coherent && padMax(x,y) >= c.scrollStart) axis_ = x > y ? 1 : 2;
                if (axis_) {
                    dx = centerX(s.frame) - centerX(origin_);
                    dy = centerY(s.frame) - centerY(origin_);
                }
            }
            if (axis_) {
                p.claim = true; p.owner = PadOwner::Scroll; p.capabilities = PadWheel;
                int delta = axis_ == 1 ? dx : dy;
                const int a = axis_ == 1 ? s.frame.points[0].x : s.frame.points[0].y;
                const int b = axis_ == 1 ? s.frame.points[1].x : s.frame.points[1].y;
                const int extent = axis_ == 1 ? c.width : c.height;
                const int lowMargin = axis_ == 2 ? c.edgeTopMargin : c.edgeMargin;
                const bool low = c.edgeMargin > 0 && (a < lowMargin || b < lowMargin);
                const bool high = c.edgeMargin > 0 &&
                                  (a >= extent - c.edgeMargin || b >= extent - c.edgeMargin);
                if (!edge_.direction && delta) scrollDirection_ = delta > 0 ? 1 : -1;
                // Contact eligibility is independent of centroid motion. If the
                // pair spans both edges, retain the established scroll direction.
                const int direction = low && high ? (edge_.direction ? edge_.direction : scrollDirection_) :
                                      low ? -1 : high ? 1 : 0;
                const int edgePosition = direction < 0 ? (a < b ? a : b) :
                                         direction > 0 ? padMax(a,b) : extent / 2;
                delta += edge_.update(edgePosition,extent,0,s.frame.ms,
                                      c.edgeScrollRepeatMs,c.scrollStep,c,true,lowMargin);
                remainder_ += delta;
                const int steps = remainder_ / c.scrollStep;
                remainder_ -= steps * c.scrollStep;
                p.action.kind = PadActionKind::Scroll;
                p.action.x = axis_ == 1 ? steps : 0;
                p.action.y = axis_ == 2 ? steps : 0;
                trace_.axis = axis_;
                trace_.pan = p.action.x; trace_.wheel = p.action.y;
                trace_.reason = steps ? PadScrollReason::Wheel : PadScrollReason::Accumulating;
            }
        } else {
            trace_.reason = PadScrollReason::OtherOwner;
        }
        previous_ = s.frame;
        return p;
    }
    PAPERMONO_PAD_CX void cancel() {
        two_ = started_ = missing_ = false; axis_ = remainder_ = 0;
        scrollDirection_ = 1;
        edge_ = PadEdge{};
    }
    PAPERMONO_PAD_CX PadScrollTrace trace() const { return trace_; }
    PAPERMONO_PAD_CX int edgeYDirection() const { return axis_ == 2 ? edge_.direction : 0; }
private:
    PadEdge edge_{};
    PadFrame previous_{}, origin_{};
    bool two_ = false, started_ = false, missing_ = false;
    uint32_t missingMs_ = 0;
    int axis_ = 0, remainder_ = 0;
    int scrollDirection_ = 1;
    PadScrollTrace trace_{};
};

class PadTipTap {
public:
    PAPERMONO_PAD_CX PadProposal observe(const PadSnapshot& s, const PadToken& prior,
                                       const PadConfig& c) {
        PadProposal p{}; p.recognizer = PadRecognizer::TipTap;
        if (s.began) {
            cancel();
            if (s.frame.count == 1) { anchor_ = s.frame.points[0]; anchorMs_ = s.frame.ms; }
        }
        if (s.ended || s.cancelled) { cancel(); return p; }
        if (prior.valid && prior.owner != PadOwner::TipTap) return p;
        const int anchorIndex = pointIndex(s.frame, anchor_.id);
        if (anchorIndex < 0 || padMax(padAbs(s.frame.points[anchorIndex].x - anchor_.x),
                                    padAbs(s.frame.points[anchorIndex].y - anchor_.y)) >= c.tapSlop) {
            candidate_ = false; anchor_.id = -1; return p;
        }
        if (s.frame.count == 2) {
            const PadPoint other = s.frame.points[1 - anchorIndex];
            const bool arrived = other.id != presentId_;
            presentId_ = other.id;
            if (arrived && s.frame.ms - anchorMs_ >= c.tipAnchorMs &&
                padAbs(other.x - anchor_.x) >= c.tipSeparation) {
                candidate_ = true; other_ = other; otherMs_ = s.frame.ms;
            }
            if (candidate_ && (other.id != other_.id ||
                padMax(padAbs(other.x - other_.x),padAbs(other.y - other_.y)) >= c.tapSlop)) candidate_ = false;
            liftMs_ = 0;
        } else if (candidate_) {
            if (!liftMs_) liftMs_ = s.frame.ms;
            if (s.frame.ms - liftMs_ >= c.releaseDebounceMs) {
                if (liftMs_ - otherMs_ <= c.tipTapMs) {
                    p.claim = true; p.owner = PadOwner::TipTap; p.capabilities = PadButtons;
                    p.action.kind = PadActionKind::Click; p.action.button = other_.x < anchor_.x ? 8 : 16;
                }
                candidate_ = false;
                presentId_ = -1;
            }
        } else {
            presentId_ = -1;
        }
        return p;
    }
    PAPERMONO_PAD_CX void cancel() { anchor_.id = presentId_ = -1; candidate_ = false; liftMs_ = 0; }
private:
    PadPoint anchor_{}, other_{};
    uint32_t anchorMs_ = 0, otherMs_ = 0, liftMs_ = 0;
    bool candidate_ = false;
    int32_t presentId_ = -1;
};

struct PadReport {
    uint8_t buttons = 0;
    int16_t dx = 0, dy = 0, wheel = 0, pan = 0;
};

// Time-based Q16 velocity, inspired by Travelers Pad scroll_inertia.
// This observer proposes wheel reports; only PadBroker may send HID.
class PadScrollInertia {
public:
    PAPERMONO_PAD_CX static int32_t decay(int32_t velocity, uint32_t ms, uint32_t halfLife) {
        if (!halfLife) return 0;
        const int64_t unit = int64_t{1} << 24;
        const int64_t time = static_cast<int64_t>(halfLife) * 2000;
        int64_t factor = (time - 693) * unit / (time + 693), result = unit;
        while (ms) {
            if (ms & 1) result = result * factor / unit;
            factor = factor * factor / unit;
            ms >>= 1;
        }
        return static_cast<int32_t>(static_cast<int64_t>(velocity) * result / unit);
    }
    PAPERMONO_PAD_CX void observe(const PadReport& report, uint32_t now, uint32_t interval,
                                 const PadConfig& c) {
        if (!c.inertiaHalfLifeMs || (!report.pan && !report.wheel)) return;
        const bool fresh = !haveWheel_ || now - wheelMs_ > c.inertiaReleaseTimeoutMs;
        const uint32_t elapsed = fresh ? padMax(interval, c.inertiaRepeatMs) : now - wheelMs_;
        const int delta = report.pan ? report.pan : report.wheel;
        const bool horizontal = report.pan != 0;
        const int32_t instant = static_cast<int32_t>(static_cast<int64_t>(delta) * 65536 /
                                                    (elapsed ? elapsed : 1));
        if (fresh || horizontal != horizontal_ || c.inertiaWindowMs == 0 || elapsed >= c.inertiaWindowMs ||
            (instant < 0) != (velocity_ < 0)) velocity_ = instant;
        else velocity_ += static_cast<int32_t>((static_cast<int64_t>(instant) - velocity_) * elapsed / c.inertiaWindowMs);
        horizontal_ = horizontal;
        haveWheel_ = true; wheelMs_ = now;
    }
    PAPERMONO_PAD_CX void start(uint32_t now, uint32_t released, const PadConfig& c) {
        if (!haveWheel_ || !c.inertiaHalfLifeMs || released - wheelMs_ > c.inertiaReleaseTimeoutMs) {
            cancel(); return;
        }
        velocity_ = decay(velocity_, now - wheelMs_, c.inertiaHalfLifeMs);
        active_ = true; releasedMs_ = released; tickMs_ = now;
        dueMs_ = now + c.inertiaStartDelayMs; remainder_ = 0;
    }
    PAPERMONO_PAD_CX bool next(uint32_t now, const PadConfig& c, PadReport& out) {
        if (!active_ || static_cast<int32_t>(now - dueMs_) < 0) return false;
        if (!c.inertiaRepeatMs || now - releasedMs_ >= c.inertiaMaxDurationMs) { cancel(); return false; }
        const int32_t old = velocity_;
        const uint32_t elapsed = now - tickMs_;
        velocity_ = decay(old, elapsed, c.inertiaHalfLifeMs);
        tickMs_ = now; dueMs_ = now + c.inertiaRepeatMs;
        if (static_cast<int64_t>(padAbs(velocity_)) * 1000 < static_cast<int64_t>(c.inertiaStopSpeed) * 65536) {
            cancel(); return false;
        }
        const int64_t travel = remainder_ + (static_cast<int64_t>(old) + velocity_) * elapsed / 2;
        const int delta = padClamp(static_cast<int>(travel / 65536), -c.inertiaMaxDelta, c.inertiaMaxDelta);
        remainder_ = travel % 65536;
        out = PadReport{};
        if (horizontal_) out.pan = delta; else out.wheel = delta;
        return delta != 0;
    }
    PAPERMONO_PAD_CX bool active() const { return active_; }
    PAPERMONO_PAD_CX void cancel() { active_ = haveWheel_ = false; velocity_ = 0; remainder_ = 0; }
private:
    int32_t velocity_ = 0;
    int64_t remainder_ = 0;
    uint32_t wheelMs_ = 0, releasedMs_ = 0, tickMs_ = 0, dueMs_ = 0;
    bool active_ = false, haveWheel_ = false, horizontal_ = false;
};

// Only this broker can call the transport. Recognizers produce proposals, not HID.
template<class Sink> class PadBroker {
public:
    PAPERMONO_PAD_CX PadBroker(Sink& sink, const PadConfig& config) : sink_(sink), c_(config) {}
    PAPERMONO_PAD_CX void bind(const PadResolution& r) {
        if (!r.complete || static_cast<int32_t>(r.epoch - minEpoch_) < 0 ||
            (bound_.complete && r.epoch == bound_.epoch &&
             static_cast<int32_t>(r.snapshot.frame.sequence - bound_.snapshot.frame.sequence) < 0)) return;
        if (r.snapshot.began || r.snapshot.cancelled || r.epoch != bound_.epoch) inertia_.cancel();
        if (r.snapshot.frame.count && contactMs_ != r.snapshot.frame.ms) {
            contactInterval_ = contactMs_ ? r.snapshot.frame.ms - contactMs_ : c_.inertiaRepeatMs;
            contactMs_ = r.snapshot.frame.ms;
        }
        if (r.snapshot.ended && !r.snapshot.cancelled && r.snapshot.frame.count == 0 &&
            r.token.valid && r.token.owner == PadOwner::Scroll && r.token.phase == PadPhase::Active &&
            (r.token.capabilities & PadWheel)) {
            inertiaLease_ = r.token;
            inertia_.start(r.snapshot.frame.ms, r.snapshot.endMs, c_);
        }
        if (r.snapshot.began || r.snapshot.cancelled ||
            r.token.generation != bound_.token.generation || r.epoch != bound_.epoch ||
            (r.snapshot.frame.count != 0 && r.snapshot.frame.count != bound_.snapshot.frame.count)) {
            pendingX_ = pendingY_ = scrollX_ = scrollY_ = 0;
        }
        if (r.snapshot.session != bound_.snapshot.session ||
            r.snapshot.frame.sequence != bound_.snapshot.frame.sequence || r.epoch != bound_.epoch)
            terminalUsed_ = false;
        bound_ = r;
    }
    PAPERMONO_PAD_CX bool submit(const PadProposal& p, const PadToken& token) {
        if (!bound_.complete || bound_.snapshot.cancelled ||
            p.frameSequence != bound_.snapshot.frame.sequence || p.session != bound_.snapshot.session ||
            p.epoch != bound_.epoch || token.epoch != bound_.token.epoch ||
            token.generation != bound_.token.generation || token.session != bound_.token.session ||
            token.valid != bound_.token.valid || token.phase != bound_.token.phase ||
            token.owner != bound_.token.owner || token.capabilities != bound_.token.capabilities) return false;
        const auto& s = bound_.snapshot;
        const bool tap = p.recognizer == PadRecognizer::TapDrag;
        const bool owns = token.valid && token.recognizer == p.recognizer && token.session == s.session;
        if (p.action.kind == PadActionKind::CancelClick) {
            if (!tap || !owns || token.owner != PadOwner::Drag || token.phase != PadPhase::Reserved)
                return false;
            if (deferredClick_) { queuedClicks_ = 0; deferredClick_ = false; }
            return true;
        }
        if (p.action.kind == PadActionKind::CommitClick) {
            if (!tap || !s.began || token.valid) return false;
            if (deferredClick_) {
                queuedAt_ = s.frame.ms;
                tick(s.frame.ms);
                // A different touch must not move with the old click held.
                if (pulsing_) release();
            }
            return true;
        }
        if (p.action.kind == PadActionKind::DragEnd) {
            if (!tap || !owns || !held_ || heldSession_ != s.session || heldGeneration_ != token.generation) return false;
            flush(s.frame.ms, true); return release();
        }
        if (p.action.kind == PadActionKind::Click || p.action.kind == PadActionKind::DeferredClick ||
            p.action.kind == PadActionKind::DoubleClick) {
            const bool terminal = tap && s.ended && !terminalUsed_ &&
                (!token.valid || (owns && token.owner == PadOwner::Drag && token.phase == PadPhase::Reserved));
            const bool tip = owns && token.owner == PadOwner::TipTap && (token.capabilities & PadButtons);
            if (!terminal && !tip) return false;
            if (p.action.kind != PadActionKind::Click && (!terminal || p.action.button != 1)) return false;
            if (terminal) terminalUsed_ = true;
            flush(s.frame.ms, true); if (!release()) return false;
            if (p.action.kind == PadActionKind::DeferredClick) {
                queuedClicks_ = 1; deferredClick_ = true;
                queuedAt_ = s.frame.ms + c_.doubleTapMs;
                return true;
            }
            queuedClicks_ = 0; deferredClick_ = false;
            PadReport report{}; report.buttons = p.action.button;
            if (!sink_.send(report)) return false;
            held_ = report.buttons; heldSession_ = s.session; heldGeneration_ = token.generation;
            if (p.action.kind == PadActionKind::DoubleClick) queuedClicks_ = 1;
            pulseAt_ = s.frame.ms + c_.pulseMs; pulsing_ = true; return true;
        }
        if (p.action.kind == PadActionKind::DragBegin) {
            if (!tap || !owns || token.owner != PadOwner::Drag || token.phase != PadPhase::Reserved ||
                !(token.capabilities & PadButtons) || s.frame.count != 1) return false;
            if (held_ && !pulsing_) return false;
            queuedClicks_ = 0; deferredClick_ = false;
            if (!release()) return false;
            PadReport report{}; report.buttons = 1;
            if (!sink_.send(report)) return false;
            held_ = 1; heldSession_ = s.session; heldGeneration_ = token.generation; return true;
        }
        if (p.action.kind == PadActionKind::Motion) {
            const bool pointer = !token.valid && p.recognizer == PadRecognizer::Router && s.frame.count == 1;
            const bool drag = tap && owns && token.owner == PadOwner::Drag && token.phase == PadPhase::Active &&
                (token.capabilities & PadMotion) && held_ == 1 && heldSession_ == s.session;
            if ((!pointer && !drag) || releasePending_) return false;
            const uint32_t dt = motionMs_ ? s.frame.ms - motionMs_ : c_.reportMs;
            const int speed = padMax(padAbs(p.action.x),padAbs(p.action.y)) * 1000 / (dt ? dt : 1);
            const int q = (padClamp(speed,c_.slowSpeed,c_.fastSpeed) - c_.slowSpeed) * 256 /
                          (c_.fastSpeed - c_.slowSpeed);
            const int smooth = q * q / 256 * (768 - 2 * q) / 256;
            const int gain = (c_.slowGainQ8 + smooth * (c_.fastGainQ8 - c_.slowGainQ8) / 256) * pointerPercent_ / 100;
            pendingX_ += p.action.x * gain;
            pendingY_ += p.action.y * gain;
            if (p.action.x || p.action.y) motionMs_ = s.frame.ms;
            pendingX_ = padClamp(pendingX_, -32767 * 256, 32767 * 256);
            pendingY_ = padClamp(pendingY_, -32767 * 256, 32767 * 256);
            return true;
        }
        if (p.action.kind == PadActionKind::Scroll && owns && token.owner == PadOwner::Scroll &&
            (token.capabilities & PadWheel) && s.frame.count == 2) {
            if (!p.action.x && !p.action.y) return false;
            PadReport report{}; report.buttons = held_;
            const int x = scrollX_ + p.action.x * scrollPercent_;
            const int y = scrollY_ + p.action.y * scrollPercent_;
            report.pan = padClamp((c_.invertScroll ? -1 : 1) * (x / 100), -32767, 32767);
            report.wheel = padClamp((c_.invertScroll ? 1 : -1) * (y / 100), -32767, 32767);
            if (report.pan || report.wheel) {
                if (!sink_.send(report)) return false;
                inertia_.observe(report, s.frame.ms, contactInterval_, c_);
            }
            scrollX_ = x % 100; scrollY_ = y % 100;
            return true;
        }
        return false;
    }
    PAPERMONO_PAD_CX void flush(uint32_t now, bool force = false) {
        if (releasePending_ || !bound_.complete || bound_.snapshot.cancelled ||
            (bound_.token.valid && (bound_.token.owner != PadOwner::Drag || bound_.token.phase != PadPhase::Active))) return;
        if (!force && now - sentMs_ < c_.reportMs) return;
        PadReport report{}; report.buttons = held_; report.dx = pendingX_ / 256; report.dy = pendingY_ / 256;
        if ((report.dx || report.dy) && sink_.send(report)) {
            pendingX_ -= report.dx * 256; pendingY_ -= report.dy * 256; sentMs_ = now;
        }
    }
    PAPERMONO_PAD_CX void tick(uint32_t now) {
        if (inertia_.active()) {
            const auto& token = bound_.token;
            const bool lease = bound_.complete && bound_.snapshot.ended && !bound_.snapshot.cancelled &&
                !bound_.snapshot.frame.count && token.valid && token.owner == PadOwner::Scroll &&
                token.session == inertiaLease_.session && token.generation == inertiaLease_.generation &&
                token.epoch == inertiaLease_.epoch && !held_ && !releasePending_;
            if (!lease) inertia_.cancel();
            else {
                PadReport report{};
                if (inertia_.next(now, c_, report) && !sink_.send(report)) inertia_.cancel();
            }
        }
        if (releasePending_) {
            if (!release()) return;
            if (queuedClicks_ && !deferredClick_) queuedAt_ = now + c_.pulseMs;
        }
        if (pulsing_ && static_cast<int32_t>(now - pulseAt_) >= 0) {
            if (!release()) return;
            if (queuedClicks_ && !deferredClick_) queuedAt_ = now + c_.pulseMs;
        }
        if (queuedClicks_ && !held_ && static_cast<int32_t>(now - queuedAt_) >= 0) {
            PadReport down{}; down.buttons = 1;
            if (!sink_.send(down)) return;
            --queuedClicks_; deferredClick_ = false;
            held_ = 1; pulsing_ = true; pulseAt_ = now + c_.pulseMs;
        }
    }
    PAPERMONO_PAD_CX void cancel() {
        inertia_.cancel(); inertiaLease_ = PadToken{}; contactMs_ = 0;
        pendingX_ = pendingY_ = scrollX_ = scrollY_ = 0; queuedClicks_ = 0; deferredClick_ = false; release();
        if (bound_.complete) minEpoch_ = bound_.epoch + 1;
        bound_ = PadResolution{};
    }
    PAPERMONO_PAD_CX void setSpeedPercent(int pointer, int scroll) {
        pointerPercent_ = padClamp(pointer, 25, 200);
        scrollPercent_ = padClamp(scroll, 25, 300);
        scrollX_ = scrollY_ = 0;
    }
    PAPERMONO_PAD_CX void observeContact(const PadFrame& frame) {
        // Cancel before processing a fresh contact, including release debounce.
        if (frame.reset || (frame.count && inertia_.active())) inertia_.cancel();
    }
private:
    PAPERMONO_PAD_CX bool release() {
        if (held_) {
            PadReport up{};
            if (!sink_.send(up)) { releasePending_ = true; return false; }
        }
        held_ = 0; pulsing_ = false;
        releasePending_ = false;
        return true;
    }
    Sink& sink_;
    PadConfig c_;
    PadScrollInertia inertia_{};
    PadToken inertiaLease_{};
    uint32_t contactMs_ = 0, contactInterval_ = 0;
    PadResolution bound_{};
    int pendingX_ = 0, pendingY_ = 0;
    int pointerPercent_ = 100, scrollPercent_ = 100;
    int scrollX_ = 0, scrollY_ = 0;
    uint32_t queuedAt_ = 0;
    uint8_t queuedClicks_ = 0;
    bool deferredClick_ = false;
    uint32_t sentMs_ = 0, motionMs_ = 0, pulseAt_ = 0, heldSession_ = 0, heldGeneration_ = 0;
    uint32_t minEpoch_ = 0;
    uint8_t held_ = 0;
    bool pulsing_ = false, terminalUsed_ = false;
    bool releasePending_ = false;
};

template<class Sink> class Touchpad {
public:
    PAPERMONO_PAD_CX Touchpad(Sink& sink, const PadConfig& config = PadConfig{}) : c_(config), broker_(sink,config) {}
    PAPERMONO_PAD_CX void setSpeedPercent(int pointer, int scroll) {
        cancel();
        broker_.setSpeedPercent(pointer, scroll);
    }
    PAPERMONO_PAD_CX void process(const PadFrame& frame, uint8_t observationOrder = 0) {
        trace_ = PadTrace{}; trace_.sequence = frame.sequence;
        if (!frame.sync || (haveClockSequence_ &&
            static_cast<int32_t>(frame.sequence - lastClockSequence_) <= 0)) return;
        lastClockSequence_ = frame.sequence; haveClockSequence_ = true;
        broker_.observeContact(frame);
        PadSnapshot s{};
        if (!session_.consume(frame,c_,s)) { broker_.tick(frame.ms); broker_.flush(frame.ms); return; }
        trace_.consumed = true; trace_.session = s.session;
        trace_.began = s.began;
        trace_.cancelled = s.cancelled; trace_.ended = s.ended;
        if (s.cancelled) { cancel(); return; }
        const auto prior = arbiter_.token();
        PadProposal proposals[3]{};
        // Observers only read the previous token, never each other's callbacks.
        const uint8_t orders[6][3] = {{0,1,2},{2,1,0},{0,2,1},{1,0,2},{1,2,0},{2,0,1}};
        for (int i = 0; i < 3; ++i) {
            const int index = orders[observationOrder % 6][i];
            if (index == 0) proposals[0] = tap_.observe(s,prior,c_);
            if (index == 1) proposals[1] = router_.observe(s,prior,c_);
            if (index == 2) proposals[2] = tip_.observe(s,prior,c_);
        }
        arbiter_.begin(s);
        for (auto& p : proposals) {
            p.frameSequence = s.frame.sequence; p.session = s.session; p.epoch = arbiter_.epoch();
            arbiter_.submit(p);
        }
        auto resolution = arbiter_.resolve();
        trace_.tapAction = proposals[0].action.kind;
        trace_.scroll = router_.trace(); trace_.owner = resolution.token;
        trace_.edgeYDirection = resolution.token.owner == PadOwner::Drag ?
            tap_.edgeYDirection() : router_.edgeYDirection();
        broker_.bind(resolution);
        for (const auto& p : proposals) {
            const bool submitted = broker_.submit(p,resolution.token);
            if (p.recognizer == PadRecognizer::TapDrag) trace_.tapSubmitted = submitted;
            if (p.recognizer == PadRecognizer::Router && p.action.kind == PadActionKind::Scroll &&
                (p.action.x || p.action.y)) {
                trace_.wheelAttempted = true; trace_.wheelSubmitted = submitted;
            }
            if (!submitted) continue;
            if (p.action.kind == PadActionKind::DragBegin) {
                arbiter_.activateDrag(resolution.token);
                resolution.token = arbiter_.token(); broker_.bind(resolution);
                PadProposal motion = p; motion.action.kind = PadActionKind::Motion;
                broker_.submit(motion,resolution.token);
            }
            if (p.recognizer == PadRecognizer::TapDrag &&
                (p.action.kind == PadActionKind::Click || p.action.kind == PadActionKind::DeferredClick))
                tap_.clicked(s,p.action.button);
        }
        broker_.tick(frame.ms);
        broker_.flush(frame.ms,s.ended);
        arbiter_.endFrame();
    }
    PAPERMONO_PAD_CX void cancel() {
        session_.cancel(); tap_.cancel(); router_.cancel(); tip_.cancel(); arbiter_.cancel(); broker_.cancel();
    }
    PAPERMONO_PAD_CX PadToken token() const { return arbiter_.token(); }
    PAPERMONO_PAD_CX PadTrace trace() const { return trace_; }
private:
    uint32_t lastClockSequence_ = 0;
    bool haveClockSequence_ = false;
    PadConfig c_;
    PadSession session_{};
    PadArbiter arbiter_{};
    PadTapDrag tap_{};
    PadRouter router_{};
    PadTipTap tip_{};
    PadBroker<Sink> broker_;
    PadTrace trace_{};
};

}  // namespace papermono

#undef PAPERMONO_PAD_CX
