state->config.add_listeners(state->dram_bw, {vulcan::listeners::global::RollingWindow(1)});
state->config.add_listeners(state->nvm_bw,  {vulcan::listeners::global::RollingWindow(1)});
state->config.add_listeners(state->accesses, {vulcan::listeners::object::EWMA(0.6667), vulcan::listeners::object::RollingWindow(1) });

auto scoring_fn = [](const vulcan::feature_store& fs, int64_t obj_id) -> double {
    return fs.get_ewma(state->accesses, obj_id);
};