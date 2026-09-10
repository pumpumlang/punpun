#ifndef PPC_SUPPORT_ARENA_HPP
#define PPC_SUPPORT_ARENA_HPP

#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#include "ppc/support/common.hpp"

namespace ppc {

/// Bump-pointer arena for AST, HIR, and MIR nodes.
///
/// A compiler allocates a great many small, same-lifetime nodes and then frees
/// them all at once. malloc/free per node is a measurable share of front-end
/// time; bumping a pointer is a few instructions and gives better locality when
/// the tree is later walked in allocation order.
///
/// Types with non-trivial destructors (anything holding a std::vector or
/// std::string) get their destructor registered so `make<T>` stays safe to use
/// for every node kind rather than only POD ones.
class Arena {
  public:
    Arena() = default;
    Arena(const Arena &) = delete;
    Arena &operator=(const Arena &) = delete;

    ~Arena() { reset(); }

    void *allocate(std::size_t bytes, std::size_t align) {
        std::size_t padding = 0;
        if (current_) {
            const auto address = reinterpret_cast<std::uintptr_t>(current_);
            const auto aligned = (address + align - 1) & ~(std::uintptr_t)(align - 1);
            padding = static_cast<std::size_t>(aligned - address);
        }
        if (!current_ || bytes + padding > remaining_) {
            grow(bytes + align);
            const auto address = reinterpret_cast<std::uintptr_t>(current_);
            const auto aligned = (address + align - 1) & ~(std::uintptr_t)(align - 1);
            padding = static_cast<std::size_t>(aligned - address);
        }
        char *result = current_ + padding;
        current_ = result + bytes;
        remaining_ -= (bytes + padding);
        used_ += bytes + padding;
        return result;
    }

    template <typename T, typename... Args>
    T *make(Args &&...args) {
        void *memory = allocate(sizeof(T), alignof(T));
        T *node = new (memory) T(std::forward<Args>(args)...);
        if constexpr (!std::is_trivially_destructible_v<T>) {
            destructors_.push_back({[](void *pointer) { static_cast<T *>(pointer)->~T(); }, node});
        }
        return node;
    }

    /// Total bytes handed out. Reported by `--time-passes` so memory growth in a
    /// phase is visible without an external profiler.
    std::size_t used() const { return used_; }

    void reset() {
        // Reverse order mirrors normal C++ destruction order for nested nodes.
        for (auto it = destructors_.rbegin(); it != destructors_.rend(); ++it) {
            it->run(it->object);
        }
        destructors_.clear();
        blocks_.clear();
        current_ = nullptr;
        remaining_ = 0;
        used_ = 0;
    }

  private:
    struct Cleanup {
        void (*run)(void *);
        void *object;
    };

    void grow(std::size_t minimum) {
        std::size_t size = block_size_;
        while (size < minimum) size *= 2;
        blocks_.push_back(std::unique_ptr<char[]>(new char[size]));
        current_ = blocks_.back().get();
        remaining_ = size;
        // Blocks grow geometrically up to a cap so a large program does not end
        // up making thousands of small allocations, and a small one does not
        // reserve megabytes it never touches.
        if (block_size_ < (1u << 20)) block_size_ *= 2;
    }

    std::vector<std::unique_ptr<char[]>> blocks_;
    std::vector<Cleanup> destructors_;
    char *current_ = nullptr;
    std::size_t remaining_ = 0;
    std::size_t used_ = 0;
    std::size_t block_size_ = 16 * 1024;
};

}  // namespace ppc

#endif
