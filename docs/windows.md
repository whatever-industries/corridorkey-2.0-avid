# Windows x64

The Windows effect runs local inference on an NVIDIA GPU. It uses the community Windows port’s CUDA worker and targets Windows 10 22H2 or Windows 11 with an NVIDIA Studio or Game Ready driver. Windows packaging and host validation for this repository are pending.

## Build and use

Set up your account and development environment through [Avid Developer](https://developer.avid.com/). Install Visual Studio 2022 with C++ desktop development, CMake, and Git. Follow the [repository setup](../README.md#build-from-source), including the pinned runtime patch, then run the following in PowerShell.

```powershell
$env:AVX2SDK_ROOT = 'C:\path\to\Avid-development-files'
.\scripts\build_plugin.ps1
```

For a local test installer, install Inno Setup 6 and run `scripts\build_installer.ps1`. The installer fetches CUDA dependencies and model assets during setup. See the [release plan](releases.md) before distributing any installer.

In Media Composer, apply **Corridor Key 2.0** from **Effect Palette → Image** to a green- or blue-screen clip. Open **Effect Editor**.

| Control | Values |
|---|---|
| Mode | Auto or External Hint from the clip’s alpha channel |
| Output | Processed over black, grayscale Matte, Foreground RGB, or Checkerboard |
| Despill / Unmix | 0–100% |
| Colorspace | sRGB or Linear |
| Time Awareness | Analyze neighboring frames |
| Temporal Window | 1–80 frames |
| Hint Quality | 0–100% |
| Carry Hint Alpha | Reuse hint alpha across temporal windows |

For the external hint, load a clip or nest whose alpha contains a grayscale guide matte. A flat opaque alpha uses automatic keying.

Logs are in `%LOCALAPPDATA%\CorridorKeyAVX\logs`.

[See the Windows Effect Editor](images/effect-editor.png)
