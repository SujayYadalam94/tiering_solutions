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

constexpr const char *kHelperLibraryPrefixes[] = {
    "libc.so",
    "libstdc++.so",
    "libpthread.so",
    "libnuma.so",
    "libgomp.so",
    "libomp.so",
    "libgcc_s.so",
    "ld-linux",
    "ld-musl",
};

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

static std::string path_basename(const std::string &path)
{
    const size_t last_slash = path.find_last_of('/');
    return (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);
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

static bool is_helper_library_path(const std::string &normalized_path, const std::string &preload_library_path)
{
    if (normalized_path.empty() || normalized_path == preload_library_path)
    {
        return false;
    }

    const std::string basename = path_basename(normalized_path);
    for (const char *prefix : kHelperLibraryPrefixes)
    {
        const size_t prefix_len = strlen(prefix);
        if (basename.size() >= prefix_len && basename.compare(0, prefix_len, prefix) == 0)
        {
            return true;
        }
    }

    return false;
}

static std::vector<struct ip_range> collect_helper_library_text_ranges(const std::string &preload_library_path)
{
    std::vector<struct ip_range> ranges;

    std::ifstream maps_file("/proc/self/maps");
    if (!maps_file.is_open())
    {
        return ranges;
    }

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

        const int fields = sscanf(line.c_str(), "%llx-%llx %4s %llx %15s %lu %4095[^\n]", &start, &end, perms,
                                  &offset, dev, &inode, mapped_path);
        if (fields < 7 || strchr(perms, 'x') == nullptr)
        {
            continue;
        }

        const std::string normalized_path = normalize_maps_path(mapped_path);
        if (!is_helper_library_path(normalized_path, preload_library_path))
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
        set_helper_library_ip_ranges(nullptr, 0);
        return;
    }

    const std::string preload_library_path(info.dli_fname);
    std::vector<struct ip_range> ranges = collect_preload_text_ranges(preload_library_path);
    std::vector<struct ip_range> helper_ranges = collect_helper_library_text_ranges(preload_library_path);

    set_preload_ip_ranges(preload_library_path.c_str(), ranges.data(), ranges.size());
    set_helper_library_ip_ranges(helper_ranges.data(), helper_ranges.size());

    if (ranges.empty())
    {
        std::cerr << "[ARMS] Warning: found no executable mappings for preload library '" << preload_library_path
                  << "'; preload IP filtering disabled." << std::endl;
    }

    if (helper_ranges.empty())
    {
        std::cerr << "[ARMS] Warning: found no executable mappings for helper libraries; helper IP filtering disabled."
                  << std::endl;
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

#if defined(ALL_NUMA_MEMORY_DEFAULT) && ALL_NUMA_MEMORY_DEFAULT
    // Preserve the runner's all-node policy until arms_start_tiering installs
    // an explicit mask containing both the fast and slow tiers.
#else
    if (NEAR_MEM_TRACING_RUN && !FORCE_FAR_MEMORY_DEFAULT)
    {
        set_application_thread_near_memory_preferred();
    }
    else if (LOGGING_RUN)
    {
        set_application_thread_near_memory_default();
    }
    else
    {
        // Keep application allocations in far memory by default.
        set_application_thread_far_memory_default();
    }
#endif

    // start tiering threads
    arms_start_tiering();

    // Call the original function
    return original_libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end);
}
