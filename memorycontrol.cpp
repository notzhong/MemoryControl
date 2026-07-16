#include <iostream>

#include "memorycontrol.hpp"

using namespace std;

std::map<void *, memory_info> memorycontrol::m_mc;
std::mutex memorycontrol::m_mtx;

memorycontrol *memorycontrol::getInstance()
{
    static memorycontrol instance;
    return &instance;
}

memorycontrol::~memorycontrol()
{
    lock_guard<mutex> lock(m_mtx);
    cout << __func__ << endl;
    if (!m_mc.empty())
    {
        // Print summary first
        size_t total_leaks  = m_mc.size();
        size_t total_bytes  = 0;
        for (auto &it : m_mc)
            total_bytes += it.second.nSize;

        cout << "=== LEAK SUMMARY: " << total_leaks << " unfreed block(s), "
             << total_bytes << " byte(s) total ===" << endl;

        print_no_release();
    }
    else
    {
        cout << "memory normal." << endl;
    }
}

int memorycontrol::delete_memory(void *p)
{
    if (!p)
        return 1;

    lock_guard<mutex> lock(m_mtx);

    auto it = memorycontrol::m_mc.find(p);
    if (it == memorycontrol::m_mc.end())
    {
        cout << "not find pointer: " << p << endl;
        return 1;
    }

    memory_info info = it->second;

    /* Free using the correct deallocator for the allocation method used.
     *
     * NOTE for NewSingle: since we don't know T at free time,
     * T's destructor is NOT called.  Only use NEW_SINGLE for types
     * with trivial destructors, or call p->~T() manually beforehand. */
    switch (info.alloc_type)
    {
    case AllocType::NewSingle:
        ::operator delete(it->first);
        break;
    case AllocType::NewArray:
        delete[] reinterpret_cast<char *>(it->first);
        break;
    case AllocType::Calloc:
        free(it->first);
        break;
    }

    if (memorycontrol::m_mc.erase(p))
    {
        cout << "release memory: " << p
             << ", file:" << info.file_name
             << ", line:" << info.line
             << ", size:" << info.nSize
             << " succeed!" << endl;
        return 0;
    }
    else
    {
        cout << "release memory: " << p
             << ", file:" << info.file_name
             << ", line:" << info.line
             << ", size:" << info.nSize
             << " failed !" << endl;
        return 2;
    }
}

void memorycontrol::print_no_release()
{
    for (auto &it : memorycontrol::m_mc)
    {
        auto info = it.second;
        cout << it.first << " no release. "
             << " file:" << info.file_name
             << ". line:" << info.line
             << ". memory size:" << info.nSize
             << endl;
    }
}