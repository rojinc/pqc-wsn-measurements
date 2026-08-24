# Note: why FN-DSA-512 signing is slow on Cortex-M0+

FN-DSA-512 (FALCON) is by far the slowest scheme to *sign* in this campaign on
both platforms, despite producing the most compact signature of any NIST
post-quantum candidate. This note records the measurement and the reason, since
the figure is often quoted an order of magnitude lower in the literature.

## The measurement

RP2040 (Raspberry Pi Pico), ARM Cortex-M0+ @ 125 MHz, PQClean `falcon-512/clean`,
`-Os`, correctness verified on every iteration:

| Operation | n | mean | CV | p99 | max | peak stack |
|---|--:|--:|--:|--:|--:|--:|
| verify | 10,000 | **12.90 ms** | 0.17% | 12.95 ms | 13.14 ms | 4.6 KB |
| sign | 10,000 | **1286.45 ms** | 0.22% | 1293.27 ms | 1296.57 ms | 42.2 KB |
| keygen | 1,000 | **5686.70 ms** | 37.28% | 13522.82 ms | **21312.07 ms** | 17.6 KB |

Signing is tight: across 10,000 signatures the full observed range is
1274.89–1296.57 ms. Signature length averages 655.1 B (647–664), consistent with
the 666 B padded FN-DSA form.

Cross-platform consistency: the ESP32 measures 354.44 ms for the same operation,
so the RP2040/ESP32 ratio is 3.63×, in line with the 2.0–3.7× range observed for
the other schemes (see `VALIDATION_rp2040.md`). Two independent platforms agree.

## Head-to-head signing on Cortex-M0+ (RP2040)

| Scheme | n | mean | CV | p99 | max |
|---|--:|--:|--:|--:|--:|
| ECDSA-P256 | 1,000 | 116.21 ms | 0.41% | 117.28 ms | 117.51 ms |
| ML-DSA-44 | 10,000 | 182.70 ms | 64.00% | 584.20 ms | 1156.79 ms |
| FN-DSA-512 | 10,000 | **1286.45 ms** | **0.22%** | 1293.27 ms | 1296.57 ms |

FN-DSA-512 is 7.0× slower than ML-DSA-44 at the mean and 11.1× slower than
ECDSA-P256. Even against ML-DSA's *worst observed* signature (1156.79 ms),
FN-DSA's *mean* is slower.

Key generation also matters for field provisioning: **5.7 s mean with a 21.3 s
worst case** on this hardware, with a 37% CV, because keygen rejects and
resamples until it finds a valid lattice trapdoor. This is a one-time
provisioning cost rather than a per-transaction one, but it is not negligible for
a device provisioned in the field.

## Why: constant-time software floating point

PQClean's `falcon-512/clean` implements floating point as
`typedef uint64_t fpr` — **constant-time software emulation. No FPU is used on
any platform, including platforms that have one.**

This is deliberate. FALCON's Gaussian sampler leaks key material through timing
if it runs on variable-time hardware floating point, so the reference
implementation emulates it in constant time instead.

Consequences worth stating in any write-up:

- The 1286 ms figure reflects constant-time emulated FP, which is the
  security-appropriate choice on MCUs without constant-time FP hardware.
- The Cortex-M0+ has no FPU at all, so no hardware-FP alternative exists there.
- An FP-native or hand-optimised build would be faster but side-channel
  questionable on this class of hardware. A substantially lower published figure
  for this platform most likely reflects such a build, a different
  implementation, or a value carried over from literature without on-hardware
  verification. Either way the trade-off needs stating rather than being
  presented as a straight speed result.

## What the data does support

The case for FN-DSA on constrained hardware does not rest on signing speed. It
rests on three properties this campaign measures directly:

1. **Determinism.** Signing CV is 0.22% against ML-DSA's 64.00%. FN-DSA is
   effectively constant-time (1274.9–1296.6 ms across 10,000 runs); ML-DSA ranges
   80.1–1156.8 ms. For a latency-bounded control loop, a predictable 1.29 s can
   be preferable to an unpredictable 0.18 s that spikes past 1.15 s.
2. **Transmission cost.** 666 B against 2420 B, so 6 against 20 IEEE 802.15.4
   frames, and 3 against 11 LoRaWAN frames. On low-rate radios this dominates.
3. **Verification.** 12.90 ms against ML-DSA's 50.93 ms, so 3.9× faster at the
   gateway tier where verification volume concentrates.

Summarised: **FN-DSA trades signing speed for compactness, verification speed,
and latency predictability.** It is not the fastest scheme to sign.

## Effect on the on-spend window

Substituting measured signing times into
`T_osp = (T_sign + T_tx) + (T_wake + T_block)`:

| Scenario | radio + chain | crypto contribution | exceeds T_crqc = 540 s? |
|---|--:|--:|:--:|
| 802.15.4 (60 s) + Fabric | 64 s | 0.12–1.31 s | no, for every scheme |
| 802.15.4 (60 s) + Ethereum PoS | 72 s | 0.12–1.31 s | no, for every scheme |
| LoRaWAN Class A + Ethereum | 1812 s | 0.25–3.99 s | **yes**, for every scheme |

Excluding SLH-DSA, the entire spread between the fastest and slowest scheme is
1.19 s on 802.15.4 and 3.74 s on LoRaWAN: roughly 2% and 0.2% of the respective
windows.
**Signature choice does not determine whether a deployment crosses the CRQC
threshold. The duty cycle alone does.** SLH-DSA-128s is the one exception, since
its 208 s signing time is itself a material fraction of an 802.15.4 window.

Two consequences follow, and they pull in opposite directions:

- It supports the structural claim that WSN duty-cycle wakeup extends the
  on-spend window past the 540 s threshold, since LoRaWAN exceeds it by more than
  3× regardless of which signature scheme runs.
- It weakens any algorithm-selection argument made on on-spend grounds. If
  on-spend exposure is governed by radio timing, FN-DSA cannot be justified
  there. Its case rests on bandwidth, memory, verification cost, and
  predictability.
