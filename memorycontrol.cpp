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
        print_no_release();
    }
    else
    {
        cout << "memory normal." << endl;
    }
}

int memorycontrol::delete_memory(void *p)
{
    lock_guard<mutex> lock(m_mtx);

    auto it = memorycontrol::m_mc.find(p);
    if (it == memorycontrol::m_mc.end())
    {
        cout << "not find pointer: 0x" << hex << p << dec << endl;
        return 1;
    }

    memory_info info = it->second;

    /* Free using the correct deallocator for the allocation method used. */
    if (info.alloc_type == AllocType::NewArray)
    {
        auto p_char = reinterpret_cast<char *>(it->first);
        delete[] p_char;
    }
    else
    {
        free(it->first);
    }

    if (memorycontrol::m_mc.erase(p))
    {
        cout << "release memory: 0x" << hex << p
             << ", file:" << info.file_name
             << ", line:" << dec << info.line
             << ", size:" << info.nSize
             << " succeed!" << endl;
        return 0;
    }
    else
    {
        cout << "release memory: 0x" << hex << p
             << ", file:" << info.file_name
             << ", line:" << dec << info.line
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
        cout << hex << "0x" << it.first << " no release. "
             << dec << " file:" << info.file_name
             << ". line:" << info.line
             << ". memory size:" << info.nSize
             << endl;
    }
}
