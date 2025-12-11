#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <string>
#include <unistd.h>

#include "../arms_kernel.h"

// Type definition for the original __libc_start_main function
typedef int (*original_libc_start_main_t)(int (*)(int, char **, char **), int, char **, int (*)(int, char **, char **),
                                          void (*)(), void (*)(), void *);

extern "C" int __libc_start_main(int (*main)(int, char **, char **), int argc, char **argv,
                                 int (*init)(int, char **, char **), void (*fini)(void), void (*rtld_fini)(void),
                                 void *stack_end)
{
    std::cout << "!!!!!!!!!!!!!!!!!!!!Overridden __libc_start_main called!" << std::endl;

    atexit(arms_kernel_shutdown);

    // Read the current process's name
    std::ifstream cmdline("/proc/self/cmdline");
    std::string proc_name;
    std::getline(cmdline, proc_name, '\0'); // Read up to the first null character

    // Extract just the executable name from proc_name, in case it's a full path
    std::string executable_name = proc_name.substr(proc_name.find_last_of("/") + 1);

    std::cout << "executable name " << executable_name << std::endl;

    // Get the real __libc_start_main function
    original_libc_start_main_t original_libc_start_main;
    original_libc_start_main = (original_libc_start_main_t)dlsym(RTLD_NEXT, "__libc_start_main");

    std::cout << "found process " << executable_name << std::endl;
    // start tiering threads
    arms_start_tiering();

    // Call the original function
    return original_libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end);
}
