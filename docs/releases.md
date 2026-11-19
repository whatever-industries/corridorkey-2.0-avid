# GitHub releases

Use a single tag for the source and both platform packages. The tag identifies the Avid integration version. CorridorKey remains the 2.0 engine.

The proposed first release is `v0.1.0`. Use the platform table from [Disc Xplorer](https://github.com/whatever-industries/disc-xplorer/releases/latest) as the release-note format. Link each platform row to the verified asset for that tag.

## Download

| Platform | Package |
|---|---|
| **Windows (x64)** | `CorridorKey.Avid_Windows_x64_v0.1.0.exe` |
| **macOS (Apple Silicon)** | `CorridorKey.Avid_macOS_ARM_v0.1.0.zip` |

## Before publishing packages

- Confirm Avid’s distribution terms cover the compiled plug-ins. The source repository links readers to [Avid Developer](https://developer.avid.com/).
- Confirm CorridorKey’s terms for the planned package layout. Keep the packages free, include the applicable license and notices, and obtain model assets from the original publishers during setup. The inherited Windows installer also bundles the upstream engine source. Review that layout before distributing it.
- On Mac, test a fresh install, render the green and blue-proxy fixtures, and check the 10-bit output and native color swatch. On Windows, build with Visual Studio on x64, run the installer on a CUDA machine, and check the effect, an offline render, and uninstall.
- Upload only packages that passed those checks. Attach SHA-256 checksums and include the tested Avid versions in the release notes.
