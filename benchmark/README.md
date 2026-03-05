# Regex Benchmark

This benchmark compares full-match runtime throughput across:

- `etch_tnfa`
- `etch_tdfa`
- `ctre`
- `std_regex`

## Build

```bash
xmake f -y -m release --benchmark=y
xmake -y -b regex_benchmark
```

## Run

```bash
xmake run regex_benchmark -- <corpus_size> <target_ops_per_case>
```

Example:

```bash
xmake run regex_benchmark -- 2048 800000
```

## Output Columns

- `case`: regex case name
- `engine`: `etch_tnfa` / `etch_tdfa` / `ctre` / `std_regex`
- `ns/op`: average nanoseconds per match call
- `Mops/s`: million match calls per second
- `matched`: number of successful matches (sanity check)
- `rounds`: loop rounds for the case
