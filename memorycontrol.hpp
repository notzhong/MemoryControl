#ifndef MEMORYCONTROL_HPP
#define MEMORYCONTROL_HPP

/**
 * @brief Compile-time switch to disable all memory tracking.
 *
 * Define MEMORYCONTROL_DISABLE before including this header to make all
 * macros into no-ops and remove the library footprint entirely.
 *
 * @code
 *   g++ -DMEMORYCONTROL_DISABLE ...
 * @endcode
 */
#ifdef MEMORYCONTROL_DISABLE

#include <cstddef>
#include <cstdlib>

#define NEW_SINGLE(t)         new (std::nothrow) t
#define NEW_MEMORY(t)         static_cast<t *>(::operator new(sizeof(t), std::nothrow))
#define NEW_ARRAY(t, count)   static_cast<t *>(::operator new[](sizeof(t) * (count), std::nothrow))
#define CALLOC_MEMORY(t)      static_cast<t *>(std::calloc(1, sizeof(t)))
#define CALLOC_ARRAY(t, count) static_cast<t *>(std::calloc(count, sizeof(t)))
#define DELETE_MEMORY(p)      do { ::operator delete(p); p = nullptr; } while(0)

template <typename T>
class MemoryControlPtr
{
    T *ptr_;
public:
    explicit MemoryControlPtr(T *p = nullptr) noexcept : ptr_(p) {}
    ~MemoryControlPtr() noexcept { if (ptr_) ::operator delete(ptr_); }
    T *get() const noexcept { return ptr_; }
    T *release() noexcept { T *p = ptr_; ptr_ = nullptr; return p; }
    void reset(T *p = nullptr) noexcept { if (ptr_) ::operator delete(ptr_); ptr_ = p; }
    T &operator*() const noexcept { return *ptr_; }
    T *operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    MemoryControlPtr(const MemoryControlPtr &) = delete;
    MemoryControlPtr &operator=(const MemoryControlPtr &) = delete;
    MemoryControlPtr(MemoryControlPtr &&other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    MemoryControlPtr &operator=(MemoryControlPtr &&other) noexcept
    {
        if (this != &other) { if (ptr_) ::operator delete(ptr_); ptr_ = other.ptr_; other.ptr_ = nullptr; }
        return *this;
    }
};

#else // ── Normal (tracking) mode ────────────────────────────────────────────

#include <unordered_map>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <atomic>
#include <new>
#include <vector>

/* ═══════════════════════════════════════════════════════════════════════════
 *  DLL export/import  (Windows only — other platforms are no-ops)
 * ═══════════════════════════════════════════════════════════════════════════ */
#if defined(_WIN32) || defined(_WIN64)
#  ifdef MEMORYCONTROL_EXPORTS
#    define MEMORYCONTROL_API __declspec(dllexport)
#  else
#    define MEMORYCONTROL_API __declspec(dllimport)
#  endif
#else
#  define MEMORYCONTROL_API
#endif

#ifndef MEMORYCONTROL_BACKTRACE_DEPTH
#define MEMORYCONTROL_BACKTRACE_DEPTH 8
#endif
#ifndef MEMORYCONTROL_NUM_STRIPES
#define MEMORYCONTROL_NUM_STRIPES 64
#endif

enum class AllocType : uint8_t
{
    NewSingle,
    NewArray,
    Calloc,
    Malloc
};

struct MEMORYCONTROL_API AllocRecord
{
    const char *file     = nullptr;
    int         line     = 0;
    size_t      size     = 0;
    AllocType   type     = AllocType::Malloc;
    void       *bt[MEMORYCONTROL_BACKTRACE_DEPTH]{};
    int         bt_len   = 0;
};

struct MEMORYCONTROL_API MemoryStats
{
    size_t current_bytes  = 0;
    size_t peak_bytes     = 0;
    size_t num_allocs     = 0;
    uint64_t total_allocs = 0;
    uint64_t total_frees  = 0;
};

class MEMORYCONTROL_API memorycontrol
{
private:
    struct Stripe
    {
        std::mutex                              mtx;
        std::unordered_map<void *, AllocRecord> map;
    };
    static Stripe m_stripes[MEMORYCONTROL_NUM_STRIPES];
    static thread_local bool t_in_tracking;

    bool    m_enable_backtrace     = true;
    bool    m_enable_poison        = false;
    bool    m_abort_on_double_free = true;
    size_t  m_limit_bytes          = 0;

    std::atomic<size_t>   m_current_bytes{0};
    std::atomic<size_t>   m_peak_bytes{0};
    std::atomic<size_t>   m_num_allocs{0};
    std::atomic<uint64_t> m_total_allocs{0};
    std::atomic<uint64_t> m_total_frees{0};

    memorycontrol() = default;
    ~memorycontrol();
    memorycontrol(const memorycontrol &) = delete;
    memorycontrol &operator=(const memorycontrol &) = delete;

public:
    static memorycontrol *getInstance();

    // ── Manual macros: templates ──────────────────────────────────────────
    template <class T>
    T *new_single(const char *file, int line);
    template <class T>
    T *new_memory(size_t nSize, const char *file, int line);
    template <class T>
    T *malloc_memory(size_t nSize, const char *file, int line);
    MEMORYCONTROL_API int delete_memory(void *p);

    // ── Core tracking API ─────────────────────────────────────────────────
    MEMORYCONTROL_API void track_alloc(void *p, size_t size, AllocType type,
                                        const char *file, int line,
                                        void **bt, int bt_len);
    MEMORYCONTROL_API bool untrack_and_free(void *p);

    // ── Query ─────────────────────────────────────────────────────────────
    MEMORYCONTROL_API MemoryStats get_stats() const;
    size_t current_bytes() const noexcept { return m_current_bytes.load(); }
    size_t peak_bytes()    const noexcept { return m_peak_bytes.load();    }
    size_t num_allocs()    const noexcept { return m_num_allocs.load();    }

    // ── Settings ──────────────────────────────────────────────────────────
    void set_enable_backtrace(bool on)     noexcept { m_enable_backtrace = on; }
    void set_enable_poison(bool on)        noexcept { m_enable_poison = on;    }
    void set_abort_on_double_free(bool on) noexcept { m_abort_on_double_free = on; }
    void set_limit_bytes(size_t bytes)     noexcept { m_limit_bytes = bytes;   }
    MEMORYCONTROL_API void print_stats() const;
    MEMORYCONTROL_API void print_no_release();
};

// ── Template implementations ───────────────────────────────────────────────

// forward declaration: defined in memorycontrol.cpp
extern MEMORYCONTROL_API void capture_bt(void **buf, int max_len, int *out);

template <class T>
inline T *memorycontrol::new_single(const char *file, int line)
{
    T *pMem = new (std::nothrow) T();
    if (!pMem) return nullptr;

    void *bt[MEMORYCONTROL_BACKTRACE_DEPTH]{};
    int bt_len = 0;
    capture_bt(bt, MEMORYCONTROL_BACKTRACE_DEPTH, &bt_len);

    track_alloc(pMem, sizeof(T), AllocType::NewSingle, file, line, bt, bt_len);
    return pMem;
}

template <class T>
inline T *memorycontrol::new_memory(size_t nSize,
                                    const char *file, int line)
{
    char *pMem = new (std::nothrow) char[nSize]{0};
    if (!pMem) return nullptr;

    void *bt[MEMORYCONTROL_BACKTRACE_DEPTH]{};
    int bt_len = 0;
    capture_bt(bt, MEMORYCONTROL_BACKTRACE_DEPTH, &bt_len);

    track_alloc(pMem, nSize, AllocType::NewArray, file, line, bt, bt_len);
    return reinterpret_cast<T *>(pMem);
}

template <class T>
inline T *memorycontrol::malloc_memory(size_t nSize,
                                       const char *file, int line)
{
    void *pMem = std::calloc(1, nSize);
    if (!pMem) return nullptr;

    void *bt[MEMORYCONTROL_BACKTRACE_DEPTH]{};
    int bt_len = 0;
    capture_bt(bt, MEMORYCONTROL_BACKTRACE_DEPTH, &bt_len);

    track_alloc(pMem, nSize, AllocType::Calloc, file, line, bt, bt_len);
    return reinterpret_cast<T *>(pMem);
}

// ── Macros ──────────────────────────────────────────────────────────────────
#define NEW_SINGLE(t)         memorycontrol::getInstance()->new_single<t>(__FILE__, __LINE__)
#define NEW_MEMORY(t)         memorycontrol::getInstance()->new_memory<t>(sizeof(t), __FILE__, __LINE__)
#define NEW_ARRAY(t, count)   memorycontrol::getInstance()->new_memory<t>(sizeof(t) * (count), __FILE__, __LINE__)
#define CALLOC_MEMORY(t)      memorycontrol::getInstance()->malloc_memory<t>(sizeof(t), __FILE__, __LINE__)
#define CALLOC_ARRAY(t, count) memorycontrol::getInstance()->malloc_memory<t>(sizeof(t) * (count), __FILE__, __LINE__)
#define DELETE_MEMORY(p)      do { if (p) memorycontrol::getInstance()->delete_memory(p); p = nullptr; } while(0)

// ── RAII wrapper ────────────────────────────────────────────────────────────
template <typename T>
class MEMORYCONTROL_API MemoryControlPtr
{
    T *ptr_;
public:
    explicit MemoryControlPtr(T *p = nullptr) noexcept : ptr_(p) {}
    ~MemoryControlPtr() noexcept { if (ptr_) DELETE_MEMORY(ptr_); }
    MemoryControlPtr(const MemoryControlPtr &) = delete;
    MemoryControlPtr &operator=(const MemoryControlPtr &) = delete;
    MemoryControlPtr(MemoryControlPtr &&other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    MemoryControlPtr &operator=(MemoryControlPtr &&other) noexcept
    {
        if (this != &other) { if (ptr_) DELETE_MEMORY(ptr_); ptr_ = other.ptr_; other.ptr_ = nullptr; }
        return *this;
    }
    T *get()      const noexcept { return ptr_; }
    T *release() noexcept { T *p = ptr_; ptr_ = nullptr; return p; }
    void reset(T *p = nullptr) noexcept { if (ptr_) DELETE_MEMORY(ptr_); ptr_ = p; }
    T &operator*() const noexcept { return *ptr_; }
    T *operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
};

// ── Global operator new/delete overloading ──────────────────────────────────
#ifdef MEMORYCONTROL_GLOBAL_HOOK

MEMORYCONTROL_API void *operator new(size_t size);
MEMORYCONTROL_API void *operator new(size_t size, const std::nothrow_t &) noexcept;
MEMORYCONTROL_API void *operator new[](size_t size);
MEMORYCONTROL_API void *operator new[](size_t size, const std::nothrow_t &) noexcept;
MEMORYCONTROL_API void operator delete(void *p) noexcept;
MEMORYCONTROL_API void operator delete(void *p, const std::nothrow_t &) noexcept;
MEMORYCONTROL_API void operator delete[](void *p) noexcept;
MEMORYCONTROL_API void operator delete[](void *p, const std::nothrow_t &) noexcept;
MEMORYCONTROL_API void *operator new(size_t size, std::align_val_t align);
MEMORYCONTROL_API void *operator new[](size_t size, std::align_val_t align);
MEMORYCONTROL_API void operator delete(void *p, std::align_val_t) noexcept;
MEMORYCONTROL_API void operator delete[](void *p, std::align_val_t) noexcept;

#endif // MEMORYCONTROL_GLOBAL_HOOK
#endif // !MEMORYCONTROL_DISABLE
#endif // MEMORYCONTROL_HPP