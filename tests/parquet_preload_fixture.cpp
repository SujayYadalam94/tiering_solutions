// Exercise the real startup/shutdown hook without perf, NUMA policy changes,
// background workers, or the large training sample allocation.
#include "../hook/hook.cpp"
#include "../logging.h"
#include "../logging_parquet.h"

void arms_start_tiering()
{
    const std::string preload_path = "/tmp/preload.so";
    for (const char *path : {"/tmp/libarrow.so.2400", "/tmp/libarrow_compute.so.2400",
                             "/tmp/libarrow_python.so", "/tmp/libparquet.so.2400"})
        if (!is_helper_library_path(path, preload_path)) std::abort();
    for (const char *path : {"/tmp/app", "/tmp/libduckdb.so", "/tmp/preload.so"})
        if (is_helper_library_path(path, preload_path)) std::abort();
}
void set_application_thread_near_memory_default() {}
void set_application_thread_near_memory_preferred() {}
void set_application_thread_far_memory_default() {}
void set_preload_ip_ranges(const char *, const ip_range *, size_t) {}
void set_helper_library_ip_ranges(const ip_range *, size_t) {}

void arms_kernel_shutdown()
{
    static bool done = false;
    if (done) return;
    done = true;
    data_row row{};
    row.step = 42;
    write_training_parquet(std::getenv("LOG_OUTPUT_PATH"), &row, 1);
    fprintf(stderr, "PRELOAD_PARQUET_WRITTEN\n");
}
