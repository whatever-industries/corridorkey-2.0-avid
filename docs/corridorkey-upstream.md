# CorridorKey 2.0 upstream integration

Reviewed [upstream pull requests](https://github.com/nikopueringer/CorridorKey2.0/pulls) on November 19, 2026. The local integration lives on `avid-runtime-integration` inside `CorridorKey2.0`. It starts from upstream `master` at `485f5e9ab759ead6138cc990776184b59d6ea971`. Upstream PR [#1](https://github.com/nikopueringer/CorridorKey2.0/pull/1) is already part of that baseline.

## Carried in the Avid runtime

| Upstream PR | Source revision | Local revision | Change |
|---|---|---|---|
| [#10](https://github.com/nikopueringer/CorridorKey2.0/pull/10) | `2ff9d29f907ae5da39f66b40eb20924e63f857db` | Same commit | Existing Apple Silicon MPS port and float EXR interface used by the Mac worker. |
| [#4](https://github.com/nikopueringer/CorridorKey2.0/pull/4) | `62fed4607436eaba6e16eb4d2a3b53c4ac4144a9` | `66c7142be63a9807336282a3b49da13d49bb17bf` | Merged. Natural frame order and case-insensitive image extensions. |
| [#20](https://github.com/nikopueringer/CorridorKey2.0/pull/20) | `66d3429c68cda3a4978ef70b1d103baaacc445b1` | `72b2e1f541ee8484c81188da62a6ea439af98731` | Merged. Lookup-table preprocessing for eligible 8-bit inputs, including the Windows worker’s SDR PNG frames. |
| [#23](https://github.com/nikopueringer/CorridorKey2.0/pull/23) | `d753c8605f7673e69e315357c810500f9016495e` | `8ad39f5dad2027e41e29c1c202ae0a4b5cd23196` | Partial. Imported the RVM downloader’s TLS certificate fallback. The integration uses #10 for device selection and inference. |

Local tip `6c793c3a62c4d914a1f16cdce10bb149f9c35774` preserves hidden-file exclusion and deterministic sort ties, keeps adjusted stills on the original preprocessing path, and adds the fallback CA bundle to system trust when needed.

## Revisit when the relevant work starts

| Upstream PRs | Reason to defer |
|---|---|
| [#3](https://github.com/nikopueringer/CorridorKey2.0/pull/3), [#14](https://github.com/nikopueringer/CorridorKey2.0/pull/14) | Review together when changing dependency management. Both affect how Mac PyTorch and Windows CUDA wheels are installed. |
| [#6](https://github.com/nikopueringer/CorridorKey2.0/pull/6), [#7](https://github.com/nikopueringer/CorridorKey2.0/pull/7), [#13](https://github.com/nikopueringer/CorridorKey2.0/pull/13) | ROCm device selection and attention support. #13 already contains #6. Revisit with an AMD test target. |
| [#9](https://github.com/nikopueringer/CorridorKey2.0/pull/9), [#18](https://github.com/nikopueringer/CorridorKey2.0/pull/18) | Performance candidates. Compare separately at the same model precision and temporal window. #9 stores some weights in BF16, which also affects what the Mac float32 retry can recover. |
| [#11](https://github.com/nikopueringer/CorridorKey2.0/pull/11) | Automatic VRAM selection and CUDA OOM retries. Both Avid workers currently use explicit memory settings. Revisit when adding automatic sizing. |
| [#15](https://github.com/nikopueringer/CorridorKey2.0/pull/15) | Alternative MPS port with bounded attention and an optional native operator. Compare against #10 on the same footage, precision and peak memory. |
| [#8](https://github.com/nikopueringer/CorridorKey2.0/pull/8), [#17](https://github.com/nikopueringer/CorridorKey2.0/pull/17) | Standalone GUI and Rust engine respectively. The Avid effect has its own UI and calls the Python runtime. |
| [#21](https://github.com/nikopueringer/CorridorKey2.0/pull/21), [#22](https://github.com/nikopueringer/CorridorKey2.0/pull/22) | Drafts. #21 combines overlapping PRs and is labeled “Do Not Merge.” #22 adds a large desktop and OFX stack and flags unresolved licensing for its OpenDRT integration. |
| [#5](https://github.com/nikopueringer/CorridorKey2.0/pull/5), [#12](https://github.com/nikopueringer/CorridorKey2.0/pull/12), [#16](https://github.com/nikopueringer/CorridorKey2.0/pull/16), [#19](https://github.com/nikopueringer/CorridorKey2.0/pull/19) | Closed without merging. |

## Verification and handoff

- Tested the combined branch once on Apple Silicon. The 1080p green-screen and blue-proxy frames returned byte-identical float RGBA against the pre-integration worker. The repeated frame hit the matte cache and neither inference needed the float32 retry.
- Compared all 256 8-bit codes and padded frames against the original preprocessing, including PNG and signed float EXR paths. Checked natural frame order, TLS verification with system and fallback trust, and the five Mac worker protocol/cache tests. Windows CUDA and in-host Media Composer validation are pending.
- The public checkout pins the official upstream baseline and applies the [runtime patch](../runtime/patches/corridorkey-2.0.patch). This reproduces the source tree at the recorded integration tip. The local branch preserves the original commit history.

```sh
git submodule update --init --recursive
git -C CorridorKey2.0 apply ../runtime/patches/corridorkey-2.0.patch
```
