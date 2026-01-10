# DLL Overlay / Hook

DX11 + MinHook DLL to capture the local player's world position and render a minimal ImGui overlay.

## Features
- DX11 Present/Resize hooks and Win32 WndProc hook (`Insert` toggles overlay visibility).
- PTTRPlayer lifecycle hooks (Awake/OnEnable/Update/OnDestroy/Die) auto-capture the local player.
- Position read path: `PTTRPlayer::GetTargetPoint` first, fallback to `Transform::get_position` (self then controller), all wrapped in SEH.
- Logging to `D:\Project\overlay_log.txt` for hook state and any read exceptions.
- ImGui render safety checks to avoid draw-data assertions/crashes.

## Build
1. Open `DLL/DLL.vcxproj` (target name already set to `DLL.dll`).
2. Select configuration/platform (Debug/Release, x86/x64).
3. Build; outputs go to `bin/<arch>/<config>/DLL.dll` with `DLL.pdb` symbols.

## Usage
- Inject the matching-arch `DLL.dll` into the game process.
- Swapchain hooks are set automatically (dummy swapchain used if needed).
- Press `Insert` to show/hide the overlay. Window displays FPS and local player XYZ.
- Check `D:\Project\overlay_log.txt` for diagnostics.

## Notes
- Feet read is disabled by default; Transform/TargetPoint paths have SEH guards and will log on failure.
- If positions stop updating or a crash occurs, inspect the tail of `overlay_log.txt` for `GetTargetPoint`/`Transform_get_position` entries and share if further tuning is needed.
