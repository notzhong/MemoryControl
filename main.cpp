#include <iostream>
#include "memorycontrol.hpp"

using namespace std;

/**
 * @brief Initialize and generate a global management memory object.
 * @brief When the program exits, destruct and print whether the memory has been released normally.
 */
void init()
{
    static memorycontrol g_instance;
}

int main(int args, char **argv)
{
    init();
    std::cout << "ok \n"
              << std::endl;

    int *p_test = NEW_MEMORY(int);

    *p_test = 123;

    std::cout << *p_test << endl;

    auto p_test2 = NEW_MEMORY(memory_info);

    DELETE_MEMORY(p_test);
    // DELETE_MEMORY(p_test2);

    return 0;
}