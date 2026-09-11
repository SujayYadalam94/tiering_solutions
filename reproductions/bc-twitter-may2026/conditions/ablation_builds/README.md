# Historical BC Twitter cost ablation builds

These cases reproduce the five cost settings in the May 7 ablation, using the
frozen May 4 source and May 3 model candidate in the parent reproduction package.
They do not select a disk or run a benchmark. The historical per-run binary and
model hashes were not recorded, so these are reconstructed candidates.

| Case | Scaler | Model history | Model discount |
| --- | ---: | --- | ---: |
| `may07_arms` | ARMS fixed cost multiplier 1.5 | None | None |
| `may07_cost_0p125` | 0.125 | Adjusted moving average, length 10 | 99% |
| `may07_cost_0p25` | 0.25 | Adjusted moving average, length 10 | 99% |
| `may07_cost_0p5` | 0.5 | Adjusted moving average, length 10 | 99% |
| `may07_cost_1p0` | 1.0 | Adjusted moving average, length 10 | 99% |
| `may07_cost_1p5` | 1.5 | Adjusted moving average, length 10 | 99% |

The model's base migration cost multiplier is `scaler / (1 - 0.99)`; the frozen
source retains its historical virtual-time scaling as well. Thus the numeric
model scaler is not the same quantity as the ARMS fixed multiplier.

Rebuild with:

```sh
python3 reproductions/bc-twitter-may2026/conditions/ablation_builds/build.py
```

The script validates every frozen May 4 source/header file, the May 3 model
object/text/report, and the reused ARMS and scaler-0.5 library hashes against the
parent manifest. It requires the same GCC version recorded there, compiles the
remaining settings sequentially with the parent's flags, and writes a separate
`manifest.json` containing the compiler, definitions, input hashes, and output
library hashes. Library paths in that manifest are relative to this directory.
No working source, working library, or host setting is modified.
