#include "logging_parquet.h"
#include "logging.h"

#if PRINT_TRAINING_DATA
#include <arrow/api.h>
#include <arrow/io/file.h>
#include <arrow/util/thread_pool.h>
#include <parquet/arrow/writer.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <functional>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <unistd.h>
#include <vector>

namespace
{
constexpr int64_t ROW_GROUP_ROWS = 64 * 1024;
constexpr int WRITER_THREADS = 10;

void check_status(const arrow::Status &status)
{
    if (!status.ok())
        throw std::runtime_error(status.ToString());
}

template <typename T> T unwrap(arrow::Result<T> result)
{
    check_status(result.status());
    return std::move(result).ValueOrDie();
}

struct Column
{
    std::shared_ptr<arrow::Field> field;
    std::function<std::shared_ptr<arrow::Array>(const data_row *, int64_t)> build;
};

template <typename Getter>
void add_column(std::vector<Column> &columns, const std::string &name, Getter get)
{
    using T = std::decay_t<decltype(get(std::declval<const data_row &>()))>;
    // Preserve native float precision and the full range of unsigned counters.
    // Booleans remain numeric 0/1, as in the CSV-based training pipeline.
    using Builder = std::conditional_t<std::is_floating_point_v<T>, arrow::FloatBuilder,
                    std::conditional_t<std::is_unsigned_v<T> && !std::is_same_v<T, bool>,
                                       arrow::UInt64Builder, arrow::Int64Builder>>;
    Builder prototype;
    columns.push_back({arrow::field(name, prototype.type(), false),
                       [get](const data_row *rows, int64_t count) {
                           Builder builder;
                           check_status(builder.Reserve(count));
                           for (int64_t i = 0; i < count; ++i)
                               builder.UnsafeAppend(get(rows[i]));
                           return unwrap(builder.Finish());
                       }});
}

void add_group_columns(std::vector<Column> &columns, float (data_row::*member)[15], const char *suffix)
{
    for (int i = 0; i < 15; ++i)
        add_column(columns, "group_" + std::to_string(i - 7) + "_" + suffix,
                   [member, i](const data_row &row) { return (row.*member)[i]; });
}

std::vector<Column> log_columns()
{
    std::vector<Column> columns;
#define ADD_COLUMN(var) add_column(columns, #var, [](const data_row &row) { return row.var; })
    // Base fields (always present)
    ADD_COLUMN(step);
    ADD_COLUMN(page);
    ADD_COLUMN(read);
    ADD_COLUMN(write);
    ADD_COLUMN(count);
    ADD_COLUMN(global_avg_accesses);
    ADD_COLUMN(global_avg_accesses_model);
    ADD_COLUMN(ewma_2);
    ADD_COLUMN(ewma_2_r);
    ADD_COLUMN(ewma_2_w);
    ADD_COLUMN(ewma_5);
    ADD_COLUMN(ewma_5_r);
    ADD_COLUMN(ewma_5_w);
    ADD_COLUMN(ewma_20);
    ADD_COLUMN(ewma_20_r);
    ADD_COLUMN(ewma_20_w);
    ADD_COLUMN(ewma_100);
    ADD_COLUMN(ewma_100_r);
    ADD_COLUMN(ewma_100_w);
    ADD_COLUMN(virtual_missed_ewma_100);
    ADD_COLUMN(gap4);
    ADD_COLUMN(read_write_gap3);
    ADD_COLUMN(ewma_var_2);
    ADD_COLUMN(ewma_var_5);
    ADD_COLUMN(ewma_var_20);
    ADD_COLUMN(ewma_var_100);

#if FULL_LOGS
    ADD_COLUMN(global_count_since_top1_percent_ewma5);
    ADD_COLUMN(global_count_since_top50_percent_ewma5);
#endif

    // Group EWMA5 percentages are always present
    add_group_columns(columns, &data_row::group_ewma5, "mean_ewma5");
    add_group_columns(columns, &data_row::groups, "mean");

    ADD_COLUMN(group_ewma5_var);

    ADD_COLUMN(age_count_total);

#if FULL_LOGS
    ADD_COLUMN(prot);
    ADD_COLUMN(flags);

    ADD_COLUMN(ewma_2_abs);
    ADD_COLUMN(ewma_2_r_abs);
    ADD_COLUMN(ewma_2_w_abs);

    ADD_COLUMN(ewma_5_abs);
    ADD_COLUMN(ewma_5_r_abs);
    ADD_COLUMN(ewma_5_w_abs);

    ADD_COLUMN(ewma_20_abs);
    ADD_COLUMN(ewma_20_r_abs);
    ADD_COLUMN(ewma_20_w_abs);

    ADD_COLUMN(ewma_100_abs);
    ADD_COLUMN(ewma_100_r_abs);
    ADD_COLUMN(ewma_100_w_abs);

    ADD_COLUMN(rank);
    ADD_COLUMN(rank_perc);
    ADD_COLUMN(rank_ewma_2);
    ADD_COLUMN(rank_ewma_5);
    ADD_COLUMN(rank_ewma_20);
    ADD_COLUMN(rank_ewma_100);

    ADD_COLUMN(count_total);
    ADD_COLUMN(global_count_similar);
    ADD_COLUMN(diff);
    ADD_COLUMN(cpu_usage);
    ADD_COLUMN(disk_read_bytes);
    ADD_COLUMN(disk_write_bytes);
    ADD_COLUMN(syscr);
    ADD_COLUMN(syscw);

    add_group_columns(columns, &data_row::groups_abs, "mean_abs");
    add_group_columns(columns, &data_row::group_ewma5_abs, "mean_ewma5_abs");

    ADD_COLUMN(model_selection);
    ADD_COLUMN(read_bytes);
    ADD_COLUMN(write_bytes);
#endif

    ADD_COLUMN(model_score);
    ADD_COLUMN(arms_score);
    ADD_COLUMN(score);
    ADD_COLUMN(in_dram);

    ADD_COLUMN(age);

    ADD_COLUMN(num_demotions);
    ADD_COLUMN(num_promotions);

    ADD_COLUMN(discounted_reward_90);
    ADD_COLUMN(discounted_reward_95);
    ADD_COLUMN(discounted_reward_99);
    ADD_COLUMN(step_unc_m_cas_count_wr);


#undef ADD_COLUMN
    return columns;
}
} // namespace

void write_training_parquet(const std::string &path, const data_row *rows, size_t count)
{
    const auto columns = log_columns();
    std::vector<std::shared_ptr<arrow::Field>> fields;
    for (const auto &column : columns)
        fields.push_back(column.field);
    const auto schema = arrow::schema(fields);

    // Only publish a completed file. A failed write must not replace a good log
    // or leave a partial Parquet file looking like a successful run.
    // A private temporary directory lets open(0666) honor the process umask,
    // without changing the process-wide umask. In particular, sudo-launched
    // workloads must not produce root-only 0600 logs (mkstemp's default).
    std::string temporary_dir = path + ".tmp.XXXXXX";
    if (mkdtemp(temporary_dir.data()) == nullptr)
        throw std::system_error(errno, std::generic_category(), "create Parquet temporary directory");
    const std::string temporary = temporary_dir + "/data.parquet";
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd < 0)
    {
        const int error = errno;
        rmdir(temporary_dir.c_str());
        throw std::system_error(error, std::generic_category(), "create Parquet temporary file");
    }
    auto opened = arrow::io::FileOutputStream::Open(fd);
    if (!opened.ok())
    {
        close(fd);
        unlink(temporary.c_str());
        rmdir(temporary_dir.c_str());
        check_status(opened.status());
    }
    try
    {
        auto output = unwrap(std::move(opened));
        auto properties = parquet::WriterProperties::Builder()
                              .compression(arrow::Compression::SNAPPY)
                              ->max_row_group_length(ROW_GROUP_ROWS)
                              ->build();
        // A dedicated bounded pool avoids changing Arrow's process-wide pool.
        // Declare it before the writer so it outlives all encoding tasks.
        auto executor = unwrap(arrow::internal::ThreadPool::Make(WRITER_THREADS));
        auto arrow_properties = parquet::ArrowWriterProperties::Builder()
                                    .store_schema()
                                    ->set_use_threads(true)
                                    ->set_executor(executor.get())
                                    ->build();
        auto writer = unwrap(parquet::arrow::FileWriter::Open(
            *schema, arrow::default_memory_pool(), output, properties, arrow_properties));

        for (size_t begin = 0; begin < count; begin += ROW_GROUP_ROWS)
        {
            const int64_t batch_size = std::min<size_t>(ROW_GROUP_ROWS, count - begin);
            std::vector<std::shared_ptr<arrow::Array>> arrays;
            arrays.reserve(columns.size());
            for (const auto &column : columns)
                arrays.push_back(column.build(rows + begin, batch_size));
            // Parallel column encoding is supported by the buffered batch API,
            // not WriteTable. Row groups remain bounded by ROW_GROUP_ROWS.
            auto batch = arrow::RecordBatch::Make(schema, batch_size, arrays);
            check_status(writer->WriteRecordBatch(*batch));
        }
        // Closing also writes the schema/footer for an empty (zero-row) log.
        check_status(writer->Close());
        check_status(output->Close());
        std::filesystem::rename(temporary, path);
        rmdir(temporary_dir.c_str());
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        std::filesystem::remove(temporary_dir, ignored);
        throw;
    }
}
#endif // PRINT_TRAINING_DATA
