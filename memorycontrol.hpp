#ifndef MEMORYCONTROL_HPP
#define MEMORYCONTROL_HPP

#include <map>
#include <string>
#include <cstdlib>
#include <memory>
#include <mutex>

/**
 * @brief Allocation method type.
 *        Tracks how each block was allocated so delete_memory can free it correctly.
 */
enum class AllocType : uint8_t
{
    NewArray, ///< allocated via new char[n]
    Calloc    ///< allocated via calloc()
};

/**
 * @brief Metadata for each tracked memory allocation.
 */
struct memory_info
{
    std::string file_name; ///< source file where allocation occurred
    int line;              ///< source line where allocation occurred
    int nSize;             ///< requested allocation size in bytes
    AllocType alloc_type;  ///< which allocation method was used

    memory_info() : file_name(""), line(0), nSize(0), alloc_type(AllocType::NewArray) {}
    memory_info(const memory_info &other)
        : file_name(other.file_name), line(other.line), nSize(other.nSize), alloc_type(other.alloc_type)
    {
    }
};

/**
 * @brief Memory management / leak detection tool.
 *
 *        All allocations performed through the provided macros are tracked in a
 *        global map keyed by pointer address.  On normal program exit the
 *        singleton destructor runs automatically (C++ static-local guarantee)
 *        and prints any unreleased allocations.
 *
 *        Usage:
 * @code
 *     int *p = NEW_MEMORY(int);       // new char[] internally
 *     DELETE_MEMORY(p);
 *
 *     int *q = CALLOC_MEMORY(int);    // calloc internally
 *     DELETE_MEMORY(q);               // same macro — handles both methods
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
     *        Automatically selects delete[] or free() based on the
     *        allocation method stored at allocation time.
     *
     * @param p Pointer previously returned by NEW_MEMORY / CALLOC_MEMORY.
     * @return 0 on success;
     *         1 if pointer is not found in the tracker;
     *         2 if map erase fails.
     */
    int delete_memory(void *p);

    /**
     * @brief Print information for every still-tracked (unreleased) pointer.
     */
    void print_no_release();
};

// ── template implementations ───────────────────────────────────────────────

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
 * @brief Allocate memory for type t using operator new[] (zero-initialised).
 * @param t  The type to allocate memory for.
 */
#define NEW_MEMORY(t)     memorycontrol::getInstance()->new_memory<t>(sizeof(t), __FILE__, __LINE__)

/**
 * @brief Allocate memory for type t using calloc (zero-initialised).
 * @param t  The type to allocate memory for.
 */
#define CALLOC_MEMORY(t)  memorycontrol::getInstance()->malloc_memory<t>(sizeof(t), __FILE__, __LINE__)

/**
 * @brief Release memory previously allocated via NEW_MEMORY or CALLOC_MEMORY.
 * @param p  The pointer to release.
 */
#define DELETE_MEMORY(p)  memorycontrol::getInstance()->delete_memory(p)

#endif