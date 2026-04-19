// Wider alpha gap (0.95/0.35) for stronger acceleration signal
state->config.add_listeners(state->accesses, { 
    vulcan::listeners::object::EWMA({0.95, 0.35})
});

auto scoring_fn = [](const vulcan::feature_store& fs, int64_t obj_id) -> double {
    double fast = fs.get_ewma(state->accesses, obj_id, 0.95);
    double slow = fs.get_ewma(state->accesses, obj_id, 0.35);
    double accel = (slow > 0.001) ? (fast / slow) : 1.0;
    return fast * (1.0 + 0.55 * std::min(accel, 2.5));
};