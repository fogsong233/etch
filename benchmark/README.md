# Regex Benchmark

This benchmark compares full-match runtime throughput across:

- `etch_tnfa`
- `etch_tdfa`
- `ctre`
- `std_regex`

`regex_ablation` is used to compare TDFA variants with optimization switches disabled.

## Build

```bash
xmake f -y -m release --benchmark=y
xmake -y -b regex_benchmark
xmake -y -b regex_ablation
xmake -y -b buildcost_etch_tdfa
xmake -y -b buildcost_etch_tnfa
xmake -y -b buildcost_ctre
xmake -y -b buildcost_std_regex
```

## Run

```bash
xmake run regex_benchmark -- <corpus_size> <target_ops_per_case>
xmake run regex_ablation -- <corpus_size> <target_ops_per_case>
python3 benchmark/compile_cost/measure_build_cost.py --repeat 3
```

Example:

```bash
xmake run regex_benchmark -- 2048 800000
xmake run regex_ablation -- 8192 5000000
python3 benchmark/compile_cost/measure_build_cost.py --repeat 3 --quiet
```

For paper reporting, run each benchmark at least 3 times and report the mean `ns/op`.

## Output Columns

- `case`: regex case name
- `engine`: `etch_tnfa` / `etch_tdfa` / `ctre` / `std_regex`
- `ns/op`: average nanoseconds per match call
- `Mops/s`: million match calls per second
- `matched`: number of successful matches (sanity check)
- `rounds`: loop rounds for the case
