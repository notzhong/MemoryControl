#ifndef MEMORYCONTROL_HPP
#define MEMORYCONTROL_HPP

#include <map>
#include <string>
#include <cstdlib>
#include <memory>

/**
 * @breif A data structure used to store pointer information.
 */
struct memory_info
{
    std::string flie_name;
    int line;
    int nSize;

    memory_info() : flie_name(""), line(0), nSize(0) {}
    memory_info(memory_info &other)
    {
        flie_name = other.flie_name;
        line = other.line;
        nSize = other.nSize;
    }
};

/**
 * @brief Memory management tool.
 *        When using it, a global object needs to be created so that the destruction can be properly called.
 *        For example: the way of main.cpp.
 */
class memorycontrol
{
private:
    /**
     * @brief A global singleton that manages memory information.
     **/
    static std::map<void *, memory_info> m_mc;

    /**
     * @brief Global unique invocation singleton.
     */
    static memorycontrol *m_instance;

public:
    /**
     * @brief Obtain the singleton object
     */
    static memorycontrol *getInstance();

    memorycontrol(/* args */);
    ~memorycontrol();

    /**
     * @brief When this function is called to allocate memory,
     *        the memory information will be written into m_mc for management.
     *        This function allocates a memory block by allocating a char byte array,
     *        and then uses reinterpret_cast to convert it to the corresponding type.
     *        Therefore, the objects that need to be applied for must be manually initialized.
     * @param nSize The requested size of memory.
     * @param file The location of the used files.
     * @param line The line number of the file being used.
     * @return The obtained memory pointer.
     */
    template <class T>
    T *new_memory(size_t nSize, std::string file, int line);

    template <class T>
    T *malloc_memory(size_t nSize, std::string, int line);

    /***
     * @brief Release the specified pointer object.
     * @param Pointer parameter
     * @return Successfully returned 0;
     *         If no corresponding pointer is found, return 1;
     *         If release fails, return 2.
     */
    int delete_memory(void *p);

    /**
     * @brief Print the information of unreleased pointers.
     */
    void print_no_release();
};

template <class T>
inline T *memorycontrol::new_memory(size_t nSize,
                                    std::string file, int line)
{
    char *pMem = new char[nSize]{0};
    if (!pMem)
        return nullptr;

    memory_info info;
    info.flie_name = file;
    info.line = line;
    info.nSize = nSize;
    memorycontrol::m_mc[pMem] = info;
    return reinterpret_cast<T *>(pMem);
}

template <class T>
inline T *memorycontrol::malloc_memory(size_t nSize, std::string file, int line)
{
    void *pMem = calloc(nSize, sizeof(char));
    if (!pMem)
        return nullptr;
    memory_info info;
    info.flie_name = file;
    info.line = line;
    info.nSize = nSize;
    memorycontrol::m_mc[pMem] = info;
    return reinterpret_cast<T *>(pMem);
}

/**
 * @brief Macro request for memory
 * @param t The type of applicants
 */
#define NEW_MEMORY(t) memorycontrol::getInstance()->new_memory<t>(sizeof(t), __FILE__, __LINE__)

/**
 * @brief Memory Release Macro
 * @param p The pointer that needs to be released
 */
#define DELETE_MEMORY(p) memorycontrol::getInstance()->delete_memory(p)

#endif