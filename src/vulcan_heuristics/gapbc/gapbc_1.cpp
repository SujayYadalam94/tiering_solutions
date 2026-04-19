// Fast-adapting EWMA with optimized bandwidth scaling
state->config.add_listeners(state->accesses, { 
    vulcan::listeners::object::EWMA({0.77})
});
state->config.add_listeners(state->dram_bw, {
    vulcan::listeners::global::EWMA({0.29})
});
state->config.add_listeners(state->nvm_bw, {
    vulcan::listeners::global::EWMA({0.29})
});

auto scoring_fn = [](const vulcan::feature_store& fs, int64_t obj_id) -> double {
    double a = fs.get_ewma(state->accesses, obj_id, 0.77);
    double d = fs.get_ewma(state->dram_bw, 0.29);
    double n = fs.get_ewma(state->nvm_bw, 0.29);
    
    // Bandwidth-aware multiplier
    double r = (n > 0.01) ? (d / n) : 1.0;
    double m = 1.0 + 0.11 * (r - 1.0);
    m = (m < 0.57) ? 0.57 : (m > 1.85) ? 1.85 : m;
    
    return a * m;
};