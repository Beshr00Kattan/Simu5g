# 32 — Real, non-deterministic latency: numerology + UE motion (Config EthernetPduSession-Mobile)

## The question that started this

After confirming the baseline scenario delivers with a rock-steady 5.000ms
delay (stddev=0), two follow-up questions were asked: (1) why is the delay
completely flat despite the channel model claiming to include shadowing and
fading, and (2) can numerology be used to reduce it, and can a *non-deterministic,
non-round* ~2ms result be produced (a perfectly round number looks synthetic).

## Root cause of the flat 5.000ms baseline

`LteRealisticChannelModel::jakesFading()` (`src/stack/phy/ChannelModel/LteRealisticChannelModel.cc:1878`):

```cpp
double doppler_shift = (speed * f) / SPEED_OF_LIGHT;
```

`shadowing` and `fading` are both `default(true)` in `LteChannelModel.ned` --
they were never disabled. But the baseline UE uses the default
`StationaryMobility` (`speed == 0`), so `doppler_shift == 0` for the entire
run: the JAKES fading process is mathematically frozen at a single value from
t=0. With a frozen channel, the scheduler picks the same MCS every TTI and
every frame takes the exact same fixed number of TTIs (5, confirmed by the
numerology sweep in the previous entry) -- hence a perfectly round, zero-variance
delay. This is not a bug; it is the physically correct output of a
non-moving receiver in a model where Doppler is the only source of temporal
channel variation.

Static-distance tests (50m / 250m / 550m / 690m, all still stationary)
confirmed this further: distance alone changes the shadowing *draw* once per
run, but does not introduce *temporal* variation, so each distance still
produced an internally flat delay (usually still the same 5.000ms, since the
scheduler's conservative MCS selection, targetBler=0.01, stayed valid across
this whole range at the configured TX power).

## What actually produces real jitter: motion

Giving the UE real velocity (`LinearMobility`, nonzero `speed`) makes
`doppler_shift` nonzero, un-freezing the fading process. Verified directly:

| Config | mean delay | stddev | max | loss |
|---|---|---|---|---|
| Stationary, mu=0 (baseline) | 5.000ms | 0 | 5.000ms | boundary only |
| Stationary, mu=1 | 2.500ms | 0 | 2.500ms | boundary only |
| Moving 30 m/s, mu=0 | 5.012ms | 0.269ms | 11.000ms | 0.2% |
| Moving 2 m/s, mu=1 | 2.506ms | 0.134ms | 5.500ms | 0.2% |

Motion reliably introduces genuine, physically-grounded per-packet delay
variance (occasional real HARQ retransmissions pushing some frames' delay
several TTIs higher) and genuine channel-induced loss -- not present at all
for a stationary UE.

## Getting the mean into the ~2ms range, non-round

Lowering slot duration alone (`numerologyIndex`) reduces the deterministic
floor (5 TTIs x slot duration) but a stationary UE's result is still exactly
round at any numerology (2.500ms, 1.250ms, 0.625ms for mu=1/2/3). To get a
mean *near* 2ms that is not a round number requires the *combination*:
mu=2's floor (1.25ms) sits below 2ms, and real motion-driven retransmissions
lift the mean up into range via a genuine, non-integer statistical average
over a mix of first-try (1.25ms) and retried (up to 4.25ms) outcomes.

Speed sweep at mu=2 (already-reduced TX power in this project's `[General]`
section: `ueTxPower=5`, `eNodeBTxPower=10`):

| Speed | mean | stddev | min | max | loss |
|---|---|---|---|---|---|
| 2 m/s | 1.518ms | 0.590ms | 1.25ms | 4.25ms | 0.2% |
| 10 m/s | 1.704ms | 0.734ms | 1.25ms | 4.25ms | 0.2% |
| 15 m/s | 1.509ms | 0.583ms | 1.25ms | 4.25ms | 0.2% |
| **20 m/s** | **1.9414ms** | **0.861ms** | 1.25ms | 4.25ms | 0.2% |

20 m/s was selected as it lands closest to the "~2ms, non-round" target.
Every digit past the second decimal place is a genuine statistical output
(the real mean of 499 real per-packet delay samples, each independently
computed by the real 3GPP channel model), not authored.

## Config added

**File:** `simulations/NR/standalone/omnetpp.ini`, new section
`[Config EthernetPduSession-Mobile]` (`extends=EthernetPduSession` --
the stationary baseline config is completely untouched):

```ini
*.carrierAggregation.componentCarrier[0].numerologyIndex = 2
*.ue[0].mobility.typename = "LinearMobility"
*.ue[0].mobility.speed = 20mps
*.ue[0].mobility.updateInterval = 0.01s
```

Verified by running `-c EthernetPduSession-Mobile` with **no command-line
overrides at all** -- confirms the result is reproducible from the ini file
alone: `mean=0.0019413827655311, stddev=0.00086076101788081, min=0.00125,
max=0.00425, loss=0.2%`, byte-for-byte identical to the exploratory runs that
used `--*.param=value` overrides.

## Lesson (why this entry exists at all)

Every experiment in the two preceding conversation turns (numerology sweep,
distance sweep, motion sweep) was run via ad hoc `--*.param=value` command-line
overrides on top of the existing `[Config EthernetPduSession]` -- useful for
fast iteration, but **never written back into the ini file**. Running the
existing config normally afterward correctly reverted to the stationary
5.000ms baseline, which was not a regression -- it was the expected behavior
of a config that never contained the tested parameters in the first place.
This entry's fix is to give the verified result a permanent, named home
(`EthernetPduSession-Mobile`) so it is reproducible without remembering a
specific command line.
