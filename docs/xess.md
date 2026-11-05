# Test Intel XeSS Super Resolution on Windows

Use the XeSS test build of TotK Recomp and your own game files. XeSS is off by default. Start with game update 1.4.3, a
1920×1080 game resolution, and HDR off. This gives the Native AA test a fixed-size input and makes it easy to compare
the two modes at the same output resolution.

1. Run `TotK-Setup.exe` from the XeSS test kit and install to a separate game folder. The setup compiles the game on your PC.
2. Copy `extras/Intel-XeSS/libxess.dll` from the kit next to `totk.exe`. For a source build, run
   `python tools/fetch_xess.py --dest <game-folder>` from the checkout to download and verify the same DLL and license.
   Intel's SDK uses the [Microsoft Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe).
3. Launch the game, press **F1**, and find **Intel XeSS Super Resolution (experimental)** in the graphics settings. Select
   **Native AA (100%)** and restart. Alternatively, set `xess = native` in the `[display]` section of `totk.ini`.
4. Load a save and move the camera and Link. In `logs/totk.log`, look for `xess: initialized` and `xess: 1 frames resolved`.
   The second line confirms that the game reached the XeSS pass. Compare with XeSS **Off** after a restart.
5. Try **Ultra Quality**, then **Quality** or **Balanced**. Those modes render the scene below the output resolution.
   Check the HUD, grass and moving objects, then a teleport or a loading screen. Compare GPU time in the F10 overlay
   at the same output resolution and frame cap. `xess = off` restores the default path after a restart.

The Windows kit contains a clean runtime rebuild of TotK Recomp 0.3.11. It uses the original installer's tooling and
the official Intel DLL from [XeSS SDK 3.0.2](https://github.com/intel/xess/releases/tag/v3.0.2). The DLL itself reports
the XeSS-SR component version. Build the runtime from source with the usual [TotK build instructions](building.md).

To check the SDK and GPU independently of gameplay, run the following from the game folder in PowerShell:

```powershell
$env:TOTK_XESS = 'quality'
.\totk.exe --probe-xess
Remove-Item Env:TOTK_XESS
```

`--probe-xess` submits a synthetic frame through the same XeSS code as the game and reads back its center pixel. It
checks the SDK, driver, image formats and output. In-game motion, HUD, HDR and frame pacing need a play test.
