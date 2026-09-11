// Exercise the real virtual-step logging and batch inference with a stub runtime.
#include "../pebs_scan_thread.cpp"
#include <thread>

std::atomic<uint64_t> virtual_step{7};
struct group_tracker *virtual_grp_tracker = nullptr;
struct access_log *access_log = nullptr;
static std::vector<data_row> logged_rows;
static size_t predictions = 0;

access_log::access_log() {}

data_row access_log::extract_row(size_t step, const page_ptr &page, struct group_tracker *, size_t)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    data_row row{};
    row.step = step;
    row.page = page->va;
    row.ewma_2 = static_cast<float>(page->va);
    return row;
}

void access_log::log_row(const page_ptr &, data_row &row)
{
    // This deliberately slow write must not contribute to any recorded total.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    logged_rows.push_back(row);
}

extern "C" void forest_root(double *features, double *out, int offset, int n_preds)
{
    assert(offset == 0 && n_preds == 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    *out = features[0] * 2.0;
    ++predictions;
}

int main()
{
    class access_log log;
    access_log = &log;
    std::vector<page_ptr> pages;
    for (size_t i = 0; i < 3; ++i)
    {
        auto page = std::make_shared<page_info>();
        page->va = i + 1;
        page->score = 42.0f;
        pages.push_back(page);
    }

    for (uint64_t step : {7, 8})
    {
        virtual_step.store(step);
        logged_rows.clear();
        model_score_timing timing;
        // Represents group/window aggregation before entering row extraction.
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        log_virtual_step_rows(pages, timing);
        assert(logged_rows.size() == pages.size());
        for (size_t i = 0; i < pages.size(); ++i)
        {
            const auto &row = logged_rows[i];
            assert(row.step == step);
            assert(row.model_score == (i + 1) * 2.0f);
            assert(row.score == 42.0f);
            assert(pages[i]->last_model_score_step == 0);
#if MODEL_TIMING_TELEMETRY_ENABLED
            assert(row.model_feature_aggregation_ns >= 7000000);
            assert(row.model_inference_ns >= 3000000);
            assert(row.model_score_total_ns >= row.model_feature_aggregation_ns + row.model_inference_ns);
            assert(row.model_feature_aggregation_ns == logged_rows[0].model_feature_aggregation_ns);
            assert(row.model_inference_ns == logged_rows[0].model_inference_ns);
            assert(row.model_score_total_ns == logged_rows[0].model_score_total_ns);
#endif
        }
    }
    assert(predictions == 6);

    std::vector<data_row> rows(pages.size());
    for (size_t i = 0; i < rows.size(); ++i)
        rows[i].ewma_2 = static_cast<float>(i + 1);
    model_predict_batch_observe(rows, pages);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        assert(rows[i].model_score == logged_rows[i].model_score);
#if MODEL_TIMING_TELEMETRY_ENABLED
        assert(rows[i].model_feature_aggregation_ns == 0);
        assert(rows[i].model_inference_ns == 0);
        assert(rows[i].model_score_total_ns == 0);
#endif
    }
    rows.clear();
    pages.clear();
    model_score_timing empty_timing;
    log_virtual_step_rows(pages, empty_timing);
    assert(predictions == 9);
}
