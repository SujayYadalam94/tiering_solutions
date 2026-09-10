#include "vulcan.h"
#include "pebs.h"
#include "libvulcan/include/vulcan.h"

// All C++ objects live here, heap-allocated in pebs_vulcan_init() to avoid SIOF:
// the __attribute__((constructor)) in interpose.c fires before C++ static constructors.
struct PebsVulcanState {
    vulcan::feature_registry registry;
    vulcan::rank_config config;
    vulcan::feature_handle<double> dram_bw;
    vulcan::feature_handle<double> nvm_bw;
    vulcan::feature_handle<double> accesses;
    vulcan::rank_policy* tiering_policy = nullptr;
    vulcan::feature_store* store = nullptr;
};

// Raw pointer — trivially zero-initialized, no constructor needed.
static PebsVulcanState* state = nullptr;

extern "C" void pebs_vulcan_init(void)
{
    state = new PebsVulcanState();

    state->dram_bw = state->registry.global.declare_f64("dram_bw", "DRAM bandwidth in GB/s");
    state->nvm_bw  = state->registry.global.declare_f64("nvm_bw",  "NVM bandwidth in GB/s");
    state->accesses = state->registry.object.declare_f64("accesses", "Avg accesses (ms)");
}


extern "C" void pebs_vulcan_setup_LLM_heuristic(){
    // Feature-handle aliases so a policy is written against the standalone libVulcan names
    // (f_*) and the read-only feature_store type (FS_REF). Macros (not references) so that a
    // no-capture [] scoring lambda can still reach the file-static `state`.
    #define FS_REF const vulcan::feature_store&
    #define f_config   state->config
    #define f_accesses state->accesses
    #define f_dram_bw  state->dram_bw
    #define f_nvm_bw   state->nvm_bw
    #include "LLMCode.h"
    #undef FS_REF
    #undef f_config
    #undef f_accesses
    #undef f_dram_bw
    #undef f_nvm_bw

    state->config.set_sorting_function(vulcan::rank::FullSort);
    state->config.set_scoring_fn(scoring_fn);
    state->config.set_comparator(vulcan::max);
    state->config.set_information(
        "You are an expert systems researcher specializing in memory tiering policies. "
        "Your task is to design a scoring function for a page: higher the score, the "
        "more important the page is, and the likelier it is to get promoted to the fast "
        "tier; similarly, lower score = likelier to be demoted to a slower tier. "
        "You are encouraged to be creative when defining this scoring function -- we "
        "will be using this scoring function within a real system and testing out how well it works."
    );

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
    assert (state && state->store && "[update_bw] state or state->store not set!");
    state->store->update(state->dram_bw, dram_bw);
    state->store->update(state->nvm_bw,  nvm_bw);
}

extern "C" void pebs_vulcan_update_accesses(uint64_t va, double new_accesses)
{
    assert (state && state->store && "[update_accesses] state or state->store not set!");
    state->store->update(state->accesses, va, new_accesses);
}

extern "C" size_t pebs_vulcan_get_page_ranks(struct score_entry *scores_out)
{
    size_t num_pages = 0;
    if (!state || !state->tiering_policy) return 0;
    auto ranked = state->tiering_policy->rank_candidates();
    for (auto& [va, score] : ranked) {
        struct arms_page* page = pebs_find_page_maps(va);
        if (!page) {
            fprintf(stderr, "Vulcan Error: Ranked page with VA %lu not found in ARMS page tracking\n", va);
            continue;
        }
        scores_out[num_pages++] = (struct score_entry){ .page = page, .score = score };
        page->prev_score = page->score;
        page->score = score;
    }

    return num_pages;
}
