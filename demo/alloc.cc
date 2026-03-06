#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>

struct MemPtr {
    std::size_t offset;
    std::size_t size;
};

template <bool counting, std::size_t MemSize>
struct ComptimeAllocator {
    static constexpr std::size_t invalidOffset = std::numeric_limits<std::size_t>::max();
    static constexpr std::size_t defaultAlign = alignof(std::max_align_t);

    std::size_t idx = 0;
    alignas(std::max_align_t) std::array<std::byte, counting ? 0 : MemSize> data{};

    [[nodiscard]] static constexpr auto isValid(MemPtr ptr) -> bool {
        return ptr.offset != invalidOffset;
    }

    [[nodiscard]] static constexpr auto alignUp(std::size_t value, std::size_t align)
        -> std::optional<std::size_t> {
        if(align == 0 || (align & (align - 1)) != 0) {
            return std::nullopt;
        }
        if(value > std::numeric_limits<std::size_t>::max() - (align - 1)) {
            return std::nullopt;
        }
        const auto aligned = (value + align - 1) & ~(align - 1);
        return aligned;
    }

    [[nodiscard]] constexpr auto used() const -> std::size_t {
        return idx;
    }

    [[nodiscard]] constexpr auto allocBytes(std::size_t size, std::size_t align = defaultAlign)
        -> MemPtr {
        const auto beginOpt = alignUp(idx, align);
        if(!beginOpt.has_value()) {
            return MemPtr{invalidOffset, 0};
        }
        const auto begin = *beginOpt;
        if(begin > std::numeric_limits<std::size_t>::max() - size) {
            return MemPtr{invalidOffset, 0};
        }
        const auto end = begin + size;

        if constexpr(!counting) {
            if(end > MemSize) {
                return MemPtr{invalidOffset, 0};
            }
        }

        idx = end;
        return MemPtr{begin, size};
    }

    template <class T>
    [[nodiscard]] constexpr auto allocObject() -> MemPtr {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        return allocBytes(sizeof(T), alignof(T));
    }

    template <class T>
    [[nodiscard]] constexpr auto allocArray(std::size_t count) -> MemPtr {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        if(count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            return MemPtr{invalidOffset, 0};
        }
        return allocBytes(count * sizeof(T), alignof(T));
    }

    template <class T>
    [[nodiscard]] auto get(MemPtr ptr) -> T* {
        if constexpr(counting) {
            return nullptr;
        } else {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
            if(!isValid(ptr) || ptr.size < sizeof(T)) {
                return nullptr;
            }
            if(ptr.offset > MemSize || sizeof(T) > MemSize - ptr.offset) {
                return nullptr;
            }
            auto* raw = data.data() + ptr.offset;
            if(reinterpret_cast<std::uintptr_t>(raw) % alignof(T) != 0) {
                return nullptr;
            }
            return reinterpret_cast<T*>(raw);
        }
    }

    template <class T>
    [[nodiscard]] auto get(MemPtr ptr) const -> const T* {
        if constexpr(counting) {
            return nullptr;
        } else {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
            if(!isValid(ptr) || ptr.size < sizeof(T)) {
                return nullptr;
            }
            if(ptr.offset > MemSize || sizeof(T) > MemSize - ptr.offset) {
                return nullptr;
            }
            auto* raw = data.data() + ptr.offset;
            if(reinterpret_cast<std::uintptr_t>(raw) % alignof(T) != 0) {
                return nullptr;
            }
            return reinterpret_cast<const T*>(raw);
        }
    }

    template <class T>
    [[nodiscard]] auto getArray(MemPtr ptr, std::size_t count) -> T* {
        if constexpr(counting) {
            return nullptr;
        } else {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
            if(!isValid(ptr)) {
                return nullptr;
            }
            if(count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
                return nullptr;
            }
            const auto bytes = count * sizeof(T);
            if(ptr.size != bytes) {
                return nullptr;
            }
            if(ptr.offset > MemSize || bytes > MemSize - ptr.offset) {
                return nullptr;
            }
            auto* raw = data.data() + ptr.offset;
            if(reinterpret_cast<std::uintptr_t>(raw) % alignof(T) != 0) {
                return nullptr;
            }
            return reinterpret_cast<T*>(raw);
        }
    }

    template <class T>
    [[nodiscard]] auto getArray(MemPtr ptr, std::size_t count) const -> const T* {
        if constexpr(counting) {
            return nullptr;
        } else {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
            if(!isValid(ptr)) {
                return nullptr;
            }
            if(count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
                return nullptr;
            }
            const auto bytes = count * sizeof(T);
            if(ptr.size != bytes) {
                return nullptr;
            }
            if(ptr.offset > MemSize || bytes > MemSize - ptr.offset) {
                return nullptr;
            }
            auto* raw = data.data() + ptr.offset;
            if(reinterpret_cast<std::uintptr_t>(raw) % alignof(T) != 0) {
                return nullptr;
            }
            return reinterpret_cast<const T*>(raw);
        }
    }
};

struct EdgeInfo {
    std::uint32_t to;
    std::uint32_t weight;
};

struct NodeInfo {
    std::uint32_t id;
    MemPtr edgeRefs;
};

struct GraphInfo {
    MemPtr nodeRefs;
    std::size_t nodeCount;
};

constexpr std::array<EdgeInfo, 3> kNode0 = {{{1, 7}, {2, 3}, {3, 5}}};
constexpr std::array<EdgeInfo, 2> kNode1 = {{{0, 1}, {3, 8}}};
constexpr std::array<EdgeInfo, 1> kNode2 = {{{3, 4}}};
constexpr std::array<EdgeInfo, 0> kNode3 = {};
constexpr std::array<std::span<const EdgeInfo>, 4> kSpec = {
    std::span<const EdgeInfo>(kNode0),
    std::span<const EdgeInfo>(kNode1),
    std::span<const EdgeInfo>(kNode2),
    std::span<const EdgeInfo>(kNode3),
};

template <bool counting, std::size_t MemSize, std::size_t NodeN>
[[nodiscard]] constexpr auto buildGraph(ComptimeAllocator<counting, MemSize>& alloc,
                                        const std::array<std::span<const EdgeInfo>, NodeN>& spec)
    -> std::optional<GraphInfo> {
    const auto nodeRefsPtr = alloc.template allocArray<MemPtr>(NodeN);
    if(!ComptimeAllocator<counting, MemSize>::isValid(nodeRefsPtr)) {
        return std::nullopt;
    }

    MemPtr* nodeRefs = nullptr;
    if constexpr(!counting) {
        nodeRefs = alloc.template getArray<MemPtr>(nodeRefsPtr, NodeN);
        if(nodeRefs == nullptr) {
            return std::nullopt;
        }
    }

    for(std::size_t nodeId = 0; nodeId < NodeN; ++nodeId) {
        const auto edges = spec[nodeId];
        const auto edgeRefsPtr = alloc.template allocArray<MemPtr>(edges.size());
        if(!ComptimeAllocator<counting, MemSize>::isValid(edgeRefsPtr)) {
            return std::nullopt;
        }

        MemPtr* edgeRefs = nullptr;
        if constexpr(!counting) {
            edgeRefs = alloc.template getArray<MemPtr>(edgeRefsPtr, edges.size());
            if(edgeRefs == nullptr && !edges.empty()) {
                return std::nullopt;
            }
        }

        for(std::size_t e = 0; e < edges.size(); ++e) {
            const auto edgePtr = alloc.template allocObject<EdgeInfo>();
            if(!ComptimeAllocator<counting, MemSize>::isValid(edgePtr)) {
                return std::nullopt;
            }

            if constexpr(!counting) {
                edgeRefs[e] = edgePtr;
                auto* edgeData = alloc.template get<EdgeInfo>(edgePtr);
                if(edgeData == nullptr) {
                    return std::nullopt;
                }
                *edgeData = edges[e];
            }
        }

        const auto nodePtr = alloc.template allocObject<NodeInfo>();
        if(!ComptimeAllocator<counting, MemSize>::isValid(nodePtr)) {
            return std::nullopt;
        }

        if constexpr(!counting) {
            auto* nodeData = alloc.template get<NodeInfo>(nodePtr);
            if(nodeData == nullptr) {
                return std::nullopt;
            }
            nodeData->id = static_cast<std::uint32_t>(nodeId);
            nodeData->edgeRefs = edgeRefsPtr;
            nodeRefs[nodeId] = nodePtr;
        }
    }

    return GraphInfo{nodeRefsPtr, NodeN};
}

template <std::size_t MemSize>
auto dumpAdjacency(const ComptimeAllocator<false, MemSize>& alloc, const GraphInfo& graph) -> bool {
    const auto* nodeRefs = alloc.template getArray<MemPtr>(graph.nodeRefs, graph.nodeCount);
    if(nodeRefs == nullptr) {
        return false;
    }

    for(std::size_t i = 0; i < graph.nodeCount; ++i) {
        const auto* node = alloc.template get<NodeInfo>(nodeRefs[i]);
        if(node == nullptr) {
            return false;
        }

        const auto edgeCount = node->edgeRefs.size / sizeof(MemPtr);
        const auto* edgeRefs = alloc.template getArray<MemPtr>(node->edgeRefs, edgeCount);
        if(edgeRefs == nullptr && edgeCount != 0) {
            return false;
        }

        std::cout << "node " << node->id << " edges:";
        for(std::size_t e = 0; e < edgeCount; ++e) {
            const auto* edge = alloc.template get<EdgeInfo>(edgeRefs[e]);
            if(edge == nullptr) {
                return false;
            }
            std::cout << " (" << edge->to << ", w=" << edge->weight << ")";
        }
        std::cout << '\n';
    }
    return true;
}

constexpr auto kRequiredBytes = []() constexpr {
    ComptimeAllocator<true, 0> countAlloc{};
    const auto graph = buildGraph(countAlloc, kSpec);
    if(!graph.has_value()) {
        return std::size_t{0};
    }
    return countAlloc.used();
}();

int main() {
    ComptimeAllocator<true, 0> firstPass{};
    const auto counted = buildGraph(firstPass, kSpec);
    if(!counted.has_value()) {
        std::cout << "first pass failed\n";
        return 1;
    }
    std::cout << "first pass required bytes: " << firstPass.used() << '\n';

    static_assert(kRequiredBytes > 0, "pool size must be non-zero");
    ComptimeAllocator<false, kRequiredBytes> secondPass{};
    const auto built = buildGraph(secondPass, kSpec);
    if(!built.has_value()) {
        std::cout << "second pass failed\n";
        return 1;
    }

    std::cout << "second pass pool size: " << kRequiredBytes << '\n';
    if(!dumpAdjacency(secondPass, *built)) {
        std::cout << "dump failed\n";
        return 1;
    }
    return 0;
}
