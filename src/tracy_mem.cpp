#ifdef TRACY_ENABLE

#include <cstdlib>
#include <cstddef>
#include <tracy/Tracy.hpp>

void *operator new(std::size_t count) {
    auto ptr = malloc(count);
    TracyAlloc(ptr, count);
    return ptr;
}

void operator delete(void *ptr) noexcept {
    TracyFree(ptr);
    free(ptr);
}

void *operator new[](std::size_t count) {
    void *ptr = std::malloc(count);
    if (!ptr)
        throw std::bad_alloc();
    TracyAlloc(ptr, count);
    return ptr;
}

void operator delete[](void *ptr) noexcept {
    TracyFree(ptr);
    std::free(ptr);
}

void operator delete(void *ptr, std::size_t /*size*/) noexcept {
    TracyFree(ptr);
    std::free(ptr);
}

void operator delete[](void *ptr, std::size_t /*size*/) noexcept {
    TracyFree(ptr);
    std::free(ptr);
}
#endif
