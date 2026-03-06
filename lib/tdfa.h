#pragma once

#include "comptime.h"
#include "tnfa.h"
#include "ty.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace etch::TDFA {
using RegisterId = uint16_t;
using ClassId = uint16_t;

enum class RegOpKind : uint8_t { SetNil, SetCur, Copy, Append };

struct RegOp {
    RegOpKind kind{};
    RegisterId lhs{};
    RegisterId rhs{};
};

struct OpSlice {
    uint32_t off = 0;
    uint16_t len = 0;
};

struct Edge {
    StateTy to{};
    OpSlice ops{};
    bool enabled = false;
};

template <size_t MaxStates, size_t MaxCharRanges, size_t MaxRegs, size_t MaxOps, size_t MaxTags>
struct TDFAModel {
    constexpr static auto ty = etch::EtchTy::TDFAModel;
    constexpr static StateTy deadState = std::numeric_limits<StateTy>::max();
    std::array<std::array<Edge, MaxCharRanges>, MaxStates> delta{};
    std::array<std::array<StateTy, 256>, MaxStates> byteDelta{};
    std::array<OpSlice, MaxStates> finalOps{};
    std::array<bool, MaxStates> isFinal{};
    StateTy startState = 0;
    uint16_t stateIdx = 0;

    std::array<RegOp, MaxOps> opPool{};
    uint32_t opPoolIdx = 0;

    // The mapping from class id to inclusive character range [first, second].
    std::array<CharPair, MaxCharRanges> rangeToPair{};
    // 0 means "no class"; otherwise stores class_id + 1.
    std::array<uint16_t, 256> classByBytePlusOne{};
    uint16_t classCount = 0;

    uint16_t regCount = 0;
    uint16_t tagCount = 0;
    uint32_t minInputLen = 0;
    uint32_t maxInputLen = std::numeric_limits<uint32_t>::max();
    bool isExactLiteral = false;
    uint16_t literalLen = 0;
    std::array<char, MaxStates> literalBytes{};
    std::array<RegisterId, MaxTags + 1> finalRegOfTag{};
    etch::TDFAError error = etch::TDFAError::none;

    [[nodiscard]] constexpr auto errorMsg(etch::TDFAError err) const -> const char* {
        return etch::TDFAErrorToString(err);
    }
};

struct TDFACounting {
    uint16_t dfaStateN = 0;
    uint16_t regN = 0;
    uint16_t opN = 0;
    uint16_t stateRegN = 0;
};
}  // namespace etch::TDFA

namespace etch::TDFA::detail {
template <typename Model>
struct TNFATraits;

template <unsigned StateN,
          unsigned MaxCharStep,
          unsigned MaxConn,
          unsigned MaxSplitRange,
          unsigned MaxOwnSplit>
struct TNFATraits<TNFA::TNFAModel<StateN, MaxCharStep, MaxConn, MaxSplitRange, MaxOwnSplit>> {
    constexpr static std::size_t kStateN = StateN;
    constexpr static std::size_t kMaxCharStep = MaxCharStep;
    constexpr static std::size_t kMaxConn = MaxConn;
    constexpr static std::size_t kMaxSplitRange = MaxSplitRange == 0 ? 1 : MaxSplitRange;
    constexpr static std::size_t kMaxOwnSplit = MaxOwnSplit;
};

enum class HistValue : uint8_t { none = 0, cur = 1, nil = 2 };

constexpr auto isSameRegOp(const RegOp& lhs, const RegOp& rhs) -> bool {
    return lhs.kind == rhs.kind && lhs.lhs == rhs.lhs && lhs.rhs == rhs.rhs;
}
}  // namespace etch::TDFA::detail

namespace etch::TDFA {
template <typename TNFAModelTy,
          bool counting,
          std::size_t RegN = 0,
          std::size_t NewStateN = 0,
          std::size_t OpN = 0,
          std::size_t TagN = 0,
          bool EnableRegOptimize = true,
          bool EnableStateMinimize = true,
          bool EnableLengthPrune = true,
          bool EnableExactLiteralFastpath = true,
          auto DeltaRecord = eventide::comptime::counting_flag<1>>
class Builder {
    using TNFADataTy = std::remove_cvref_t<TNFAModelTy>;
    using Traits = detail::TNFATraits<TNFADataTy>;

    constexpr static std::size_t kCharRangeCap =
        Traits::kMaxSplitRange == 0 ? 1 : Traits::kMaxSplitRange;
    constexpr static std::size_t kStateCap = NewStateN == 0 ? 1 : NewStateN;
    constexpr static std::size_t kRegCap = RegN == 0 ? 1 : RegN;
    constexpr static std::size_t kOpCap = OpN == 0 ? 1 : OpN;
    constexpr static std::size_t kTagCap = TagN == 0 ? 1 : TagN;

    static_assert(counting || (RegN > 0 && NewStateN > 0 && OpN > 0 && TagN > 0),
                  "non-counting TDFA build requires non-zero capacities");

    struct RhsKey {
        RegOpKind kind = RegOpKind::SetNil;
        RegisterId rhs = 0;
    };

    struct RhsAlloc {
        RhsKey key{};
        RegisterId reg = 0;
    };

    struct Config3 {
        StateTy q = 0;
        std::vector<RegisterId> regs{};
        std::vector<detail::HistValue> lookahead{};
    };

    struct Config4 {
        StateTy q = 0;
        std::vector<RegisterId> regs{};
        std::vector<detail::HistValue> inherited{};
        std::vector<detail::HistValue> lookahead{};
    };

    struct DFAStateInner {
        std::vector<Config3> configs{};
        std::vector<StateTy> precedence{};
        bool isFinal = false;
        std::vector<RegOp> finalOps{};
    };

    struct TempEdge {
        StateTy to = 0;
        std::vector<RegOp> ops{};
        bool enabled = false;
    };

private:
    using ModelTy = TDFAModel<kStateCap, kCharRangeCap, kRegCap, kOpCap, kTagCap>;
    using BuildResult = std::conditional_t<counting, TDFACounting, std::optional<ModelTy>>;
    using DeltaResourceTy = eventide::comptime::ComptimeMemoryResource<DeltaRecord>;
    using DeltaPoolTy = eventide::comptime::ComptimeVector<TempEdge, DeltaResourceTy, 0>;

public:
    constexpr explicit Builder(const TNFAModelTy& tnfaModel) :
        tnfaModel_(tnfaModel), delta_(deltaResource_) {}

    constexpr auto build() -> BuildResult {
        reset();
        if(!prepare()) {
            return failedResult();
        }
        if(!determinize()) {
            return failedResult();
        }
        if(!optimize()) {
            return failedResult();
        }
        return finish();
    }

    [[nodiscard]] constexpr auto error() const -> TDFAError {
        return buildError_;
    }

    [[nodiscard]] constexpr auto ok() const -> bool {
        return buildError_ == TDFAError::none;
    }

    [[nodiscard]] consteval auto genDeltaRecord() const {
        static_assert(counting, "delta record is only available in counting mode");
        return deltaResource_.gen_record();
    }

private:
    constexpr auto setError(TDFAError err) -> bool {
        if(buildError_ == TDFAError::none) {
            buildError_ = err;
        }
        return false;
    }

    constexpr void reset() {
        buildError_ = TDFAError::none;
        classCount_ = 0;
        tagCount_ = 0;
        regCount_ = 0;
        peakRegCount_ = 0;
        peakStateCount_ = 0;
        startState_ = 0;
        states_.clear();
        delta_.clear();
        deltaPeak_ = 0;
        if constexpr(counting) {
            deltaResource_.set_reserved(0, 0);
        }
        finalRegOfTag_.clear();
        counting_ = {};
    }

    [[nodiscard]] constexpr auto failedResult() const -> BuildResult {
        if constexpr(counting) {
            return counting_;
        } else {
            return std::nullopt;
        }
    }

    [[nodiscard]] constexpr auto finish() -> BuildResult {
        counting_.dfaStateN = clampToU16(std::max(peakStateCount_, states_.size()));
        counting_.regN = clampToU16(std::max(peakRegCount_, regCount_));
        counting_.opN = clampToU16(countOperations());
        if constexpr(counting) {
            return counting_;
        } else {
            return materializeModel();
        }
    }

    [[nodiscard]] constexpr static auto clampToU16(std::size_t value) -> uint16_t {
        if(value > static_cast<std::size_t>(std::numeric_limits<uint16_t>::max())) {
            return std::numeric_limits<uint16_t>::max();
        }
        return static_cast<uint16_t>(value);
    }

    [[nodiscard]] constexpr auto prepare() -> bool {
        if(tnfaModel_.initialState >= Traits::kStateN || tnfaModel_.finalState >= Traits::kStateN) {
            return setError(TDFAError::invalid_tnfa);
        }

        classCount_ = tnfaModel_.transition.splitRangeIdx;
        if(classCount_ > kCharRangeCap) {
            return setError(TDFAError::class_capacity_exceeded);
        }

        tagCount_ = tnfaModel_.tagSize();
        if(!counting && tagCount_ > kTagCap) {
            return setError(TDFAError::tag_capacity_exceeded);
        }

        if(tagCount_ * 2 > static_cast<std::size_t>(std::numeric_limits<RegisterId>::max())) {
            return setError(TDFAError::register_capacity_exceeded);
        }

        finalRegOfTag_.assign(tagCount_ + 1, 0);
        for(std::size_t t = 1; t <= tagCount_; ++t) {
            finalRegOfTag_[t] = static_cast<RegisterId>(tagCount_ + t);
        }

        regCount_ = tagCount_ * 2;
        peakRegCount_ = std::max(peakRegCount_, regCount_);
        if(!counting && regCount_ > kRegCap) {
            return setError(TDFAError::register_capacity_exceeded);
        }

        std::vector<RegisterId> initialRegs(tagCount_ + 1, 0);
        for(std::size_t t = 1; t <= tagCount_; ++t) {
            initialRegs[t] = static_cast<RegisterId>(t);
        }
        const auto emptyMarks = makeEmptyMarks();

        std::vector<Config4> begin;
        begin.push_back(Config4{tnfaModel_.initialState, initialRegs, emptyMarks, emptyMarks});

        auto closure = epsilonClosure(begin);
        if(buildError_ != TDFAError::none) {
            return false;
        }
        auto precedence = buildPrecedence(closure);
        std::vector<RegOp> emptyOps;
        const auto s0 = addState(closure, precedence, emptyOps);
        if(buildError_ != TDFAError::none) {
            return false;
        }
        startState_ = s0;
        return true;
    }

    [[nodiscard]] constexpr auto determinize() -> bool {
        for(std::size_t s = 0; s < states_.size(); ++s) {
            std::vector<std::vector<RhsAlloc>> regCache(tagCount_ + 1);

            for(std::size_t classId = 0; classId < classCount_; ++classId) {
                auto moved = stepOnSymbol(states_[s], static_cast<ClassId>(classId));
                if(moved.empty()) {
                    continue;
                }

                auto closure = epsilonClosure(moved);
                if(buildError_ != TDFAError::none) {
                    return false;
                }
                if(closure.empty()) {
                    continue;
                }

                auto ops = transitionRegOps(closure, regCache);
                if(buildError_ != TDFAError::none) {
                    return false;
                }
                auto precedence = buildPrecedence(closure);

                const auto to = addState(closure, precedence, ops);
                if(buildError_ != TDFAError::none) {
                    return false;
                }

                auto& edge = edgeRef(s, classId);
                edge.to = to;
                edge.ops = std::move(ops);
                edge.enabled = true;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr auto deltaOffset(std::size_t state, std::size_t classId) const
        -> std::size_t {
        return state * classCount_ + classId;
    }

    [[nodiscard]] constexpr auto edgeRef(std::size_t state, std::size_t classId) -> TempEdge& {
        return delta_[deltaOffset(state, classId)];
    }

    [[nodiscard]] constexpr auto edgeRef(std::size_t state, std::size_t classId) const
        -> const TempEdge& {
        return delta_[deltaOffset(state, classId)];
    }

    constexpr void pushDeltaEdge(TempEdge edge = {}) {
        delta_.push_back(std::move(edge));
        if constexpr(counting) {
            if(delta_.size() > deltaPeak_) {
                deltaPeak_ = delta_.size();
            }
            deltaResource_.set_reserved(0, deltaPeak_);
        }
    }

    struct BlockRef {
        bool isFinal = false;
        std::size_t state = 0;
        std::size_t classId = 0;
    };

    struct LivenessData {
        std::vector<BlockRef> blocks{};
        std::vector<std::vector<unsigned char>> liveIn{};
        std::vector<std::vector<unsigned char>> liveOut{};
    };

    [[nodiscard]] constexpr auto optimize() -> bool {
        if constexpr(EnableRegOptimize) {
            if(!optimizeRegisters()) {
                return false;
            }
        }
        if constexpr(EnableStateMinimize) {
            if(!minimizeStates()) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr auto optimizeRegisters() -> bool {
        compactRegisters();
        constexpr std::size_t kRounds = 2;
        for(std::size_t round = 0; round < kRounds; ++round) {
            if(!deadStoreElimination()) {
                return false;
            }
            bool changed = false;
            if(!coalesceRegisters(changed)) {
                return false;
            }
            if(!changed) {
                break;
            }
        }
        if(!deadStoreElimination()) {
            return false;
        }
        compactRegisters();
        return true;
    }

    [[nodiscard]] constexpr auto getOps(const BlockRef& ref) -> std::vector<RegOp>& {
        if(ref.isFinal) {
            return states_[ref.state].finalOps;
        }
        return edgeRef(ref.state, ref.classId).ops;
    }

    [[nodiscard]] constexpr auto getOpsConst(const BlockRef& ref) const
        -> const std::vector<RegOp>& {
        if(ref.isFinal) {
            return states_[ref.state].finalOps;
        }
        return edgeRef(ref.state, ref.classId).ops;
    }

    constexpr void computeUseDef(const std::vector<RegOp>& ops,
                                 std::vector<unsigned char>& use,
                                 std::vector<unsigned char>& def) const {
        std::vector<unsigned char> defined(regCount_ + 1, 0);
        for(const auto& op: ops) {
            const auto rhs = readRegister(op);
            if(rhs != 0 && static_cast<std::size_t>(rhs) < defined.size() && defined[rhs] == 0) {
                use[rhs] = 1;
            }
            if(op.lhs != 0 && static_cast<std::size_t>(op.lhs) < defined.size()) {
                defined[op.lhs] = 1;
                def[op.lhs] = 1;
            }
        }
    }

    constexpr void unionSet(std::vector<unsigned char>& dst,
                            const std::vector<unsigned char>& src) const {
        const auto n = std::min(dst.size(), src.size());
        for(std::size_t i = 0; i < n; ++i) {
            dst[i] = static_cast<unsigned char>(dst[i] | src[i]);
        }
    }

    [[nodiscard]] constexpr auto setEqual(const std::vector<unsigned char>& lhs,
                                          const std::vector<unsigned char>& rhs) const -> bool {
        if(lhs.size() != rhs.size()) {
            return false;
        }
        for(std::size_t i = 0; i < lhs.size(); ++i) {
            if(lhs[i] != rhs[i]) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr auto computeLiveness(LivenessData& out) const -> bool {
        out = {};
        if(states_.empty()) {
            return true;
        }

        constexpr auto kInvalidIdx = std::numeric_limits<std::size_t>::max();

        std::vector<std::size_t> edgeBlockIdx(states_.size() * classCount_, kInvalidIdx);
        std::vector<std::size_t> finalBlockIdx(states_.size(), kInvalidIdx);
        const auto edgeBlockOffset = [&](std::size_t state, std::size_t classId) -> std::size_t {
            return state * classCount_ + classId;
        };

        for(std::size_t s = 0; s < states_.size(); ++s) {
            for(std::size_t c = 0; c < classCount_; ++c) {
                if(!edgeRef(s, c).enabled) {
                    continue;
                }
                edgeBlockIdx[edgeBlockOffset(s, c)] = out.blocks.size();
                out.blocks.push_back(BlockRef{false, s, c});
            }
            if(states_[s].isFinal) {
                finalBlockIdx[s] = out.blocks.size();
                out.blocks.push_back(BlockRef{true, s, 0});
            }
        }

        if(out.blocks.empty()) {
            return true;
        }

        std::vector<std::vector<std::size_t>> succ(out.blocks.size());
        std::vector<std::vector<unsigned char>> use(out.blocks.size(),
                                                    std::vector<unsigned char>(regCount_ + 1, 0));
        std::vector<std::vector<unsigned char>> def(out.blocks.size(),
                                                    std::vector<unsigned char>(regCount_ + 1, 0));
        std::vector<std::vector<unsigned char>> exitLive(
            out.blocks.size(),
            std::vector<unsigned char>(regCount_ + 1, 0));

        for(std::size_t b = 0; b < out.blocks.size(); ++b) {
            const auto& br = out.blocks[b];
            const auto& ops = getOpsConst(br);
            computeUseDef(ops, use[b], def[b]);

            if(br.isFinal) {
                for(std::size_t t = 1; t <= tagCount_; ++t) {
                    const auto fr = finalRegOfTag_[t];
                    if(fr != 0 && static_cast<std::size_t>(fr) < exitLive[b].size()) {
                        exitLive[b][fr] = 1;
                    }
                }
                continue;
            }

            const auto to = edgeRef(br.state, br.classId).to;
            if(to >= states_.size()) {
                return false;
            }
            for(std::size_t c = 0; c < classCount_; ++c) {
                const auto nb = edgeBlockIdx[edgeBlockOffset(to, c)];
                if(nb != kInvalidIdx) {
                    succ[b].push_back(nb);
                }
            }
            const auto fb = finalBlockIdx[to];
            if(fb != kInvalidIdx) {
                succ[b].push_back(fb);
            }
        }

        out.liveIn.assign(out.blocks.size(), std::vector<unsigned char>(regCount_ + 1, 0));
        out.liveOut.assign(out.blocks.size(), std::vector<unsigned char>(regCount_ + 1, 0));

        bool changed = true;
        while(changed) {
            changed = false;
            for(std::size_t it = out.blocks.size(); it > 0; --it) {
                const auto b = it - 1;
                auto blockOut = exitLive[b];
                for(const auto nb: succ[b]) {
                    unionSet(blockOut, out.liveIn[nb]);
                }

                auto blockIn = use[b];
                for(std::size_t r = 1; r <= regCount_; ++r) {
                    if(blockOut[r] != 0 && def[b][r] == 0) {
                        blockIn[r] = 1;
                    }
                }

                if(!setEqual(blockOut, out.liveOut[b]) || !setEqual(blockIn, out.liveIn[b])) {
                    out.liveOut[b] = std::move(blockOut);
                    out.liveIn[b] = std::move(blockIn);
                    changed = true;
                }
            }
        }

        return true;
    }

    [[nodiscard]] constexpr auto deadStoreElimination() -> bool {
        LivenessData liveness{};
        if(!computeLiveness(liveness)) {
            return setError(TDFAError::invalid_tnfa);
        }
        if(liveness.blocks.empty()) {
            return true;
        }

        for(std::size_t b = 0; b < liveness.blocks.size(); ++b) {
            auto& ops = getOps(liveness.blocks[b]);
            auto live = liveness.liveOut[b];
            std::vector<RegOp> keptRev;
            keptRev.reserve(ops.size());

            for(std::size_t i = ops.size(); i > 0; --i) {
                const auto& op = ops[i - 1];
                const auto lhs = static_cast<std::size_t>(op.lhs);
                const auto rhs = static_cast<std::size_t>(readRegister(op));

                bool keep = false;
                if(lhs < live.size() && live[lhs] != 0) {
                    keep = true;
                    live[lhs] = 0;
                    if(rhs > 0 && rhs < live.size()) {
                        live[rhs] = 1;
                    }
                }
                if(keep) {
                    keptRev.push_back(op);
                }
            }

            std::reverse(keptRev.begin(), keptRev.end());
            ops = std::move(keptRev);
            if(!topologicalSortOps(ops)) {
                return setError(TDFAError::invalid_tnfa);
            }
        }

        return true;
    }

    [[nodiscard]] constexpr auto coalesceRegisters(bool& changed) -> bool {
        changed = false;
        if(regCount_ < 2) {
            return true;
        }

        LivenessData liveness{};
        if(!computeLiveness(liveness)) {
            return setError(TDFAError::invalid_tnfa);
        }
        if(liveness.blocks.empty()) {
            return true;
        }

        std::vector<std::vector<unsigned char>> interfere(
            regCount_ + 1,
            std::vector<unsigned char>(regCount_ + 1, 0));
        for(std::size_t r = 1; r <= regCount_; ++r) {
            interfere[r][r] = 1;
        }

        // Keep final output registers distinct and pinned.
        std::vector<unsigned char> pinned(regCount_ + 1, 0);
        for(std::size_t t = 1; t <= tagCount_; ++t) {
            const auto fr = finalRegOfTag_[t];
            if(fr == 0 || static_cast<std::size_t>(fr) > regCount_) {
                continue;
            }
            pinned[fr] = 1;
            for(std::size_t r = 1; r <= regCount_; ++r) {
                interfere[fr][r] = 1;
                interfere[r][fr] = 1;
            }
        }

        for(std::size_t b = 0; b < liveness.blocks.size(); ++b) {
            auto live = liveness.liveOut[b];
            const auto& ops = getOpsConst(liveness.blocks[b]);
            for(std::size_t i = ops.size(); i > 0; --i) {
                const auto& op = ops[i - 1];
                const auto lhs = static_cast<std::size_t>(op.lhs);
                const auto rhs = static_cast<std::size_t>(readRegister(op));

                if(lhs != 0 && lhs < live.size()) {
                    for(std::size_t r = 1; r < live.size(); ++r) {
                        if(live[r] == 0 || r == lhs) {
                            continue;
                        }
                        if(op.kind == RegOpKind::Copy && rhs == r) {
                            continue;
                        }
                        interfere[lhs][r] = 1;
                        interfere[r][lhs] = 1;
                    }
                    live[lhs] = 0;
                }

                if(rhs != 0 && rhs < live.size()) {
                    live[rhs] = 1;
                }
                if(op.kind == RegOpKind::Append && lhs != 0 && lhs < live.size()) {
                    live[lhs] = 1;
                }
            }
        }

        std::vector<RegisterId> parent(regCount_ + 1, 0);
        std::vector<uint16_t> rank(regCount_ + 1, 0);
        for(std::size_t r = 1; r <= regCount_; ++r) {
            parent[r] = static_cast<RegisterId>(r);
        }

        const auto findRoot = [&](RegisterId x) {
            auto root = x;
            while(parent[root] != root) {
                root = parent[root];
            }
            while(parent[x] != x) {
                const auto next = parent[x];
                parent[x] = root;
                x = next;
            }
            return root;
        };

        const auto canMerge = [&](RegisterId a, RegisterId b) {
            if(a == b) {
                return true;
            }
            for(std::size_t i = 1; i <= regCount_; ++i) {
                if(findRoot(static_cast<RegisterId>(i)) != a) {
                    continue;
                }
                for(std::size_t j = 1; j <= regCount_; ++j) {
                    if(findRoot(static_cast<RegisterId>(j)) != b) {
                        continue;
                    }
                    if(interfere[i][j] != 0) {
                        return false;
                    }
                }
            }
            return true;
        };

        const auto unite = [&](RegisterId a, RegisterId b) {
            if(rank[a] < rank[b]) {
                parent[a] = b;
                return;
            }
            if(rank[a] > rank[b]) {
                parent[b] = a;
                return;
            }
            parent[b] = a;
            rank[a] = static_cast<uint16_t>(rank[a] + 1);
        };

        std::vector<std::pair<RegisterId, RegisterId>> copyPairs;
        for(const auto& block: liveness.blocks) {
            const auto& ops = getOpsConst(block);
            for(const auto& op: ops) {
                if(op.kind != RegOpKind::Copy || op.lhs == 0 || op.rhs == 0 || op.lhs == op.rhs) {
                    continue;
                }
                if(static_cast<std::size_t>(op.lhs) > regCount_ ||
                   static_cast<std::size_t>(op.rhs) > regCount_) {
                    continue;
                }
                copyPairs.push_back({op.lhs, op.rhs});
            }
        }

        for(const auto [lhs, rhs]: copyPairs) {
            if(pinned[lhs] != 0 || pinned[rhs] != 0) {
                continue;
            }
            const auto ra = findRoot(lhs);
            const auto rb = findRoot(rhs);
            if(ra == rb || !canMerge(ra, rb)) {
                continue;
            }
            unite(ra, rb);
        }

        for(std::size_t i = 1; i <= regCount_; ++i) {
            if(pinned[i] != 0) {
                continue;
            }
            for(std::size_t j = i + 1; j <= regCount_; ++j) {
                if(pinned[j] != 0) {
                    continue;
                }
                const auto ri = findRoot(static_cast<RegisterId>(i));
                const auto rj = findRoot(static_cast<RegisterId>(j));
                if(ri == rj || !canMerge(ri, rj)) {
                    continue;
                }
                unite(ri, rj);
            }
        }

        std::vector<RegisterId> remap(regCount_ + 1, 0);
        for(std::size_t r = 1; r <= regCount_; ++r) {
            const auto root = findRoot(static_cast<RegisterId>(r));
            remap[r] = root;
            if(root != static_cast<RegisterId>(r)) {
                changed = true;
            }
        }
        if(!changed) {
            return true;
        }

        for(std::size_t s = 0; s < states_.size(); ++s) {
            for(std::size_t c = 0; c < classCount_; ++c) {
                auto& edge = edgeRef(s, c);
                if(!edge.enabled) {
                    continue;
                }
                for(auto& op: edge.ops) {
                    if(op.lhs != 0 && static_cast<std::size_t>(op.lhs) < remap.size()) {
                        op.lhs = remap[op.lhs];
                    }
                    if((op.kind == RegOpKind::Copy || op.kind == RegOpKind::Append) &&
                       op.rhs != 0 && static_cast<std::size_t>(op.rhs) < remap.size()) {
                        op.rhs = remap[op.rhs];
                    }
                }
                if(!topologicalSortOps(edge.ops)) {
                    return setError(TDFAError::invalid_tnfa);
                }
            }
        }

        for(auto& st: states_) {
            for(auto& cfg: st.configs) {
                for(auto& reg: cfg.regs) {
                    if(reg != 0 && static_cast<std::size_t>(reg) < remap.size()) {
                        reg = remap[reg];
                    }
                }
            }
            for(auto& op: st.finalOps) {
                if(op.lhs != 0 && static_cast<std::size_t>(op.lhs) < remap.size()) {
                    op.lhs = remap[op.lhs];
                }
                if((op.kind == RegOpKind::Copy || op.kind == RegOpKind::Append) && op.rhs != 0 &&
                   static_cast<std::size_t>(op.rhs) < remap.size()) {
                    op.rhs = remap[op.rhs];
                }
            }
            if(!topologicalSortOps(st.finalOps)) {
                return setError(TDFAError::invalid_tnfa);
            }
        }

        for(std::size_t t = 1; t <= tagCount_; ++t) {
            const auto fr = finalRegOfTag_[t];
            if(fr != 0 && static_cast<std::size_t>(fr) < remap.size()) {
                finalRegOfTag_[t] = remap[fr];
            }
        }

        compactRegisters();
        return true;
    }

    constexpr void compactRegisters() {
        if(regCount_ == 0) {
            return;
        }

        std::vector<unsigned char> used(regCount_ + 1, 0);
        for(std::size_t s = 0; s < states_.size(); ++s) {
            for(std::size_t c = 0; c < classCount_; ++c) {
                const auto& edge = edgeRef(s, c);
                if(!edge.enabled) {
                    continue;
                }
                for(const auto& op: edge.ops) {
                    if(op.lhs != 0 && static_cast<std::size_t>(op.lhs) < used.size()) {
                        used[op.lhs] = 1;
                    }
                    const auto rhs = readRegister(op);
                    if(rhs != 0 && static_cast<std::size_t>(rhs) < used.size()) {
                        used[rhs] = 1;
                    }
                }
            }
        }

        for(const auto& st: states_) {
            for(const auto& op: st.finalOps) {
                if(op.lhs != 0 && static_cast<std::size_t>(op.lhs) < used.size()) {
                    used[op.lhs] = 1;
                }
                const auto rhs = readRegister(op);
                if(rhs != 0 && static_cast<std::size_t>(rhs) < used.size()) {
                    used[rhs] = 1;
                }
            }
        }

        for(std::size_t t = 1; t <= tagCount_; ++t) {
            const auto fr = finalRegOfTag_[t];
            if(fr != 0 && static_cast<std::size_t>(fr) < used.size()) {
                used[fr] = 1;
            }
        }

        std::vector<RegisterId> remap(regCount_ + 1, 0);
        std::size_t newCount = 0;
        for(std::size_t r = 1; r <= regCount_; ++r) {
            if(used[r] == 0) {
                continue;
            }
            newCount += 1;
            remap[r] = static_cast<RegisterId>(newCount);
        }

        for(std::size_t s = 0; s < states_.size(); ++s) {
            for(std::size_t c = 0; c < classCount_; ++c) {
                auto& edge = edgeRef(s, c);
                if(!edge.enabled) {
                    continue;
                }
                for(auto& op: edge.ops) {
                    if(op.lhs != 0 && static_cast<std::size_t>(op.lhs) < remap.size()) {
                        op.lhs = remap[op.lhs];
                    }
                    if((op.kind == RegOpKind::Copy || op.kind == RegOpKind::Append) &&
                       op.rhs != 0 && static_cast<std::size_t>(op.rhs) < remap.size()) {
                        op.rhs = remap[op.rhs];
                    }
                }
            }
        }

        for(auto& st: states_) {
            for(auto& cfg: st.configs) {
                for(auto& reg: cfg.regs) {
                    if(reg != 0 && static_cast<std::size_t>(reg) < remap.size()) {
                        reg = remap[reg];
                    }
                }
            }
            for(auto& op: st.finalOps) {
                if(op.lhs != 0 && static_cast<std::size_t>(op.lhs) < remap.size()) {
                    op.lhs = remap[op.lhs];
                }
                if((op.kind == RegOpKind::Copy || op.kind == RegOpKind::Append) && op.rhs != 0 &&
                   static_cast<std::size_t>(op.rhs) < remap.size()) {
                    op.rhs = remap[op.rhs];
                }
            }
        }

        for(std::size_t t = 1; t <= tagCount_; ++t) {
            const auto fr = finalRegOfTag_[t];
            if(fr != 0 && static_cast<std::size_t>(fr) < remap.size()) {
                finalRegOfTag_[t] = remap[fr];
            }
        }

        regCount_ = newCount;
    }

    [[nodiscard]] constexpr auto equalOps(const std::vector<RegOp>& lhs,
                                          const std::vector<RegOp>& rhs) const -> bool {
        if(lhs.size() != rhs.size()) {
            return false;
        }
        for(std::size_t i = 0; i < lhs.size(); ++i) {
            if(!detail::isSameRegOp(lhs[i], rhs[i])) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr auto internOps(const std::vector<RegOp>& ops,
                                           std::vector<std::vector<RegOp>>& opPool) const
        -> std::size_t {
        for(std::size_t i = 0; i < opPool.size(); ++i) {
            if(equalOps(ops, opPool[i])) {
                return i;
            }
        }
        opPool.push_back(ops);
        return opPool.size() - 1;
    }

    [[nodiscard]] constexpr auto sameSignature(const std::vector<std::size_t>& lhs,
                                               const std::vector<std::size_t>& rhs) const -> bool {
        if(lhs.size() != rhs.size()) {
            return false;
        }
        for(std::size_t i = 0; i < lhs.size(); ++i) {
            if(lhs[i] != rhs[i]) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr auto minimizeStates() -> bool {
        const auto n = states_.size();
        if(n < 2) {
            return true;
        }

        std::vector<std::vector<RegOp>> uniqueOps;
        std::vector<std::size_t> edgeOpId(n * classCount_, 0);
        std::vector<std::size_t> finalOpId(n, 0);
        const auto edgeOpOffset = [&](std::size_t state, std::size_t classId) -> std::size_t {
            return state * classCount_ + classId;
        };

        for(std::size_t s = 0; s < n; ++s) {
            for(std::size_t c = 0; c < classCount_; ++c) {
                if(!edgeRef(s, c).enabled) {
                    continue;
                }
                edgeOpId[edgeOpOffset(s, c)] = internOps(edgeRef(s, c).ops, uniqueOps);
            }
            if(states_[s].isFinal) {
                finalOpId[s] = internOps(states_[s].finalOps, uniqueOps) + 1;
            }
        }

        std::vector<std::size_t> part(n, 0);
        std::vector<std::vector<std::size_t>> initSigs;
        for(std::size_t s = 0; s < n; ++s) {
            std::vector<std::size_t> sig;
            sig.push_back(states_[s].isFinal ? 1u : 0u);
            sig.push_back(finalOpId[s]);

            std::size_t gid = initSigs.size();
            for(std::size_t i = 0; i < initSigs.size(); ++i) {
                if(sameSignature(sig, initSigs[i])) {
                    gid = i;
                    break;
                }
            }
            if(gid == initSigs.size()) {
                initSigs.push_back(std::move(sig));
            }
            part[s] = gid;
        }

        bool changed = true;
        while(changed) {
            changed = false;
            std::vector<std::vector<std::size_t>> sigs;
            std::vector<std::size_t> newPart(n, 0);

            for(std::size_t s = 0; s < n; ++s) {
                std::vector<std::size_t> sig;
                sig.reserve(2 + classCount_ * 3);
                sig.push_back(states_[s].isFinal ? 1u : 0u);
                sig.push_back(finalOpId[s]);

                for(std::size_t c = 0; c < classCount_; ++c) {
                    if(!edgeRef(s, c).enabled) {
                        sig.push_back(0);
                        sig.push_back(0);
                        sig.push_back(0);
                        continue;
                    }
                    const auto to = edgeRef(s, c).to;
                    if(to >= n) {
                        return setError(TDFAError::invalid_tnfa);
                    }
                    sig.push_back(1);
                    sig.push_back(edgeOpId[edgeOpOffset(s, c)] + 1);
                    sig.push_back(part[to] + 1);
                }

                std::size_t gid = sigs.size();
                for(std::size_t i = 0; i < sigs.size(); ++i) {
                    if(sameSignature(sig, sigs[i])) {
                        gid = i;
                        break;
                    }
                }
                if(gid == sigs.size()) {
                    sigs.push_back(std::move(sig));
                }
                newPart[s] = gid;
                if(newPart[s] != part[s]) {
                    changed = true;
                }
            }

            part = std::move(newPart);
        }

        std::size_t groupN = 0;
        for(const auto p: part) {
            if(p + 1 > groupN) {
                groupN = p + 1;
            }
        }
        if(groupN == n) {
            return true;
        }

        std::vector<std::size_t> rep(groupN, n);
        for(std::size_t s = 0; s < n; ++s) {
            if(rep[part[s]] == n) {
                rep[part[s]] = s;
            }
        }

        std::vector<DFAStateInner> newStates(groupN);
        std::vector<TempEdge> newDelta(groupN * classCount_);

        for(std::size_t g = 0; g < groupN; ++g) {
            const auto rs = rep[g];
            if(rs >= n) {
                return setError(TDFAError::invalid_tnfa);
            }

            newStates[g].isFinal = states_[rs].isFinal;
            newStates[g].finalOps = states_[rs].finalOps;

            for(std::size_t c = 0; c < classCount_; ++c) {
                if(!edgeRef(rs, c).enabled) {
                    continue;
                }
                const auto to = edgeRef(rs, c).to;
                if(to >= n) {
                    return setError(TDFAError::invalid_tnfa);
                }
                auto& edge = newDelta[g * classCount_ + c];
                edge.enabled = true;
                edge.to = static_cast<StateTy>(part[to]);
                edge.ops = edgeRef(rs, c).ops;
            }
        }

        if(startState_ >= n) {
            return setError(TDFAError::invalid_tnfa);
        }
        startState_ = static_cast<StateTy>(part[startState_]);
        states_ = std::move(newStates);
        delta_.clear();
        for(auto& edge: newDelta) {
            pushDeltaEdge(std::move(edge));
        }
        return true;
    }

    [[nodiscard]] constexpr auto makeEmptyMarks() const -> std::vector<detail::HistValue> {
        return std::vector<detail::HistValue>(tagCount_ + 1, detail::HistValue::none);
    }

    [[nodiscard]] constexpr auto buildPrecedence(const std::vector<Config4>& closure) const
        -> std::vector<StateTy> {
        std::vector<StateTy> out;
        out.reserve(closure.size());
        for(const auto& cfg: closure) {
            out.push_back(cfg.q);
        }
        return out;
    }

    [[nodiscard]] constexpr auto stepOnSymbol(const DFAStateInner& state, ClassId classId) const
        -> std::vector<Config4> {
        std::vector<Config4> moved;

        for(const auto q: state.precedence) {
            const auto* cfg = findConfig(state, q);
            if(cfg == nullptr) {
                continue;
            }
            if(q >= Traits::kStateN) {
                continue;
            }

            const auto& charSteps = tnfaModel_.transition.toMap[q].charSteps;
            for(const auto& edge: charSteps) {
                if(!edge.isEnabled) {
                    continue;
                }
                if(!supportsClass(edge, classId)) {
                    continue;
                }

                moved.push_back(Config4{edge.another, cfg->regs, cfg->lookahead, makeEmptyMarks()});
            }
        }

        return moved;
    }

    using CharStepTy =
        std::remove_cvref_t<decltype(std::declval<TNFADataTy>().transition.toMap[0].charSteps[0])>;

    [[nodiscard]] constexpr auto supportsClass(const CharStepTy& edge, ClassId classId) const
        -> bool {
        for(std::size_t i = 0; i < edge.charSupportRefIdx; ++i) {
            if(i >= edge.charSupportRef.size()) {
                break;
            }
            if(static_cast<std::size_t>(edge.charSupportRef[i]) ==
               static_cast<std::size_t>(classId)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr auto epsilonClosure(const std::vector<Config4>& moved)
        -> std::vector<Config4> {
        std::vector<Config4> stack;
        stack.reserve(moved.size() + Traits::kStateN);
        for(auto it = moved.rbegin(); it != moved.rend(); ++it) {
            stack.push_back(*it);
        }

        std::vector<unsigned char> visited(Traits::kStateN, 0);
        std::vector<Config4> closure;
        closure.reserve(Traits::kStateN);

        while(!stack.empty()) {
            auto now = std::move(stack.back());
            stack.pop_back();

            if(now.q >= Traits::kStateN) {
                setError(TDFAError::invalid_tnfa);
                return {};
            }
            if(visited[now.q] != 0) {
                continue;
            }

            visited[now.q] = 1;
            closure.push_back(now);

            const auto& epsSteps = tnfaModel_.transition.toMap[now.q].epsilonSteps;
            for(std::size_t i = epsSteps.size(); i > 0; --i) {
                const auto& edge = epsSteps[i - 1];
                if(!edge.isEnabled) {
                    continue;
                }

                auto next = now;
                next.q = edge.another;
                if(!applyTag(next.lookahead, edge.tag)) {
                    return {};
                }

                if(next.q < visited.size() && visited[next.q] == 0) {
                    stack.push_back(std::move(next));
                }
            }
        }

        std::vector<Config4> filtered;
        filtered.reserve(closure.size());
        for(auto& cfg: closure) {
            if(cfg.q == tnfaModel_.finalState ||
               TNFA::hasSymbolTransition(tnfaModel_.transition, cfg.q)) {
                filtered.push_back(std::move(cfg));
            }
        }
        return filtered;
    }

    [[nodiscard]] constexpr auto applyTag(std::vector<detail::HistValue>& marks, TagTy tag)
        -> bool {
        if(tag == tagEpsilon) {
            return true;
        }
        const auto absTag = static_cast<std::size_t>(tag > 0 ? tag : -tag);
        if(absTag == 0 || absTag > tagCount_ || absTag >= marks.size()) {
            return setError(TDFAError::invalid_tnfa);
        }
        marks[absTag] = tag > 0 ? detail::HistValue::cur : detail::HistValue::nil;
        return true;
    }

    [[nodiscard]] constexpr auto transitionRegOps(std::vector<Config4>& closure,
                                                  std::vector<std::vector<RhsAlloc>>& regCache)
        -> std::vector<RegOp> {
        std::vector<RegOp> ops;

        for(auto& cfg: closure) {
            for(std::size_t t = 1; t <= tagCount_; ++t) {
                const auto mark = cfg.inherited[t];
                if(mark == detail::HistValue::none) {
                    continue;
                }

                RhsKey key{};
                if(mark == detail::HistValue::cur) {
                    key.kind = RegOpKind::SetCur;
                } else {
                    key.kind = RegOpKind::SetNil;
                }

                RegisterId outReg = 0;
                auto& cache = regCache[t];
                for(const auto& entry: cache) {
                    if(entry.key.kind == key.kind && entry.key.rhs == key.rhs) {
                        outReg = entry.reg;
                        break;
                    }
                }

                if(outReg == 0) {
                    outReg = allocRegister();
                    if(outReg == 0) {
                        return {};
                    }
                    cache.push_back(RhsAlloc{key, outReg});
                }

                const RegOp op{key.kind, outReg, key.rhs};
                appendUnique(ops, op);
                cfg.regs[t] = outReg;
            }
        }
        return ops;
    }

    constexpr void appendUnique(std::vector<RegOp>& ops, const RegOp& op) const {
        for(const auto& existed: ops) {
            if(detail::isSameRegOp(existed, op)) {
                return;
            }
        }
        ops.push_back(op);
    }

    [[nodiscard]] constexpr auto allocRegister() -> RegisterId {
        const auto next = regCount_ + 1;
        if(next > static_cast<std::size_t>(std::numeric_limits<RegisterId>::max())) {
            setError(TDFAError::register_capacity_exceeded);
            return 0;
        }
        if(!counting && next > kRegCap) {
            setError(TDFAError::register_capacity_exceeded);
            return 0;
        }
        regCount_ = next;
        peakRegCount_ = std::max(peakRegCount_, regCount_);
        return static_cast<RegisterId>(next);
    }

    [[nodiscard]] constexpr auto addState(const std::vector<Config4>& closure,
                                          const std::vector<StateTy>& precedence,
                                          std::vector<RegOp>& ops) -> StateTy {
        auto candidate = makeDFAState(closure, precedence);
        if(buildError_ != TDFAError::none) {
            return 0;
        }

        for(std::size_t i = 0; i < states_.size(); ++i) {
            if(sameFullState(candidate, states_[i])) {
                return static_cast<StateTy>(i);
            }
        }

        for(std::size_t i = 0; i < states_.size(); ++i) {
            auto mappedOps = ops;
            if(mapToExisting(candidate, states_[i], mappedOps)) {
                ops = std::move(mappedOps);
                return static_cast<StateTy>(i);
            }
        }

        if(!counting && states_.size() >= kStateCap) {
            setError(TDFAError::state_capacity_exceeded);
            return 0;
        }
        if(states_.size() >= static_cast<std::size_t>(std::numeric_limits<StateTy>::max())) {
            setError(TDFAError::state_capacity_exceeded);
            return 0;
        }

        const auto stateIdx = static_cast<StateTy>(states_.size());
        states_.push_back(std::move(candidate));
        peakStateCount_ = std::max(peakStateCount_, states_.size());
        for(std::size_t c = 0; c < classCount_; ++c) {
            pushDeltaEdge();
        }
        updateStateRegStat(states_.back());
        return stateIdx;
    }

    constexpr void updateStateRegStat(const DFAStateInner& state) {
        std::vector<unsigned char> used(regCount_ + 1, 0);
        std::size_t distinct = 0;
        for(const auto& cfg: state.configs) {
            for(std::size_t t = 1; t <= tagCount_; ++t) {
                const auto reg = cfg.regs[t];
                if(reg == 0 || static_cast<std::size_t>(reg) >= used.size()) {
                    continue;
                }
                if(used[reg] == 0) {
                    used[reg] = 1;
                    ++distinct;
                }
            }
        }
        if(distinct > counting_.stateRegN) {
            counting_.stateRegN = clampToU16(distinct);
        }
    }

    [[nodiscard]] constexpr auto makeDFAState(const std::vector<Config4>& closure,
                                              const std::vector<StateTy>& precedence)
        -> DFAStateInner {
        DFAStateInner state{};
        state.precedence = precedence;
        state.configs.reserve(closure.size());

        const Config4* finalCfg = nullptr;
        for(const auto& cfg: closure) {
            state.configs.push_back(Config3{cfg.q, cfg.regs, cfg.lookahead});
            if(cfg.q == tnfaModel_.finalState && finalCfg == nullptr) {
                finalCfg = &cfg;
            }
        }

        std::sort(state.configs.begin(),
                  state.configs.end(),
                  [](const Config3& lhs, const Config3& rhs) { return lhs.q < rhs.q; });

        if(finalCfg != nullptr) {
            state.isFinal = true;
            state.finalOps = finalRegOps(*finalCfg);
        }
        return state;
    }

    [[nodiscard]] constexpr auto finalRegOps(const Config4& finalCfg) -> std::vector<RegOp> {
        std::vector<RegOp> ops;
        ops.reserve(tagCount_);

        for(std::size_t t = 1; t <= tagCount_; ++t) {
            const auto lhs = finalRegOfTag_[t];
            if(finalCfg.lookahead[t] == detail::HistValue::cur) {
                ops.push_back(RegOp{RegOpKind::SetCur, lhs, 0});
            } else if(finalCfg.lookahead[t] == detail::HistValue::nil) {
                ops.push_back(RegOp{RegOpKind::SetNil, lhs, 0});
            } else {
                ops.push_back(RegOp{RegOpKind::Copy, lhs, finalCfg.regs[t]});
            }
        }

        if(!topologicalSortOps(ops)) {
            setError(TDFAError::invalid_tnfa);
            return {};
        }
        return ops;
    }

    [[nodiscard]] constexpr auto findConfig(const DFAStateInner& state, StateTy q) const
        -> const Config3* {
        const auto it =
            std::lower_bound(state.configs.begin(),
                             state.configs.end(),
                             q,
                             [](const Config3& cfg, StateTy target) { return cfg.q < target; });
        if(it != state.configs.end() && it->q == q) {
            return &*it;
        }
        return nullptr;
    }

    [[nodiscard]] constexpr auto sameLayout(const DFAStateInner& lhs,
                                            const DFAStateInner& rhs) const -> bool {
        if(lhs.precedence != rhs.precedence) {
            return false;
        }
        if(lhs.configs.size() != rhs.configs.size()) {
            return false;
        }
        for(std::size_t i = 0; i < lhs.configs.size(); ++i) {
            if(lhs.configs[i].q != rhs.configs[i].q) {
                return false;
            }
            if(lhs.configs[i].lookahead != rhs.configs[i].lookahead) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr auto sameFullState(const DFAStateInner& lhs,
                                               const DFAStateInner& rhs) const -> bool {
        if(!sameLayout(lhs, rhs)) {
            return false;
        }
        for(std::size_t i = 0; i < lhs.configs.size(); ++i) {
            if(lhs.configs[i].regs != rhs.configs[i].regs) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr auto mapToExisting(const DFAStateInner& src,
                                               const DFAStateInner& dst,
                                               std::vector<RegOp>& ops) const -> bool {
        if(!sameLayout(src, dst)) {
            return false;
        }

        std::vector<RegisterId> mapSrcToDst(regCount_ + 1, 0);
        std::vector<RegisterId> mapDstToSrc(regCount_ + 1, 0);

        for(std::size_t i = 0; i < src.configs.size(); ++i) {
            const auto& lhsCfg = src.configs[i];
            const auto& rhsCfg = dst.configs[i];
            for(std::size_t t = 1; t <= tagCount_; ++t) {
                if(lhsCfg.lookahead[t] != detail::HistValue::none) {
                    continue;
                }
                const auto srcReg = lhsCfg.regs[t];
                const auto dstReg = rhsCfg.regs[t];
                if(srcReg == 0 || dstReg == 0) {
                    continue;
                }
                if(static_cast<std::size_t>(srcReg) >= mapSrcToDst.size() ||
                   static_cast<std::size_t>(dstReg) >= mapDstToSrc.size()) {
                    return false;
                }

                if(mapSrcToDst[srcReg] == 0 && mapDstToSrc[dstReg] == 0) {
                    mapSrcToDst[srcReg] = dstReg;
                    mapDstToSrc[dstReg] = srcReg;
                    continue;
                }
                if(mapSrcToDst[srcReg] != dstReg || mapDstToSrc[dstReg] != srcReg) {
                    return false;
                }
            }
        }

        for(auto& op: ops) {
            const auto srcLhs = op.lhs;
            if(static_cast<std::size_t>(srcLhs) >= mapSrcToDst.size()) {
                return false;
            }
            if(mapSrcToDst[srcLhs] == 0) {
                continue;
            }
            const auto dstLhs = mapSrcToDst[srcLhs];
            op.lhs = dstLhs;
            mapDstToSrc[dstLhs] = 0;
            mapSrcToDst[srcLhs] = 0;
        }

        std::vector<RegOp> merged;
        merged.reserve(ops.size() + mapSrcToDst.size());

        for(std::size_t srcReg = 1; srcReg < mapSrcToDst.size(); ++srcReg) {
            const auto dstReg = mapSrcToDst[srcReg];
            if(dstReg == 0 || dstReg == srcReg) {
                continue;
            }
            merged.push_back(RegOp{RegOpKind::Copy, dstReg, static_cast<RegisterId>(srcReg)});
        }
        for(const auto& op: ops) {
            merged.push_back(op);
        }

        if(!topologicalSortOps(merged)) {
            return false;
        }
        ops = std::move(merged);
        return true;
    }

    [[nodiscard]] constexpr static auto readRegister(const RegOp& op) -> RegisterId {
        if(op.kind == RegOpKind::Copy || op.kind == RegOpKind::Append) {
            return op.rhs;
        }
        return 0;
    }

    [[nodiscard]] constexpr auto topologicalSortOps(std::vector<RegOp>& ops) const -> bool {
        std::vector<RegOp> filtered;
        filtered.reserve(ops.size());
        for(const auto& op: ops) {
            if(op.kind == RegOpKind::Copy && op.lhs == op.rhs) {
                continue;
            }
            bool existed = false;
            for(const auto& oldOp: filtered) {
                if(detail::isSameRegOp(oldOp, op)) {
                    existed = true;
                    break;
                }
            }
            if(!existed) {
                filtered.push_back(op);
            }
        }

        const auto n = filtered.size();
        if(n < 2) {
            ops = std::move(filtered);
            return true;
        }

        std::vector<std::vector<unsigned char>> dep(n, std::vector<unsigned char>(n, 0));
        std::vector<std::size_t> indegree(n, 0);

        for(std::size_t i = 0; i < n; ++i) {
            const auto readReg = readRegister(filtered[i]);
            if(readReg == 0) {
                continue;
            }
            for(std::size_t j = 0; j < n; ++j) {
                if(i == j) {
                    continue;
                }
                if(filtered[j].lhs != readReg || dep[i][j] != 0) {
                    continue;
                }
                dep[i][j] = 1;
                indegree[j] += 1;
            }
        }

        std::vector<unsigned char> used(n, 0);
        std::vector<RegOp> sorted;
        sorted.reserve(n);

        for(std::size_t done = 0; done < n; ++done) {
            std::size_t pick = n;
            for(std::size_t i = 0; i < n; ++i) {
                if(used[i] == 0 && indegree[i] == 0) {
                    pick = i;
                    break;
                }
            }

            if(pick == n) {
                return false;
            }

            used[pick] = 1;
            sorted.push_back(filtered[pick]);
            for(std::size_t j = 0; j < n; ++j) {
                if(dep[pick][j] != 0 && indegree[j] > 0) {
                    indegree[j] -= 1;
                }
            }
        }

        ops = std::move(sorted);
        return true;
    }

    [[nodiscard]] constexpr auto countOperations() const -> std::size_t {
        std::size_t total = 0;
        std::vector<std::vector<RegOp>> uniqueOps;

        const auto account = [&](const std::vector<RegOp>& ops) {
            if(ops.empty()) {
                return;
            }
            for(const auto& existed: uniqueOps) {
                if(equalOps(ops, existed)) {
                    return;
                }
            }
            uniqueOps.push_back(ops);
            total += ops.size();
        };

        for(std::size_t s = 0; s < states_.size(); ++s) {
            for(std::size_t c = 0; c < classCount_; ++c) {
                if(!edgeRef(s, c).enabled) {
                    continue;
                }
                account(edgeRef(s, c).ops);
            }
            if(states_[s].isFinal) {
                account(states_[s].finalOps);
            }
        }
        return total;
    }

    constexpr void computeLengthBounds(ModelTy& model) const {
        constexpr uint32_t kInf = std::numeric_limits<uint32_t>::max();

        model.minInputLen = 0;
        model.maxInputLen = kInf;

        const auto n = states_.size();
        if(n == 0 || startState_ >= n) {
            return;
        }

        std::vector<unsigned char> reachable(n, 0);
        std::vector<StateTy> queue;
        queue.reserve(n);
        queue.push_back(startState_);
        reachable[startState_] = 1;
        for(std::size_t i = 0; i < queue.size(); ++i) {
            const auto s = queue[i];
            for(std::size_t c = 0; c < classCount_; ++c) {
                if(!model.delta[s][c].enabled) {
                    continue;
                }
                const auto to = model.delta[s][c].to;
                if(to >= n || reachable[to] != 0) {
                    continue;
                }
                reachable[to] = 1;
                queue.push_back(to);
            }
        }

        std::vector<std::vector<StateTy>> reverse(n);
        for(std::size_t s = 0; s < n; ++s) {
            for(std::size_t c = 0; c < classCount_; ++c) {
                if(!model.delta[s][c].enabled) {
                    continue;
                }
                const auto to = model.delta[s][c].to;
                if(to < n) {
                    reverse[to].push_back(static_cast<StateTy>(s));
                }
            }
        }

        std::vector<unsigned char> canReachFinal(n, 0);
        queue.clear();
        for(std::size_t s = 0; s < n; ++s) {
            if(!model.isFinal[s]) {
                continue;
            }
            canReachFinal[s] = 1;
            queue.push_back(static_cast<StateTy>(s));
        }
        for(std::size_t i = 0; i < queue.size(); ++i) {
            const auto s = queue[i];
            for(const auto pre: reverse[s]) {
                if(canReachFinal[pre] != 0) {
                    continue;
                }
                canReachFinal[pre] = 1;
                queue.push_back(pre);
            }
        }

        std::vector<unsigned char> relevant(n, 0);
        std::size_t relevantCount = 0;
        for(std::size_t s = 0; s < n; ++s) {
            if(reachable[s] != 0 && canReachFinal[s] != 0) {
                relevant[s] = 1;
                ++relevantCount;
            }
        }
        if(relevant[startState_] == 0) {
            return;
        }

        std::vector<uint32_t> dist(n, kInf);
        queue.clear();
        dist[startState_] = 0;
        queue.push_back(startState_);
        for(std::size_t i = 0; i < queue.size(); ++i) {
            const auto s = queue[i];
            const auto d = dist[s];
            for(std::size_t c = 0; c < classCount_; ++c) {
                if(!model.delta[s][c].enabled) {
                    continue;
                }
                const auto to = model.delta[s][c].to;
                if(to >= n || reachable[to] == 0) {
                    continue;
                }
                const auto nd = d + 1;
                if(nd < dist[to]) {
                    dist[to] = nd;
                    queue.push_back(to);
                }
            }
        }

        uint32_t minLen = kInf;
        for(std::size_t s = 0; s < n; ++s) {
            if(!model.isFinal[s] || relevant[s] == 0) {
                continue;
            }
            if(dist[s] < minLen) {
                minLen = dist[s];
            }
        }
        if(minLen == kInf) {
            return;
        }
        model.minInputLen = minLen;

        std::vector<uint32_t> indegree(n, 0);
        for(std::size_t s = 0; s < n; ++s) {
            if(relevant[s] == 0) {
                continue;
            }
            for(std::size_t c = 0; c < classCount_; ++c) {
                if(!model.delta[s][c].enabled) {
                    continue;
                }
                const auto to = model.delta[s][c].to;
                if(to < n && relevant[to] != 0) {
                    indegree[to] += 1;
                }
            }
        }

        queue.clear();
        queue.reserve(relevantCount);
        for(std::size_t s = 0; s < n; ++s) {
            if(relevant[s] != 0 && indegree[s] == 0) {
                queue.push_back(static_cast<StateTy>(s));
            }
        }

        std::vector<StateTy> topo;
        topo.reserve(relevantCount);
        for(std::size_t i = 0; i < queue.size(); ++i) {
            const auto s = queue[i];
            topo.push_back(s);
            for(std::size_t c = 0; c < classCount_; ++c) {
                if(!model.delta[s][c].enabled) {
                    continue;
                }
                const auto to = model.delta[s][c].to;
                if(to >= n || relevant[to] == 0) {
                    continue;
                }
                if(--indegree[to] == 0) {
                    queue.push_back(to);
                }
            }
        }

        if(topo.size() < relevantCount) {
            model.maxInputLen = kInf;
            return;
        }

        std::vector<int64_t> longest(n, -1);
        longest[startState_] = 0;
        for(const auto s: topo) {
            const auto base = longest[s];
            if(base < 0) {
                continue;
            }
            for(std::size_t c = 0; c < classCount_; ++c) {
                if(!model.delta[s][c].enabled) {
                    continue;
                }
                const auto to = model.delta[s][c].to;
                if(to >= n || relevant[to] == 0) {
                    continue;
                }
                const auto cand = base + 1;
                if(cand > longest[to]) {
                    longest[to] = cand;
                }
            }
        }

        uint32_t maxLen = 0;
        for(std::size_t s = 0; s < n; ++s) {
            if(!model.isFinal[s] || relevant[s] == 0 || longest[s] < 0) {
                continue;
            }
            const auto candidate = static_cast<uint32_t>(longest[s]);
            if(candidate > maxLen) {
                maxLen = candidate;
            }
        }
        model.maxInputLen = maxLen;
    }

    constexpr void computeExactLiteral(ModelTy& model) const {
        model.isExactLiteral = false;
        model.literalLen = 0;

        if(model.maxInputLen == std::numeric_limits<uint32_t>::max() ||
           model.minInputLen != model.maxInputLen) {
            return;
        }

        const auto len = static_cast<std::size_t>(model.maxInputLen);
        if(len > model.literalBytes.size() || startState_ >= states_.size()) {
            return;
        }

        StateTy state = startState_;
        for(std::size_t i = 0; i < len; ++i) {
            if(state >= states_.size()) {
                return;
            }

            std::size_t enabledCount = 0;
            std::size_t classId = 0;
            for(std::size_t c = 0; c < classCount_; ++c) {
                if(!model.delta[state][c].enabled) {
                    continue;
                }
                enabledCount += 1;
                classId = c;
                if(enabledCount > 1) {
                    return;
                }
            }
            if(enabledCount != 1) {
                return;
            }

            const auto range = model.rangeToPair[classId];
            if(range.first != range.second) {
                return;
            }

            model.literalBytes[i] = static_cast<char>(range.first);
            state = model.delta[state][classId].to;
        }

        if(state >= states_.size() || !model.isFinal[state]) {
            return;
        }

        model.isExactLiteral = true;
        model.literalLen = static_cast<uint16_t>(len);
    }

    [[nodiscard]] constexpr auto materializeModel() -> std::optional<ModelTy> {
        ModelTy model{};
        model.startState = startState_;
        model.stateIdx = clampToU16(states_.size());
        model.classCount = clampToU16(classCount_);
        model.regCount = clampToU16(regCount_);
        model.tagCount = clampToU16(tagCount_);
        std::vector<std::vector<RegOp>> uniqueOps;
        std::vector<OpSlice> uniqueSlices;
        std::vector<std::uint64_t> uniqueHashes;
        for(std::size_t s = 0; s < states_.size(); ++s) {
            model.byteDelta[s].fill(ModelTy::deadState);
        }

        for(std::size_t i = 0; i < classCount_; ++i) {
            model.rangeToPair[i] = tnfaModel_.transition.splitRanges[i];
        }
        model.classByBytePlusOne.fill(0);
        for(std::size_t i = 0; i < classCount_; ++i) {
            const auto range = model.rangeToPair[i];
            const auto classCode = static_cast<uint16_t>(i + 1);
            for(uint16_t code = range.first; code <= range.second; ++code) {
                model.classByBytePlusOne[code] = classCode;
            }
        }
        for(std::size_t t = 1; t <= tagCount_; ++t) {
            model.finalRegOfTag[t] = finalRegOfTag_[t];
        }

        for(std::size_t s = 0; s < states_.size(); ++s) {
            model.isFinal[s] = states_[s].isFinal;

            for(std::size_t c = 0; c < classCount_; ++c) {
                const auto& edge = edgeRef(s, c);
                if(!edge.enabled) {
                    continue;
                }
                const auto slice =
                    appendSliceInterned(model, edge.ops, uniqueOps, uniqueSlices, uniqueHashes);
                if(buildError_ != TDFAError::none) {
                    return std::nullopt;
                }
                model.delta[s][c].enabled = true;
                model.delta[s][c].to = edge.to;
                model.delta[s][c].ops = slice;

                const auto range = model.rangeToPair[c];
                for(uint16_t code = range.first; code <= range.second; ++code) {
                    model.byteDelta[s][code] = edge.to;
                }
            }

            if(states_[s].isFinal) {
                const auto slice =
                    appendSliceInterned(
                        model, states_[s].finalOps, uniqueOps, uniqueSlices, uniqueHashes);
                if(buildError_ != TDFAError::none) {
                    return std::nullopt;
                }
                model.finalOps[s] = slice;
            }
        }

        if constexpr(EnableLengthPrune) {
            computeLengthBounds(model);
        } else {
            model.minInputLen = 0;
            model.maxInputLen = std::numeric_limits<uint32_t>::max();
        }

        if constexpr(EnableExactLiteralFastpath && EnableLengthPrune) {
            computeExactLiteral(model);
        } else {
            model.isExactLiteral = false;
            model.literalLen = 0;
        }

        model.error = buildError_;
        return model;
    }

    [[nodiscard]] constexpr auto appendSlice(ModelTy& model, const std::vector<RegOp>& ops)
        -> OpSlice {
        if(ops.empty()) {
            return OpSlice{0, 0};
        }

        if(model.opPoolIdx + ops.size() > model.opPool.size()) {
            setError(TDFAError::op_capacity_exceeded);
            return {};
        }
        if(ops.size() > static_cast<std::size_t>(std::numeric_limits<uint16_t>::max())) {
            setError(TDFAError::op_capacity_exceeded);
            return {};
        }

        const auto off = model.opPoolIdx;
        for(const auto& op: ops) {
            model.opPool[model.opPoolIdx++] = op;
        }

        return OpSlice{off, static_cast<uint16_t>(ops.size())};
    }

    [[nodiscard]] constexpr auto hashOps(const std::vector<RegOp>& ops) const -> std::uint64_t {
        // FNV-1a over op-kind/lhs/rhs triples.
        std::uint64_t hash = 1469598103934665603ULL;
        for(const auto& op: ops) {
            hash ^= static_cast<std::uint64_t>(op.kind);
            hash *= 1099511628211ULL;
            hash ^= static_cast<std::uint64_t>(op.lhs);
            hash *= 1099511628211ULL;
            hash ^= static_cast<std::uint64_t>(op.rhs);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    [[nodiscard]] constexpr auto appendSliceInterned(ModelTy& model,
                                                     const std::vector<RegOp>& ops,
                                                     std::vector<std::vector<RegOp>>& uniqueOps,
                                                     std::vector<OpSlice>& uniqueSlices,
                                                     std::vector<std::uint64_t>& uniqueHashes)
        -> OpSlice {
        if(ops.empty()) {
            return OpSlice{0, 0};
        }

        const auto opsHash = hashOps(ops);
        for(std::size_t i = 0; i < uniqueOps.size(); ++i) {
            if(uniqueHashes[i] == opsHash && equalOps(ops, uniqueOps[i])) {
                return uniqueSlices[i];
            }
        }

        const auto slice = appendSlice(model, ops);
        if(buildError_ != TDFAError::none) {
            return {};
        }
        uniqueOps.push_back(ops);
        uniqueSlices.push_back(slice);
        uniqueHashes.push_back(opsHash);
        return slice;
    }

private:
    const TNFAModelTy& tnfaModel_;
    TDFAError buildError_ = TDFAError::none;

    std::size_t classCount_ = 0;
    std::size_t tagCount_ = 0;
    std::size_t regCount_ = 0;
    std::size_t peakRegCount_ = 0;
    std::size_t peakStateCount_ = 0;
    StateTy startState_ = 0;

    std::vector<RegisterId> finalRegOfTag_{};
    std::vector<DFAStateInner> states_{};
    DeltaResourceTy deltaResource_{};
    DeltaPoolTy delta_;
    std::size_t deltaPeak_ = 0;

    TDFACounting counting_{};
};

template <typename TNFAModelTy,
          std::size_t StateN,
          std::size_t RegN,
          std::size_t OpN,
          std::size_t TagN,
          bool EnableRegOptimize = true,
          bool EnableStateMinimize = true,
          bool EnableLengthPrune = true,
          bool EnableExactLiteralFastpath = true>
[[nodiscard]] constexpr auto fromTNFA(const TNFAModelTy& tnfaModel)
    -> std::optional<TDFAModel<StateN,
                               detail::TNFATraits<std::remove_cvref_t<TNFAModelTy>>::kMaxSplitRange,
                               RegN,
                               OpN,
                               TagN>> {
    Builder<TNFAModelTy,
            false,
            RegN,
            StateN,
            OpN,
            TagN,
            EnableRegOptimize,
            EnableStateMinimize,
            EnableLengthPrune,
            EnableExactLiteralFastpath>
        builder(tnfaModel);
    return builder.build();
}

template <auto tnfaModel,
          bool EnableRegOptimize = true,
          bool EnableStateMinimize = true,
          bool EnableLengthPrune = true,
          bool EnableExactLiteralFastpath = true>
[[nodiscard]] consteval auto fromTNFAAutoTuned() {
    using TNFAModelTy = std::remove_cvref_t<decltype(tnfaModel)>;
    constexpr auto meta = []() consteval {
        Builder<TNFAModelTy,
                true,
                0,
                0,
                0,
                0,
                EnableRegOptimize,
                EnableStateMinimize,
                EnableLengthPrune,
                EnableExactLiteralFastpath>
            builder(tnfaModel);
        const auto counting = builder.build();
        return std::pair{counting, builder.genDeltaRecord()};
    }();
    constexpr auto counting = meta.first;
    constexpr auto deltaRecord = meta.second;

    constexpr auto tagNRaw = tnfaModel.tagSize();
    constexpr auto stateN =
        counting.dfaStateN == 0 ? 1u : static_cast<unsigned>(counting.dfaStateN);
    constexpr auto regN = counting.regN == 0 ? 1u : static_cast<unsigned>(counting.regN);
    constexpr auto opN = counting.opN == 0 ? 1u : static_cast<unsigned>(counting.opN);
    constexpr auto tagN = tagNRaw == 0 ? 1u : static_cast<unsigned>(tagNRaw);

    Builder<TNFAModelTy,
            false,
            regN,
            stateN,
            opN,
            tagN,
            EnableRegOptimize,
            EnableStateMinimize,
            EnableLengthPrune,
            EnableExactLiteralFastpath,
            deltaRecord>
        builder(tnfaModel);
    return builder.build();
}

template <auto tnfaModel>
[[nodiscard]] consteval auto fromTNFAAuto() {
    return fromTNFAAutoTuned<tnfaModel>();
}

template <std::size_t MaxStates,
          std::size_t MaxCharRanges,
          std::size_t MaxRegs,
          std::size_t MaxOps,
          std::size_t MaxTags>
[[nodiscard]] constexpr auto
    classByBytePlusOne(const TDFAModel<MaxStates, MaxCharRanges, MaxRegs, MaxOps, MaxTags>& model,
                       char ch) -> uint16_t {
    const auto code = static_cast<unsigned char>(ch);
    return model.classByBytePlusOne[code];
}

template <std::size_t MaxStates,
          std::size_t MaxCharRanges,
          std::size_t MaxRegs,
          std::size_t MaxOps,
          std::size_t MaxTags>
[[nodiscard]] constexpr auto
    classify(const TDFAModel<MaxStates, MaxCharRanges, MaxRegs, MaxOps, MaxTags>& model, char ch)
        -> std::optional<ClassId> {
    const auto encoded = classByBytePlusOne(model, ch);
    if(encoded == 0) {
        return std::nullopt;
    }
    return static_cast<ClassId>(encoded - 1);
}

template <std::size_t MaxStates,
          std::size_t MaxCharRanges,
          std::size_t MaxRegs,
          std::size_t MaxOps,
          std::size_t MaxTags,
          typename RegisterStorage>
constexpr void applyOpsUnchecked(const TDFAModel<MaxStates, MaxCharRanges, MaxRegs, MaxOps, MaxTags>& model,
                                 OpSlice slice,
                                 RegisterStorage& registers,
                                 int curPos,
                                 int unsetValue) {
    for(std::size_t i = 0; i < slice.len; ++i) {
        const auto& op = model.opPool[slice.off + i];
        switch(op.kind) {
            case RegOpKind::SetNil: registers[op.lhs] = unsetValue; break;
            case RegOpKind::SetCur: registers[op.lhs] = curPos; break;
            case RegOpKind::Copy:
            case RegOpKind::Append: registers[op.lhs] = registers[op.rhs]; break;
        }
    }
}

template <std::size_t MaxStates,
          std::size_t MaxCharRanges,
          std::size_t MaxRegs,
          std::size_t MaxOps,
          std::size_t MaxTags,
          typename RegisterStorage>
[[nodiscard]] constexpr auto
    applyOps(const TDFAModel<MaxStates, MaxCharRanges, MaxRegs, MaxOps, MaxTags>& model,
             OpSlice slice,
             RegisterStorage& registers,
             int curPos,
             int unsetValue) -> bool {
    if(static_cast<std::size_t>(slice.off) + static_cast<std::size_t>(slice.len) >
       model.opPoolIdx) {
        return false;
    }

    for(std::size_t i = 0; i < slice.len; ++i) {
        const auto& op = model.opPool[slice.off + i];
        if(static_cast<std::size_t>(op.lhs) >= registers.size()) {
            return false;
        }
        if((op.kind == RegOpKind::Copy || op.kind == RegOpKind::Append) &&
           static_cast<std::size_t>(op.rhs) >= registers.size()) {
            return false;
        }
    }
    applyOpsUnchecked(model, slice, registers, curPos, unsetValue);
    return true;
}

template <std::size_t MaxStates,
          std::size_t MaxCharRanges,
          std::size_t MaxRegs,
          std::size_t MaxOps,
          std::size_t MaxTags,
          typename RegisterStorage>
[[nodiscard]] constexpr auto
    applyOpsFast(const TDFAModel<MaxStates, MaxCharRanges, MaxRegs, MaxOps, MaxTags>& model,
                 OpSlice slice,
                 RegisterStorage& registers,
                 int curPos,
                 int unsetValue) -> bool {
    if(static_cast<std::size_t>(slice.off) + static_cast<std::size_t>(slice.len) >
       model.opPoolIdx) {
        return false;
    }
    applyOpsUnchecked(model, slice, registers, curPos, unsetValue);
    return true;
}

template <std::size_t MaxStates,
          std::size_t MaxCharRanges,
          std::size_t MaxRegs,
          std::size_t MaxOps,
          std::size_t MaxTags>
[[nodiscard]] constexpr auto
    lengthMightMatch(const TDFAModel<MaxStates, MaxCharRanges, MaxRegs, MaxOps, MaxTags>& model,
                     std::size_t inputLen) -> bool {
    if(inputLen < model.minInputLen) {
        return false;
    }
    if(model.maxInputLen != std::numeric_limits<uint32_t>::max() && inputLen > model.maxInputLen) {
        return false;
    }
    return true;
}

template <std::size_t MaxStates,
          std::size_t MaxCharRanges,
          std::size_t MaxRegs,
          std::size_t MaxOps,
          std::size_t MaxTags>
class Runtime {
    using ModelTy = TDFAModel<MaxStates, MaxCharRanges, MaxRegs, MaxOps, MaxTags>;

public:
    explicit Runtime(const ModelTy& model, int unsetValue = TNFA::offset_unset) :
        model_(model), unsetValue_(unsetValue) {}

    [[nodiscard]] auto reset() -> bool {
        pos_ = 0;
        alive_ = false;
        finalized_ = false;
        finalResult_.reset();

        if(model_.stateIdx == 0 || model_.startState >= model_.stateIdx) {
            return false;
        }

        state_ = model_.startState;
        const auto regSize = static_cast<std::size_t>(model_.regCount) + 1;
        if(registers_.size() != regSize) {
            registers_.resize(regSize);
        }
        std::fill(registers_.begin(), registers_.end(), unsetValue_);
        alive_ = true;
        return true;
    }

    [[nodiscard]] auto step(char ch) -> bool {
        if(!alive_ || finalized_) {
            return false;
        }

        const auto byte = static_cast<unsigned char>(ch);
        const auto next = model_.byteDelta[state_][byte];
        if(next == ModelTy::deadState) {
            alive_ = false;
            return false;
        }

        if(model_.opPoolIdx != 0) {
            const auto classIdPlusOne = model_.classByBytePlusOne[byte];
            if(classIdPlusOne == 0) {
                alive_ = false;
                return false;
            }
            const auto classId = static_cast<ClassId>(classIdPlusOne - 1);
            const auto& edge = model_.delta[state_][classId];
            if(!applyOpsFast(model_, edge.ops, registers_, static_cast<int>(pos_ + 1), unsetValue_)) {
                alive_ = false;
                return false;
            }
        }

        state_ = next;
        ++pos_;
        return true;
    }

    [[nodiscard]] auto finish() -> std::optional<std::vector<int>> {
        if(finalized_) {
            return finalResult_;
        }
        if(!alive_) {
            return std::nullopt;
        }
        if(state_ >= model_.stateIdx || !model_.isFinal[state_]) {
            return std::nullopt;
        }
        if(pos_ >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            alive_ = false;
            return std::nullopt;
        }
        const auto finalBoundary = static_cast<int>(pos_ + 1);

        if(!applyOpsFast(model_, model_.finalOps[state_], registers_, finalBoundary, unsetValue_)) {
            alive_ = false;
            return std::nullopt;
        }

        std::vector<int> out(model_.tagCount, unsetValue_);
        for(std::size_t t = 1; t <= model_.tagCount; ++t) {
            const auto reg = model_.finalRegOfTag[t];
            if(reg != 0 && static_cast<std::size_t>(reg) < registers_.size()) {
                out[t - 1] = registers_[reg];
            }
        }

        finalResult_ = std::move(out);
        finalized_ = true;
        return finalResult_;
    }

    [[nodiscard]] auto run(std::string_view input) -> std::optional<std::vector<int>> {
        if(!reset()) {
            return std::nullopt;
        }
        if(!lengthMightMatch(model_, input.size())) {
            return std::nullopt;
        }
        if(model_.isExactLiteral) {
            if(input.size() != model_.literalLen ||
               std::char_traits<char>::compare(input.data(),
                                               model_.literalBytes.data(),
                                               input.size()) != 0) {
                return std::nullopt;
            }
            state_ = model_.startState;
            for(const auto ch: input) {
                state_ = model_.byteDelta[state_][static_cast<unsigned char>(ch)];
            }
            pos_ = input.size();
            return finish();
        }
        for(const auto ch: input) {
            if(!step(ch)) {
                return std::nullopt;
            }
        }
        return finish();
    }

    [[nodiscard]] auto state() const -> StateTy {
        return state_;
    }

    [[nodiscard]] auto position() const -> std::size_t {
        return pos_;
    }

    [[nodiscard]] auto alive() const -> bool {
        return alive_;
    }

    [[nodiscard]] auto registers() const -> const std::vector<int>& {
        return registers_;
    }

private:
    const ModelTy& model_;
    int unsetValue_ = TNFA::offset_unset;
    StateTy state_ = 0;
    std::size_t pos_ = 0;
    bool alive_ = false;
    bool finalized_ = false;
    std::vector<int> registers_{};
    std::optional<std::vector<int>> finalResult_{};
};

template <std::size_t MaxStates,
          std::size_t MaxCharRanges,
          std::size_t MaxRegs,
          std::size_t MaxOps,
          std::size_t MaxTags>
[[nodiscard]] auto
    isMatch(const TDFAModel<MaxStates, MaxCharRanges, MaxRegs, MaxOps, MaxTags>& model,
            std::string_view input,
            int unsetValue = TNFA::offset_unset) -> bool {
    if(model.stateIdx == 0 || model.startState >= model.stateIdx) {
        return false;
    }
    if(!lengthMightMatch(model, input.size())) {
        return false;
    }
    if(model.isExactLiteral) {
        if(input.size() != model.literalLen) {
            return false;
        }
        return std::char_traits<char>::compare(input.data(), model.literalBytes.data(), input.size()) ==
               0;
    }

    StateTy state = model.startState;

    if(model.opPoolIdx == 0) {
        for(const auto ch: input) {
            const auto next = model.byteDelta[state][static_cast<unsigned char>(ch)];
            if(next == TDFAModel<MaxStates, MaxCharRanges, MaxRegs, MaxOps, MaxTags>::deadState) {
                return false;
            }
            state = next;
        }
        return model.isFinal[state];
    }

    std::array<int, MaxRegs + 1> registers{};
    const auto regSpan = static_cast<std::size_t>(model.regCount) + 1;
    std::fill_n(registers.begin(), regSpan, unsetValue);
    std::size_t pos = 0;

    for(const auto ch: input) {
        const auto byte = static_cast<unsigned char>(ch);
        const auto next = model.byteDelta[state][byte];
        if(next == TDFAModel<MaxStates, MaxCharRanges, MaxRegs, MaxOps, MaxTags>::deadState) {
            return false;
        }
        const auto classIdPlusOne = model.classByBytePlusOne[byte];
        if(classIdPlusOne == 0) {
            return false;
        }
        const auto classId = static_cast<ClassId>(classIdPlusOne - 1);
        const auto& edge = model.delta[state][classId];
        if(!applyOpsFast(model, edge.ops, registers, static_cast<int>(pos + 1), unsetValue)) {
            return false;
        }
        state = next;
        ++pos;
    }

    if(state >= model.stateIdx || !model.isFinal[state]) {
        return false;
    }
    if(pos >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    const auto finalBoundary = static_cast<int>(pos + 1);
    return applyOpsFast(model, model.finalOps[state], registers, finalBoundary, unsetValue);
}

template <std::size_t MaxStates,
          std::size_t MaxCharRanges,
          std::size_t MaxRegs,
          std::size_t MaxOps,
          std::size_t MaxTags>
[[nodiscard]] auto
    simulation(const TDFAModel<MaxStates, MaxCharRanges, MaxRegs, MaxOps, MaxTags>& model,
               std::string_view input,
               int unsetValue = TNFA::offset_unset) -> std::optional<std::vector<int>> {
    if(!lengthMightMatch(model, input.size())) {
        return std::nullopt;
    }

    if(model.tagCount == 0) {
        if(!isMatch(model, input, unsetValue)) {
            return std::nullopt;
        }
        return std::vector<int>{};
    }

    if(model.stateIdx == 0 || model.startState >= model.stateIdx ||
       model.regCount >= static_cast<uint16_t>(MaxRegs + 1)) {
        return std::nullopt;
    }

    std::array<int, MaxRegs + 1> registers{};
    const auto regSpan = static_cast<std::size_t>(model.regCount) + 1;
    std::fill_n(registers.begin(), regSpan, unsetValue);

    StateTy state = model.startState;
    std::size_t pos = 0;
    for(const auto ch: input) {
        const auto byte = static_cast<unsigned char>(ch);
        const auto next = model.byteDelta[state][byte];
        if(next == TDFAModel<MaxStates, MaxCharRanges, MaxRegs, MaxOps, MaxTags>::deadState) {
            return std::nullopt;
        }
        const auto classIdPlusOne = model.classByBytePlusOne[byte];
        if(classIdPlusOne == 0) {
            return std::nullopt;
        }
        const auto classId = static_cast<ClassId>(classIdPlusOne - 1);
        const auto& edge = model.delta[state][classId];
        if(!applyOpsFast(model, edge.ops, registers, static_cast<int>(pos + 1), unsetValue)) {
            return std::nullopt;
        }
        state = next;
        ++pos;
    }

    if(state >= model.stateIdx || !model.isFinal[state]) {
        return std::nullopt;
    }
    if(pos >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    const auto finalBoundary = static_cast<int>(pos + 1);
    if(!applyOpsFast(model, model.finalOps[state], registers, finalBoundary, unsetValue)) {
        return std::nullopt;
    }

    std::vector<int> out(model.tagCount, unsetValue);
    for(std::size_t t = 1; t <= model.tagCount; ++t) {
        const auto reg = model.finalRegOfTag[t];
        if(reg != 0 && static_cast<std::size_t>(reg) < registers.size()) {
            out[t - 1] = registers[reg];
        }
    }
    return out;
}

}  // namespace etch::TDFA
