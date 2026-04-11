#include <dlfcn.h>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <vector>

#include "../arms_kernel.h"

// Type definition for the original __libc_start_main function
typedef int (*original_libc_start_main_t)(int (*)(int, char **, char **), int, char **, int (*)(int, char **, char **),
                                          void (*)(), void (*)(), void *);

namespace
{

static std::string normalize_maps_path(const char *raw_path)
{
    if (raw_path == nullptr)
    {
        return "";
    }

    std::string path(raw_path);
    const size_t first_non_space = path.find_first_not_of(' ');
    if (first_non_space == std::string::npos)
    {
        return "";
    }
    path.erase(0, first_non_space);

    constexpr const char *deleted_suffix = " (deleted)";
    const size_t deleted_suffix_len = strlen(deleted_suffix);
    if (path.size() > deleted_suffix_len &&
        path.compare(path.size() - deleted_suffix_len, deleted_suffix_len, deleted_suffix) == 0)
    {
        path.erase(path.size() - deleted_suffix_len);
    }

    return path;
}

static bool path_matches_target(const std::string &normalized_path, const std::string &target_path,
                                const std::string &target_basename)
{
    if (normalized_path == target_path)
    {
        return true;
    }

    if (!target_basename.empty())
    {
        if (normalized_path == target_basename)
        {
            return true;
        }

        if (normalized_path.size() > target_basename.size() &&
            normalized_path.compare(normalized_path.size() - target_basename.size(), target_basename.size(),
                                    target_basename) == 0 &&
            normalized_path[normalized_path.size() - target_basename.size() - 1] == '/')
        {
            return true;
        }
    }

    return false;
}

static std::vector<struct ip_range> collect_preload_text_ranges(const std::string &target_path)
{
    std::vector<struct ip_range> ranges;

    std::ifstream maps_file("/proc/self/maps");
    if (!maps_file.is_open())
    {
        return ranges;
    }

    const size_t last_slash = target_path.find_last_of('/');
    const std::string target_basename =
        (last_slash == std::string::npos) ? target_path : target_path.substr(last_slash + 1);

    std::string line;
    while (std::getline(maps_file, line))
    {
        unsigned long long start = 0;
        unsigned long long end = 0;
        unsigned long long offset = 0;
        char perms[5] = {0};
        char dev[16] = {0};
        unsigned long inode = 0;
        char mapped_path[4096] = {0};

        const int fields = sscanf(line.c_str(), "%llx-%llx %4s %llx %15s %lu %4095[^\n]", &start, &end, perms, &offset,
                                  dev, &inode, mapped_path);
        if (fields < 6)
        {
            continue;
        }

        if (strchr(perms, 'x') == nullptr)
        {
            continue;
        }

        if (fields < 7)
        {
            continue;
        }

        const std::string normalized_path = normalize_maps_path(mapped_path);
        if (!path_matches_target(normalized_path, target_path, target_basename))
        {
            continue;
        }

        if (end > start)
        {
            ranges.push_back({static_cast<uint64_t>(start), static_cast<uint64_t>(end)});
        }
    }

    return ranges;
}

static void configure_preload_ip_filter()
{
    Dl_info info{};
    if (dladdr(reinterpret_cast<void *>(&arms_start_tiering), &info) == 0 || info.dli_fname == nullptr)
    {
        std::cerr << "[ARMS] Warning: unable to resolve preload library path; preload IP filtering disabled."
                  << std::endl;
        set_preload_ip_ranges(nullptr, nullptr, 0);
        return;
    }

    const std::string preload_library_path(info.dli_fname);
    std::vector<struct ip_range> ranges = collect_preload_text_ranges(preload_library_path);

    set_preload_ip_ranges(preload_library_path.c_str(), ranges.data(), ranges.size());

    if (ranges.empty())
    {
        std::cerr << "[ARMS] Warning: found no executable mappings for preload library '" << preload_library_path
                  << "'; preload IP filtering disabled." << std::endl;
    }
}

} // namespace

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
    configure_preload_ip_filter();

    if (LOGGING_RUN)
    {
        set_application_thread_near_memory_default();
    }
    else
    {
        // Keep application allocations in far memory by default.
        set_application_thread_far_memory_default();
    }

    // start tiering threads
    arms_start_tiering();

    // Call the original function
    return original_libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end);
}
