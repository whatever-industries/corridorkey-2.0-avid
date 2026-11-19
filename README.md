# CorridorKey 2.0 for Avid Media Composer

Community plug-in for green- and blue-screen keying with [CorridorKey 2.0](https://github.com/nikopueringer/CorridorKey2.0). Inference runs locally on Apple Silicon or NVIDIA GPUs.

## Platform status

| Platform | Backend | Status |
|---|---|---|
| macOS · Apple Silicon | PyTorch MPS on Metal | Tested in Media Composer 2025.12 with 1080p SDR Rec.709 compositing and 10-bit render output. [Download preview](https://github.com/whatever-industries/corridorkey-2.0-avid/releases/tag/v0.1.0) · [Setup](docs/macos.md) |
| Windows · x64 | NVIDIA CUDA | Based on the [community Windows port](https://github.com/ber2minsin/corridor-key-v2-avid). A native build and a CUDA render in Media Composer are pending for this repository’s version. [Setup and controls](docs/windows.md) |

See the [release plan](docs/releases.md) for the platform package checklist.

## Use in Media Composer

On **Mac**, place the plate on V2 above the background on V1. Apply **Corridor Key 2.0** from **Effect Palette → Key** and open the **Effect Editor**. Choose **Composite**, **Matte**, or **Original**. Use **Despill Amount** and the **Despill Color** swatch to adjust the foreground cast. Render at position to see the keyed frame.

On **Windows**, apply the effect from **Effect Palette → Image**. The Windows effect exposes its own output, alpha hint, and temporal controls. [Windows workflow](docs/windows.md)

The model loads on first render. Mac renders can reuse the cached foreground and matte for background swaps and despill adjustments.

## Build from source

Sign up at [Avid Developer](https://developer.avid.com/) and set up Avid’s development environment. For this checkout, see the platform instructions above.

```sh
git clone --recurse-submodules https://github.com/whatever-industries/corridorkey-2.0-avid.git
cd corridorkey-2.0-avid
git -C CorridorKey2.0 apply ../runtime/patches/corridorkey-2.0.patch
```

The submodule pins the upstream baseline. The patch reproduces the tested runtime. The [upstream PR ledger](docs/corridorkey-upstream.md) records the exact revisions and integration choices.

## License and credits

The Avid integration is available free of charge under its [license notice](LICENSE). The CorridorKey engine, runtime patch, and weights are governed by the [CorridorKey 2.0 license](licenses/CorridorKey-2.0.txt), which includes CC BY-NC-SA 4.0 and Corridor Digital’s additional terms. The license allows commercial use of generated footage under its stated conditions and requires a separate agreement for commercial software integration. Model assets come from their respective publishers. [Third-party notices](licenses/CorridorKey-third-party.md)

CorridorKey 2.0 is by Corridor Digital and its contributors. This project builds on the [Windows Avid port by ber2minsin](https://github.com/ber2minsin/corridor-key-v2-avid). Avid and Media Composer are trademarks of Avid Technology, Inc. This is an independent community project.
