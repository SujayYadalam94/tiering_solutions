#include <arrow/api.h>
#include <cstdlib>
#include <cstdio>

void use_arrow()
{
    // Initialize Arrow's lazy globals during the workload, as an Arrow-using
    // application would. They must still be alive when the log is written.
    arrow::FloatBuilder builder;
    if (!builder.Append(1.0f).ok()) std::abort();
}

int main(int argc, char **argv)
{
    if (argc < 2 || argv[1][0] != 'c') use_arrow();
    std::atexit([] { fprintf(stderr, "APPLICATION_EXIT_HANDLER\n"); });
    if (argc > 1 && argv[1][0] == 'e') std::exit(7);
    return 7;
}
