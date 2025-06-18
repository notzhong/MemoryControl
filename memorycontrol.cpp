#include <iostream>

#include "memorycontrol.hpp"

using namespace std;

std::map<void *, memory_info> memorycontrol::m_mc;
memorycontrol *memorycontrol::m_instance = nullptr;

memorycontrol *memorycontrol::getInstance()
{
    if (!m_instance)
        m_instance = new memorycontrol;
    return m_instance;
}

memorycontrol::memorycontrol(/* args */)
{
}

memorycontrol::~memorycontrol()
{
    cout << __func__ << endl;
    if (memorycontrol::m_instance)
    {
        if (!memorycontrol::m_mc.empty())
        {
            print_no_release();
        }
        else
        {
            cout << "memory normal." << endl;
        }
    }
}

int memorycontrol::delete_memory(void *p)
{
    auto it = memorycontrol::m_mc.find(p);
    if (it != memorycontrol::m_mc.end())
    {
        auto p_char = reinterpret_cast<char *>(it->first);
        delete[] p_char;
        p_char = nullptr;
        memory_info info = it->second;
        if (memorycontrol::m_mc.erase(p))
        {
            std::cout << "release memory: 0x" << std::hex << p
                      << ", file:" << info.flie_name
                      << ", line:" << std::dec << info.line
                      << ", size:" << info.nSize
                      << " succeed!" << std::endl;
        }
        else
        {
            std::cout << "release memory: 0x" << std::hex << p
                      << ", file:" << info.flie_name
                      << ", line:" << std::dec << info.line
                      << ", size:" << info.nSize
                      << " failed !" << std::endl;
            return 2;
        }
    }
    else
    {
        std::cout << "not find pointer: 0x" << std::hex << p << std::endl;
        return 1;
    }

    return 0;
}

void memorycontrol::print_no_release()
{
    for (auto &it : memorycontrol::m_mc)
    {
        auto info = it.second;
        cout << hex << "0x" << it.first << " no release. "
             << dec << " file:" << info.flie_name
             << ". line:" << info.line
             << ". memory size:" << info.nSize
             << endl;
    }
}
