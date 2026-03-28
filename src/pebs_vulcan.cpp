#include "pebs_vulcan.h"
#include "libvulcan/include/vulcan.h"
#include <memory>
#include <optional>

struct hemem_page;
struct score_entry {
    struct hemem_page *page;
    float score;
};
extern "C" struct hemem_page *pebs_find_page(uint64_t va);

static vulcan::feature_registry registry;
static vulcan::feature_handle<double> h_dram_bw;
static vulcan::feature_handle<double> h_nvm_bw;
static vulcan::feature_handle<double> h_accesses;
static vulcan::feature_store* store = nullptr;
static std::optional<vulcan::rank_policy> tiering_policy;
static vulcan::rank_config config;

extern "C" void pebs_vulcan_init(void)
{
    h_dram_bw = registry.global.declare_f64("h_dram_bw", "DRAM bandwidth in GB/s");
    h_nvm_bw  = registry.global.declare_f64("h_nvm_bw",  "NVM bandwidth in GB/s");
    h_accesses = registry.object.declare_f64("h_accesses", "Avg accesses (ms)");

    tiering_policy.emplace(registry, config);
    store = &tiering_policy->get_feature_store();
}


extern "C" void pebs_vulcan_setup_LLM_heuristic(){
    config.add_listeners(h_dram_bw, {vulcan::listeners::global::RollingWindow(1)});
    config.add_listeners(h_nvm_bw,  {vulcan::listeners::global::RollingWindow(1)});
    config.add_listeners(h_accesses, {vulcan::listeners::object::EWMA(0.6667) });

    auto scoring_fn = [&](const vulcan::feature_store& fs, int64_t obj_id) -> double {
        return fs.get_ewma(h_accesses, obj_id);
    };

    config.set_sorting_function(vulcan::rank::FullSort);
    config.set_scoring_fn(scoring_fn);
    config.set_comparator(vulcan::min);
}


extern "C" void pebs_vulcan_add_page(uint64_t va)
{
    tiering_policy->add_object(va);
}

extern "C" void pebs_vulcan_remove_page(uint64_t va)
{
    tiering_policy->remove_object(va);
}

extern "C" void pebs_vulcan_update_bw(double dram_bw, double nvm_bw)
{
    if (!store) return;
    store->update(h_dram_bw, dram_bw);
    store->update(h_nvm_bw,  nvm_bw);
}

extern "C" void pebs_vulcan_update_accesses(uint64_t va, double new_accesses)
{
    if (!store) return;
    store->update(h_accesses, va, new_accesses);
}

extern "C" void pebs_vulcan_get_all_ranks(struct score_entry *scores_out, int *num_pages_copied, const int max_pages_allowed)
{
    *num_pages_copied = 0;
    if (!tiering_policy) return;

    auto ranked = tiering_policy->rank_candidates();
    for (auto& [va, score] : ranked) {
        if (*num_pages_copied >= max_pages_allowed) break;
        struct hemem_page *page = pebs_find_page((uint64_t)va);
        if (!page) continue;
        scores_out[(*num_pages_copied)++] = { page, (float)score };
    }
}