#ifndef MEMORYCONTROL_HPP
#define MEMORYCONTROL_HPP

/**
 * @brief Compile-time switch to disable all memory tracking.
 *
 * Define MEMORYCONTROL_DISABLE before including this header to make all
 * macros into no-ops.  This allows zero-overhead in release builds while
 * keeping the same code structure.
 *
 * @code
 *   // In CMakeLists.txt or compiler flags:
 *   add_definitions(-DMEMORYCONTROL_DISABLE)
 * @endcode
 */
#ifdef MEMORYCONTROL_DISABLE

// ── no-op macros ───────────────────────────────────────────────────────────
#include <cstddef>  // nullptr_t

#define NEW_MEMORY(t)         static_cast<t *>(::operator new(sizeof(t), std::nothrow))
#define NEW_SINGLE(t)         static_cast<t *>(::operator new(sizeof(t), std::nothrow))
#define NEW_ARRAY(t, count)   static_cast<t *>(::operator new(sizeof(t) * (count), std::nothrow))
#define CALLOC_MEMORY(t)      static_cast<t *>(::calloc(1, sizeof(t)))
#define CALLOC_ARRAY(t, count) static_cast<t *>(::calloc(count, sizeof(t)))
#define DELETE_MEMORY(p)      do { if (p) { ::operator delete(p); p = nullptr; } } while(0)

// No-op RAII wrapper (degenerates to raw pointer)
template <typename T>
class MemoryControlPtr
{
    T *ptr_;
public:
    explicit MemoryControlPtr(T *p = nullptr) : ptr_(p) {}
    ~MemoryControlPtr() { if (ptr_) ::operator delete(ptr_); }
    // ... (simplified; released builds should use std::unique_ptr instead)
    T *get() const { return ptr_; }
    T *release() { T *p = ptr_; ptr_ = nullptr; return p; }
    void reset(T *p = nullptr) { if (ptr_) ::operator delete(ptr_); ptr_ = p; }
    T &operator*() const { return *ptr_; }
    T *operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    MemoryControlPtr(const MemoryControlPtr &) = delete;
    MemoryControlPtr &operator=(const MemoryControlPtr &) = delete;
    MemoryControlPtr(MemoryControlPtr &&other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    MemoryControlPtr &operator=(MemoryControlPtr &&other) noexcept
    {
        if (this != &other)
        {
            if (ptr_) ::operator delete(ptr_);
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }
};

#else // ── normal (tracking) mode ────────────────────────────────────────────

#include <map>
#include <string>
#include <cstdlib>
#include <cstdint>
#include <mutex>

/**
 * @brief Allocation method type.
 *        Tracks how each block was allocated so delete_memory can free it correctly.
 */
enum class AllocType : uint8_t
{
    NewSingle, ///< allocated via new (single object)
    NewArray,  ///< allocated via new char[n]
    Calloc     ///< allocated via calloc()
};

/**
 * @brief Metadata for each tracked memory allocation.
 */
struct memory_info
{
    std::string file_name;         ///< source file where allocation occurred
    int line         = 0;          ///< source line where allocation occurred
    int nSize        = 0;          ///< requested allocation size in bytes
    AllocType alloc_type = AllocType::NewArray; ///< which allocation method was used

    memory_info() = default;
};

/**
 * @brief Memory management / leak detection tool.
 *
 *        All allocations performed through the provided macros are tracked in a
 *        global map keyed by pointer address.  On normal program exit the
 *        singleton destructor runs automatically (C++ static-local guarantee)
 *        and prints any unreleased allocations with a summary.
 *
 *        Usage:
 * @code
 *     int *p = NEW_SINGLE(int);      // new (nothrow) int
 *     DELETE_MEMORY(p);
 *
 *     int *q = CALLOC_MEMORY(int);   // calloc internally
 *     DELETE_MEMORY(q);              // same macro — handles both methods
 * @endcode
 */
class memorycontrol
{
private:
    /**
     * @brief Global map: pointer address → allocation metadata.
     */
    static std::map<void *, memory_info> m_mc;

    /**
     * @brief Mutex protecting m_mc for thread-safe access.
     */
    static std::mutex m_mtx;

    memorycontrol() = default;
    ~memorycontrol();

    // Non-copyable / non-movable
    memorycontrol(const memorycontrol &) = delete;
    memorycontrol &operator=(const memorycontrol &) = delete;

public:
    /**
     * @brief Obtain the singleton instance (created on first call).
     *
     *        Uses C++11 thread-safe static local initialisation.
     *        The instance destructor runs automatically at program exit.
     */
    static memorycontrol *getInstance();

    /**
     * @brief Allocate a single object of type T via new (nothrow).
     *
     *        Unlike new_memory, this calls T's constructor (via new T).
     *        For trivial types this is equivalent to malloc(sizeof(T)).
     *
     * @note  When freeing via DELETE_MEMORY, T's destructor is NOT called
     *        (since T's type is unknown at free time).  For non-trivial
     *        types call `p->~T()` manually BEFORE DELETE_MEMORY(p).
     *
     * @return Pointer to T, or nullptr on failure.
     */
    template <class T>
    T *new_single(std::string file, int line);

    /**
     * @brief Allocate nSize bytes via new char[] and track the allocation.
     *
     *        Returns a zero-initialised raw memory block cast to T*.
     *        For non-trivial types the constructor is NOT called —
     *        use placement new afterwards if needed.
     *
     * @param nSize Number of bytes to allocate.
     * @param file  Source file string (usually __FILE__).
     * @param line  Source line number (usually __LINE__).
     * @return Pointer cast to T*, or nullptr on failure.
     */
    template <class T>
    T *new_memory(size_t nSize, std::string file, int line);

    /**
     * @brief Allocate nSize bytes via calloc() and track the allocation.
     *
     *        Identical semantics to new_memory except the underlying
     *        allocation is performed by calloc(1, nSize) and freed
     *        with free() instead of delete[].
     *
     * @param nSize Number of bytes to allocate.
     * @param file  Source file string (usually __FILE__).
     * @param line  Source line number (usually __LINE__).
     * @return Pointer cast to T*, or nullptr on failure.
     */
    template <class T>
    T *malloc_memory(size_t nSize, std::string file, int line);

    /**
     * @brief Release a tracked pointer and remove it from the map.
     *
     *        Automatically selects delete, delete[] or free() based on the
     *        allocation method stored at allocation time.
     *
     * @param p Pointer previously returned by any allocation macro.
     * @return 0 on success;
     *         1 if pointer is not found in the tracker;
     *         2 if map erase fails.
     */
    int delete_memory(void *p);

    /**
     * @brief Print a summary and then information for every still-tracked pointer.
     */
    void print_no_release();
};

// ── template implementations ───────────────────────────────────────────────

template <class T>
inline T *memorycontrol::new_single(std::string file, int line)
{
    T *pMem = new (std::nothrow) T();
    if (!pMem)
        return nullptr;

    memory_info info;
    info.file_name  = file;
    info.line       = line;
    info.nSize      = sizeof(T);
    info.alloc_type = AllocType::NewSingle;

    {
        std::lock_guard<std::mutex> lock(m_mtx);
        memorycontrol::m_mc[pMem] = info;
    }

    return pMem;
}

template <class T>
inline T *memorycontrol::new_memory(size_t nSize,
                                    std::string file, int line)
{
    char *pMem = new (std::nothrow) char[nSize]{0};
    if (!pMem)
        return nullptr;

    memory_info info;
    info.file_name  = file;
    info.line       = line;
    info.nSize      = nSize;
    info.alloc_type = AllocType::NewArray;

    {
        std::lock_guard<std::mutex> lock(m_mtx);
        memorycontrol::m_mc[pMem] = info;
    }

    return reinterpret_cast<T *>(pMem);
}

template <class T>
inline T *memorycontrol::malloc_memory(size_t nSize,
                                       std::string file, int line)
{
    void *pMem = calloc(1, nSize);
    if (!pMem)
        return nullptr;

    memory_info info;
    info.file_name  = file;
    info.line       = line;
    info.nSize      = nSize;
    info.alloc_type = AllocType::Calloc;

    {
        std::lock_guard<std::mutex> lock(m_mtx);
        memorycontrol::m_mc[pMem] = info;
    }

    return reinterpret_cast<T *>(pMem);
}

// ── convenience macros ─────────────────────────────────────────────────────

/**
 * @brief Allocate a single object of type t using new (nothrow) — calls constructor.
 * @param t  The type to allocate.
 */
#define NEW_SINGLE(t)         memorycontrol::getInstance()->new_single<t>(__FILE__, __LINE__)

/**
 * @brief Allocate memory for one element of type t using operator new[] (zero-initialised).
 * @param t  The type to allocate memory for.
 */
#define NEW_MEMORY(t)         memorycontrol::getInstance()->new_memory<t>(sizeof(t), __FILE__, __LINE__)

/**
 * @brief Allocate an array of count elements of type t using operator new[] (zero-initialised).
 * @param t      The element type.
 * @param count  Number of elements to allocate.
 */
#define NEW_ARRAY(t, count)   memorycontrol::getInstance()->new_memory<t>(sizeof(t) * (count), __FILE__, __LINE__)

/**
 * @brief Allocate memory for one element of type t using calloc (zero-initialised).
 * @param t  The type to allocate memory for.
 */
#define CALLOC_MEMORY(t)      memorycontrol::getInstance()->malloc_memory<t>(sizeof(t), __FILE__, __LINE__)

/**
 * @brief Allocate an array of count elements of type t using calloc (zero-initialised).
 * @param t      The element type.
 * @param count  Number of elements to allocate.
 */
#define CALLOC_ARRAY(t, count) memorycontrol::getInstance()->malloc_memory<t>(sizeof(t) * (count), __FILE__, __LINE__)

/**
 * @brief Release memory previously allocated via any allocation macro.
 * @param p  The pointer to release.  Set to nullptr after free.
 */
#define DELETE_MEMORY(p)      do { memorycontrol::getInstance()->delete_memory(p); p = nullptr; } while(0)

// ── RAII wrapper (only available in tracking mode) ─────────────────────────

/**
 * @brief RAII smart pointer that automatically calls DELETE_MEMORY on destruction.
 *
 *        Movable but not copyable.  Degenerates gracefully when MEMORYCONTROL_DISABLE
 *        is defined (the no-op version uses ::operator delete directly).
 *
 * @tparam T  The pointee type.
 */
template <typename T>
class MemoryControlPtr
{
    T *ptr_;

public:
    explicit MemoryControlPtr(T *p = nullptr) noexcept : ptr_(p) {}

    ~MemoryControlPtr() noexcept
    {
        if (ptr_)
            DELETE_MEMORY(ptr_);
    }

    // Non-copyable
    MemoryControlPtr(const MemoryControlPtr &) = delete;
    MemoryControlPtr &operator=(const MemoryControlPtr &) = delete;

    // Movable
    MemoryControlPtr(MemoryControlPtr &&other) noexcept : ptr_(other.ptr_)
    {
        other.ptr_ = nullptr;
    }

    MemoryControlPtr &operator=(MemoryControlPtr &&other) noexcept
    {
        if (this != &other)
        {
            if (ptr_)
                DELETE_MEMORY(ptr_);
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    /// Accessors
    T *get() const noexcept { return ptr_; }

    T *release() noexcept
    {
        T *p = ptr_;
        ptr_ = nullptr;
        return p;
    }

    void reset(T *p = nullptr) noexcept
    {
        if (ptr_)
            DELETE_MEMORY(ptr_);
        ptr_ = p;
    }

    T &operator*() const noexcept { return *ptr_; }
    T *operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
};

#endif // MEMORYCONTROL_DISABLE

#endif // MEMORYCONTROL_HPP