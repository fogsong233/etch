# Regex Benchmark

This benchmark compares runtime full-match throughput between:

- `Etch TDFA` (this project)
- `CTRE` (`compile-time-regular-expressions`)

## Build

```bash
xmake f -m release --benchmark=y
xmake
```

## Run

```bash
xmake run regex_benchmark
```

Optional arguments:

```bash
xmake run regex_benchmark -- <corpus_size> <target_ops_per_case>
```

Example:

```bash
xmake run regex_benchmark -- 16384 10000000
```

## Notes

- Benchmarks use full-match semantics on both engines.
- Each case runs with the same generated corpus and injected known samples.
- Output shows `ns/op` and `Mops/s` per engine.
