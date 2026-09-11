# PR-Kron model candidate for historical replay

The strongest retained candidate for the **May 7 PR-Kron cost-1.5 model**, which
immediately preceded BC Twitter ARMS in all three original repetitions, is:

`models/models/model_discounted_reward_99_pr-kron.sg_l2.o`

Its retained mtime is **May 3, 2026, 03:43:42.290006 UTC**; size is **308,952
bytes**; SHA-256 is:

```text
46deab29bd8eae7515e9633b91b630470666530bfa742d6d9d7a109ad63706ea
```

Use this as a **candidate reconstruction**, with frozen May04 source, discount
99 and configuration **`[2, 10, 1.5]`**. This audit did not compile, link, execute
the model, or change any host setting. No saved per-run forest/library hash
proves these were the bytes loaded by the original May 7 process.

The three matching files are now frozen, with source mtimes preserved, under
`conditions/speed_matching/historical_models/may03_pr_kron/`. Its
[manifest](../../speed_matching/historical_models/may03_pr_kron/manifest.json)
records source paths, hashes, mtimes, feature order, training-repository byte
matches and the missing per-run identity caveat. All three frozen hashes were
verified against their sources after copying.

## Why this candidate is the best supported

- The accompanying text model and report have mtimes May 3 03:43:37 UTC. The
  directory also contains the known May03 BC Twitter object, whose hash is
  exactly the `763ad7a4…` forest already frozen for the BC reproduction.
- The PR-Kron `.o`, `.txt`, and report are **all byte-identical** to the copies
  under `models/with_age/`, whose retained mtimes are May 3 16:50:19 UTC.
  The directory name alone is not used to identify model generation: its BC
  Twitter object was replaced on May 14, but this PR-Kron object was not.
- All three files also byte-match training repository commit
  `25cd9eddd277c84363e4340fec527625bb5e86b9`, path
  `process_data/models/model_discounted_reward_99_pr-kron.sg_l2.*`. That commit
  is dated **May 30 19:27:26 UTC**. It preserves the bytes independently, but
  does not independently prove their selection on May 7.
- The text model contains **225 trees**, objective **`regression sqrt`**, and
  the same **12-feature order** consumed by frozen May04 `model.cpp` and used
  by the known May03 BC forest:

```text
ewma_2 ewma_5 ewma_20 ewma_100 global_avg_accesses_model
group_neg_mean group_pos_mean group_0_mean age
r_ratio_20 r_ratio_100 group_ewma5_var
```

The short report identifies PR-Kron and discounted reward 99, with reported
honest-evaluation R² 0.9957 and all-data retrain R² 0.9958. It does not preserve
individual training-file dates or historical benchmark model-loading logs.

| Companion | Retained mtime UTC | SHA-256 |
|---|---|---|
| `.txt` | May 3 03:43:37.155936 | `9b50c0a228b9a474148908fecc73b20692a65110f10ce2959ffdcec3c7d5ea33` |
| `_report.txt` | May 3 03:43:37.150936 | `e9d59da459bad8b4f80a8ac0b297b79f3af7bf569598ab7a29b225f3d9d74931` |

## Other retained PR-Kron generations

| Location | Object mtime UTC | Trees / features | Assessment for May 7 replay |
|---|---|---|---|
| `models/models_old/` | April 24 01:41:09 | 20 / 14 | Older feature order, including separate write EWMAs and gap features; does not match frozen May04 extraction. |
| `models/model_100k/` | April 24 01:41:08 | 249 / 14 | Also uses the older feature order. |
| `models/models/` | May 3 03:43:42 | 225 / 12 | Selected candidate. |
| `models/with_age/` | May 3 16:50:19 | 225 / 12 | Exact duplicate of selected candidate. |
| `models/no_age/` | June 13 20:19:04 | 116 / 11 | Later generation; removes age and shifts the feature layout. |
| Root `models/` | June 18 17:02:06 | 128 / 12 | Later, different forest despite matching feature names. |

No PR-Kron object exists under `models/old/`; that directory holds the selected
BC Twitter copy. Full paths, file sizes, hashes, dates, reports and Git matches
are retained in [replay_model_provenance.json](replay_model_provenance.json).

The original predecessor timing files explicitly name PR-Kron, reward 99 and
`l2-2_10_1.5`; their temporal relationship to BC ARMS is preserved in
[the launch-order audit](profiling_order_history.md). That identifies the model
family and runtime configuration. The selected forest's May3 provenance and
matching feature layout make it the best retained reconstruction, while its
historical loaded-byte identity remains unverified.
