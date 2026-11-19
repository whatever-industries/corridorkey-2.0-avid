# CorridorKey 2.0 on Apple Silicon

The native AVX2 effect uses a local Python worker and the Apple GPU through PyTorch MPS.

## Set up inference

Use native ARM64 Python 3.12 and install FFmpeg if you want to run the upstream video CLI. Follow the [repository setup](../README.md#build-from-source) to prepare the pinned runtime, then run the following from the repository root.

```sh
python3.12 -m venv runtime/.venv
runtime/.venv/bin/python -m pip install torch==2.9.1 torchvision==0.24.1 \
  -r CorridorKey2.0/requirements.txt
runtime/.venv/bin/python CorridorKey2.0/scripts/download_weights.py \
  --package_root .cache/model-assets \
  --corridorkey_revision cf44a8db3f024bb49fd59daa408848ccf05f7477

runtime_dir="$HOME/Library/Application Support/CorridorKeyAVX"
mkdir -p "$runtime_dir"
(umask 077; set -C; printf '%s\n' "$PWD/runtime/.venv/bin/python" "$PWD/CorridorKey2.0" \
  "$PWD/.cache/model-assets" > "$runtime_dir/worker.conf")
```

The [upstream integration log](corridorkey-upstream.md) records the PR revisions included in the runtime patch.

The worker reads the three absolute paths from `worker.conf`. Point them to the ARM64 Python executable, the pinned CorridorKey source checkout, and the model asset directory. If the file already exists, update those three paths in place. The worker writes diagnostics to `worker.log` beside it. The effect normalizes the foreground to the nominal SDR range before inference. The worker synchronizes GPU work between model stages and retries once in float32 on MPS if the output contains non-finite samples.

The macOS worker uses blocking CPU-to-MPS uploads during inference. The pinned runtime schedules asynchronous copies of temporary CPU tensors. The PyTorch MPS backend can read their storage after it has been released, which caused corrupted RGB samples to reach Stage 2. Blocking the copy preserves the source samples and keeps the same model and inference precision. [PyTorch MPS copy issue](https://github.com/pytorch/pytorch/issues/189690)

The worker caches the exact float32 foreground and matte before despill, keyed by frame dimensions and normalized foreground pixels. Replacing V1, switching between Composite and Matte, or changing Despill settings can reuse this result when Avid rerenders the same V2 frame. Cache hits are recorded in `worker.log`. Each worker keeps up to 128 frames and 4 GiB of temporary local storage. The operating system reclaims the cache when the worker exits.

After the transfer fix, repeated 1080p worker requests returned finite results at the original precision. Cache hits returned byte-identical foreground and matte in about 55–57 ms in the worker protocol test. Media Composer also logged a cache hit and composited the keyed foreground over a replacement V1 frame in the Scratch 24FPS test sequence.

## Validation status

On the 32 GB Apple Silicon test workstation, three 1080p frames processed in 29.3 seconds on a green-screen plate and 27.1 seconds on a hue-shifted blue-screen proxy. Wall times including model load were 50.4 and 47.7 seconds. The measured peak footprints were 8.8 and 9.0 GiB, respectively. Both runs used synchronized MPS execution and one-frame windows in low-memory mode.

Media Composer 2025.12.0 loaded both CorridorKey diagnostic builds. The single-input diagnostic displayed all three bands as opaque. The two-input diagnostic rendered 0%, 50%, and 100% opacity directly over V1. The inference build rendered one full-resolution frame from each plate over V1, rerendered both after replacing V1, and played the saved sequence after reopening the project. Model inference took 16.2 seconds for the green frame and 31.2 seconds for the blue proxy on that rerender, excluding model load. Background patches sampled in the reopened Avid monitors were within four 8-bit display code values per channel. Missing assets and an interrupted worker failed the render while leaving Media Composer usable, and both frames rendered on retry. The missing-assets notification was verified on screen. Media Composer also imported the ProRes 4444 fixture as a Matte Key.

The **CorridorKey 10-bit Ramp** sequence in Scratch 24FPS uses a 10-bit Rec.709 ramp on V1 and a 10-bit green plate on V2. With **Effects Processing** set to **Automatic** and **Same as Source** enabled, Avid requested 2.14 RGB for both inputs and the render output. The resulting DNxHD precompute decoded as 10-bit Rec.709. Of 728 patch centers sampled in the uncovered left 520 pixels, the imported background had 606 distinct luma levels and the render retained 602. Every sampled patch was within one 10-bit code value of the imported background. The same configuration supplied 8-bit RGB for a 960×540 preview.

## Build and validate in Avid

Install Xcode's command-line tools and set up your AVX2 development environment through [Avid Developer](https://developer.avid.com/). Set `avid_development_root` to its local installation path.

```sh
cmake -S plugin -B plugin/build/macos-keyer \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 -DAVX2SDK_ROOT="$avid_development_root"
cmake --build plugin/build/macos-keyer
```

The build produces an ad hoc signed `bin/CorridorKey.avx` bundle.

With Media Composer closed, install `CorridorKey.avx` in `/Library/Application Support/Avid/AVX2_Plug-Ins` and reopen Avid. Apply **Corridor Key 2.0** from **Key** to a foreground clip on V2 above the background on V1. Use **Timeline > Render > Render at Position** for an offline render. The effect shows the original foreground when Avid requests an interactive preview. Media Composer chooses between 8-bit RGB and normalized signed 16-bit RGB (2.14 fixed point). Both paths transport float RGB and alpha between the effect and worker. The plug-in targets progressive 1080p SDR Rec.709 and renders one frame at a time. Playback uses Avid's rendered media.

**View** in the Effect Editor selects Composite over V1, a grayscale Matte, or the Original V2 plate for the next render. Original bypasses the worker. **Despill Amount** defaults to 50% and **Despill Color** defaults to green, matching the original Mac render. Open the native RGB swatch to select the cast to suppress. Despill changes foreground color after inference and preserves the matte. Render to see the result after adjusting a setting. In the 10-bit ramp sequence, Avid registered a new RGB sample in the existing effect instance and rendered the composite with that setting.

To check the host without loading the model, build in a fresh directory with `-DCORRIDORKEY_AVX_SYNTHETIC=ON` and apply **CorridorKey Composite Test** from **Key**. The result should show V1 on the left, a 50% blend in the middle, and solid orange on the right. Add `-DCORRIDORKEY_AVX_PROBE=alpha` to test single-input output alpha with **CorridorKey Alpha Test** from **Image**. Install one bundle at a time with Media Composer closed.

## Test the pop-out dialog inputs

Use the macOS build command above with a fresh build directory and `-DCORRIDORKEY_AVX_PREVIEW_PROBE=ON`. This produces `CorridorKeyPreviewProbe.avx`, which can be installed beside `CorridorKey.avx`. The probe uses the two-input Key effect and synthetic matte without loading the model.

On a test sequence, apply **Key → CorridorKey Timeline Input Test** to V2 with visibly different footage on V1. Park on a frame where both clips have visible content and click **Check Timeline Inputs** in the Effect Editor. Verify that the left thumbnail shows the V1 background and the right thumbnail shows the V2 foreground. The dialog also reports the dimensions, pixel format, and AVX image call used for each input.

To inspect the host's result from the terminal, run `/usr/bin/log show --last 10m --predicate 'eventMessage BEGINSWITH "CorridorKey preview probe frame"'`.

**Pending**

- [ ] Verify in Apple Silicon Media Composer that the dialog receives distinct V1 background and V2 foreground frames from the same timeline position. The arm64 bundle registers with Avid’s command-line cache tool and the callback passes with test inputs.
- [ ] Once the Mac test passes, build the viewer with shared C++ frame and preview logic, using CMake to select the native dialog and inference backend. Reuse the Windows worker where it fits and verify the Windows build with Visual Studio and Media Composer.

## Distribution

See the [release plan](releases.md) for distribution and validation requirements.
