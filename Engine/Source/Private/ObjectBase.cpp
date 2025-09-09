#include "ObjectBase.h"
#include <cstdint>
#include <mutex>
#include <vector>
#include <cassert>
#ifdef _WIN32
    #define NOMINMAX
    #include <windows.h>
#endif

// ----------------------
// Simple size-class slab allocator for QObjectBase
// Blocks are fixed-size within a slab (page). A small header is stored
// at the beginning of each block to locate its slab on deallocation.
// ----------------------
namespace {
    constexpr std::size_t kPageSize   = 64 * 1024;   // 64 KB per slab page
    constexpr std::size_t kQuantum    = 16;          // size-class granularity
    constexpr std::size_t kMaxBlock   = 4096;        // max block size managed by slabs
    constexpr uint32_t    kNpos       = 0xFFFFFFFFu;

    struct Slab;

    struct BlockHeader {
        Slab*        slab;       // owning slab
        uint16_t     classIndex; // size-class index
        uint16_t     blockIndex; // index within slab
        uint16_t     align;      // used only for oversize fallback
    };
    
    // Ensure header alignment is sufficient
    static_assert(alignof(BlockHeader) <= alignof(std::max_align_t), "Header alignment too strict");
    constexpr std::size_t kHeaderSize = ((sizeof(BlockHeader) + (kQuantum - 1)) / kQuantum) * kQuantum;

    struct Slab {
        void*        page = nullptr;   // raw page memory
        uint32_t     blockSize = 0;    // bytes per block (including header space)
        uint32_t     capacity = 0;     // number of blocks in this slab
        uint32_t     freeHead = 0;     // index of first free block (single-linked list)
        uint32_t     live = 0;         // number of allocated blocks
        uint16_t     classIndex = 0;   // bin index

        // Initialize free list inside the blocks; next index is stored at block start.
        void InitFreeList() {
            freeHead = 0;
            for (uint32_t i = 0; i < capacity; ++i) {
                char* base = static_cast<char*>(page) + i * blockSize;
                *reinterpret_cast<uint32_t*>(base) = (i + 1 < capacity) ? (i + 1) : kNpos;
            }
            live = 0;
        }
    };

    struct Bin {
        std::vector<Slab*> partial; // slabs that have at least one free block
    };

    class QSlabAllocator {
    public:
        void* Allocate(std::size_t payloadSize, std::size_t align);
        void  Deallocate(void* userPtr) noexcept;
        void  TrimEmpty() noexcept {}

        static QSlabAllocator& Instance() {
            static QSlabAllocator S;
            return S;
        }

    private:
        std::mutex mtx;
        std::vector<Bin> bins;

        static std::size_t AlignUp(std::size_t v, std::size_t a) {
            return (v + (a - 1)) & ~(a - 1);
        }

        static std::pair<std::size_t, std::size_t> ClassFor(std::size_t payload, std::size_t align) {
            std::size_t need = kHeaderSize + payload;
            std::size_t blk  = AlignUp(need, std::max<std::size_t>(align, kQuantum));
            blk = AlignUp(blk, kQuantum);
            if (blk > kMaxBlock) return { SIZE_MAX, blk };
            std::size_t idx = (blk / kQuantum) - 1;
            return { idx, blk };
        }

        Slab* NewSlab(std::size_t classIdx, std::size_t blockSize) {
        #ifdef _WIN32
            void* page = VirtualAlloc(nullptr, kPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        #else
            void* page = std::aligned_alloc(kQuantum, kPageSize);
        #endif
            if (!page) return nullptr;

            auto* s = new Slab();
            s->page = page;
            s->blockSize = static_cast<uint32_t>(blockSize);
            s->capacity  = static_cast<uint32_t>(kPageSize / blockSize);
            if (s->capacity == 0) { // safety
            #ifdef _WIN32
                VirtualFree(page, 0, MEM_RELEASE);
            #else
                std::free(page);
            #endif
                delete s;
                return nullptr;
            }
            s->classIndex = static_cast<uint16_t>(classIdx);
            s->InitFreeList();
            return s;
        }

        void FreeSlab(Slab* s) noexcept {
            if (!s) return;
        #ifdef _WIN32
            VirtualFree(s->page, 0, MEM_RELEASE);
        #else
            std::free(s->page);
        #endif
            delete s;
        }
    };

    void* QSlabAllocator::Allocate(std::size_t payloadSize, std::size_t align) {
        auto [classIdx, blockSize] = ClassFor(payloadSize, align);

        // Oversize fallback: keep alignment in header
        if (classIdx == SIZE_MAX) {
            char* raw = static_cast<char*>(::operator new(kHeaderSize + payloadSize, std::align_val_t(align)));
            auto* hdr = reinterpret_cast<BlockHeader*>(raw);
            hdr->slab = nullptr;
            hdr->classIndex = 0xFFFF;
            hdr->blockIndex = 0xFFFF;
            hdr->align = static_cast<uint16_t>(align);
            return raw + kHeaderSize;
        }

        std::lock_guard<std::mutex> lock(mtx);

        if (bins.size() <= classIdx) bins.resize(classIdx + 1);
        Bin& bin = bins[classIdx];

        // Defensive prune: ensure partial contains only slabs with free blocks
        while (!bin.partial.empty() && bin.partial.back()->freeHead == kNpos) {
            bin.partial.pop_back();
        }

        Slab* slab = nullptr;
        if (bin.partial.empty()) {
            slab = NewSlab(classIdx, blockSize);
            if (!slab) throw std::bad_alloc();
            bin.partial.push_back(slab);
        } else {
            slab = bin.partial.back(); // guaranteed to have freeHead != kNpos
        }

        // Pop a block
        uint32_t idx = slab->freeHead;
        // assert now becomes a real invariant instead of a runtime hazard
        assert(idx != kNpos && "No free block in selected slab");
        char* block = static_cast<char*>(slab->page) + idx * slab->blockSize;
        slab->freeHead = *reinterpret_cast<uint32_t*>(block); // next index
        ++slab->live;

        // If slab became full, remove from partial (do not keep full slabs)
        if (slab->freeHead == kNpos) {
            bin.partial.pop_back(); // we took back()
        }

        auto* hdr = reinterpret_cast<BlockHeader*>(block);
        hdr->slab       = slab;
        hdr->classIndex = static_cast<uint16_t>(classIdx);
        hdr->blockIndex = static_cast<uint16_t>(idx);
        hdr->align      = 0; // not used for slab blocks
        return block + kHeaderSize;
    }

    void QSlabAllocator::Deallocate(void* userPtr) noexcept {
        if (!userPtr) return;
        char* p = static_cast<char*>(userPtr);
        auto* hdr = reinterpret_cast<BlockHeader*>(p - kHeaderSize);

        // Oversize block: free with the recorded alignment
        if (hdr->slab == nullptr && hdr->classIndex == 0xFFFF) {
            ::operator delete(hdr, std::align_val_t(hdr->align));
            return;
        }

        Slab* slab = hdr->slab;
        const uint32_t idx = hdr->blockIndex;

        std::lock_guard<std::mutex> lock(mtx);

        Bin& bin = bins[slab->classIndex];

        // Was this slab full before this free?
        const bool wasFull = (slab->freeHead == kNpos);

        // Push the block back to slab free-list
        char* block = static_cast<char*>(slab->page) + idx * slab->blockSize;
        *reinterpret_cast<uint32_t*>(block) = slab->freeHead;
        slab->freeHead = idx;
        slab->live--;

        // If slab is now empty: free whole page
        if (slab->live == 0) {
            // It might or might not be in partial (full slabs are not in partial).
            // Remove if present.
            for (size_t i = 0; i < bin.partial.size(); ++i) {
                if (bin.partial[i] == slab) {
                    bin.partial[i] = bin.partial.back();
                    bin.partial.pop_back();
                    break;
                }
            }
            FreeSlab(slab);
            return;
        }

        // If it was full, it just gained a free block; add back to partial.
        if (wasFull) {
            bin.partial.push_back(slab);
        }
    }
}

// ---- QObjectBase new/delete routing ----
void* QObjectBase::operator new(std::size_t sz) {
    return QSlabAllocator::Instance().Allocate(sz, alignof(std::max_align_t));
}
void QObjectBase::operator delete(void* p) noexcept {
    QSlabAllocator::Instance().Deallocate(p);
}
void QObjectBase::operator delete(void* p, std::size_t) noexcept {
    QSlabAllocator::Instance().Deallocate(p);
}

void* QObjectBase::operator new(std::size_t sz, std::align_val_t al) {
    return QSlabAllocator::Instance().Allocate(sz, static_cast<std::size_t>(al));
}
void QObjectBase::operator delete(void* p, std::align_val_t) noexcept {
    QSlabAllocator::Instance().Deallocate(p);
}
void QObjectBase::operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    QSlabAllocator::Instance().Deallocate(p);
}
