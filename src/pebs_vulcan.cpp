#include "pebs_vulcan.h"
#include "pebs.h"
#include "libvulcan/include/vulcan.h"

extern "C" struct hemem_page *pebs_find_page(uint64_t va);

// All C++ objects live here, heap-allocated in pebs_vulcan_init() to avoid SIOF:
// the __attribute__((constructor)) in interpose.c fires before C++ static constructors.
struct PebsVulcanState {
    vulcan::feature_registry registry;
    vulcan::rank_config config;
    vulcan::feature_handle<double> h_dram_bw;
    vulcan::feature_handle<double> h_nvm_bw;
    vulcan::feature_handle<double> h_accesses;
    vulcan::rank_policy* tiering_policy = nullptr;
    vulcan::feature_store* store = nullptr;
};

// Raw pointer — trivially zero-initialized, no constructor needed.
static PebsVulcanState* state = nullptr;

extern "C" void pebs_vulcan_init(void)
{
    state = new PebsVulcanState();

    state->h_dram_bw = state->registry.global.declare_f64("h_dram_bw", "DRAM bandwidth in GB/s");
    state->h_nvm_bw  = state->registry.global.declare_f64("h_nvm_bw",  "NVM bandwidth in GB/s");
    state->h_accesses = state->registry.object.declare_f64("h_accesses", "Avg accesses (ms)");
}


extern "C" void pebs_vulcan_setup_LLM_heuristic(){
    state->config.add_listeners(state->h_dram_bw, {vulcan::listeners::global::RollingWindow(1)});
    state->config.add_listeners(state->h_nvm_bw,  {vulcan::listeners::global::RollingWindow(1)});
    state->config.add_listeners(state->h_accesses, {vulcan::listeners::object::EWMA(0.6667) });

    auto h = state->h_accesses;
    auto scoring_fn = [h](const vulcan::feature_store& fs, int64_t obj_id) -> double {
        return fs.get_ewma(h, obj_id);
    };

    state->config.set_sorting_function(vulcan::rank::FullSort);
    state->config.set_scoring_fn(scoring_fn);
    state->config.set_comparator(vulcan::min);

    // Create policy only after config is fully configured — it takes a copy.
    state->tiering_policy = new vulcan::rank_policy(state->registry, state->config);
    state->store = &state->tiering_policy->get_feature_store();
}


extern "C" void pebs_vulcan_add_page(uint64_t va)
{
    if (!state || !state->tiering_policy) return;
    state->tiering_policy->add_object(va);
}

extern "C" void pebs_vulcan_remove_page(uint64_t va)
{
    if (!state || !state->tiering_policy) return;
    state->tiering_policy->remove_object(va);
}

extern "C" void pebs_vulcan_update_bw(double dram_bw, double nvm_bw)
{
    if (!state || !state->store) return;
    state->store->update(state->h_dram_bw, dram_bw);
    state->store->update(state->h_nvm_bw,  nvm_bw);
}

extern "C" void pebs_vulcan_update_accesses(uint64_t va, double new_accesses)
{
    if (!state || !state->store) return;
    state->store->update(state->h_accesses, va, new_accesses);
}

extern "C" void pebs_vulcan_get_all_ranks(struct score_entry *scores_out, int *num_pages_copied, const int max_pages_allowed)
{
    *num_pages_copied = 0;
    if (!state || !state->tiering_policy) return;

    auto ranked = state->tiering_policy->rank_candidates();
    for (auto& [va, score] : ranked) {
        if (*num_pages_copied >= max_pages_allowed) break;
        struct hemem_page *page = pebs_find_page((uint64_t)va);
        if (!page) continue;
        scores_out[(*num_pages_copied)++] = { page, (float)score };
        page->score = (float)score;
    }
}
