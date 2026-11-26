# Matched original uncapped benchmark — 2026-11-25

This repeats the original early level-1 idle workload using the preserved old
native executable, current native reconstruction and John's supplied executable.
The old native binary hash matches the historical receipt. All are run afresh
in alternating order, five trials at each virtual cadence, unrestricted affinity.

| Virtual cadence | Preserved native FPS | Latest native FPS | John FPS |
| --- | ---: | ---: | ---: |
| 24 Hz | 426.0 | 419.2 | 172.3 |
| 60 Hz | 440.6 | 444.2 | 212.4 |

These are median trial throughputs, not frame caps. Latest versus preserved
native is -1.6% at 24 Hz and +0.8% at 60 Hz. Individual trial ranges overlap
substantially; this is broadly unchanged idle throughput, not a demonstrated
optimization gain or a drop to 230 FPS. John's runs were especially variable
without pinned CPU affinity; full ranges and timings are retained in the receipt.

The original historical native medians were 432.9 and 440.7 FPS; rerunning the
preserved executable today gives 426.0 and 440.6. Compare today's old/new pair
rather than attributing machine/run variation to code.

Method retained: 1440x1080 offscreen GPU target, no wall-clock pacing, no
presentation, interpolation off, 32 active-picture warmup then 128 measured
pictures. Native mixer/media uses the same virtual 24/60 Hz clock, without an
audio-device queue. John uses the same original state entry and GPU drawing
gate. The old/native gameplay counters match at measured boundaries: at 24 Hz,
wave=10, used=0 at active picture33; wave=10, used=2 at picture160; score/exit=0.

The latest test executable links current reconstruction and renderer sources.
Its benchmark driver and host retain the original algorithms, with include paths
updated and the new GPU-ring option enabled. It runs original single-step logic;
it is not the 120 Hz five-substep workload. Consequently it does not exercise
the new fine-step feedback ownership optimization. Do not interpret idle results
as coverage of large rings, death effects, or level transitions.

This deliberately retains the original offscreen flush-only submission method.
It is not a fence-completed GPU benchmark or an end-to-end onscreen measurement.
The separate combat benchmark (~230 fine steps/draws per second) uses actual
presentation and a longer, different route. The two figures are different tests.

Reproduction: build optional reference target `t3k_idle_benchmark`, then run
`../tempest-profile/benchmark_matched_idle_latest.py` from this repository.
Raw results, per-trial CSVs/logs and binary/source hashes are under
`../tempest-profile/benchmarks/matched-idle-latest/`. No game assets are included
in the benchmark sources. The playable executable and frozen handoff are unchanged.
