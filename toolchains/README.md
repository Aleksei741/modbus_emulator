# Toolchain Workflows

This project uses one host toolchain:

- `windows-ucrt64`: `x64-windows` builds inside a local MSYS2 `UCRT64` environment.

## Build Directory

Scripts default to the VS Code setting `toolchains.buildDir`:

- `${workspaceFolder}/toolchain_build`

That directory holds:

- local `msys64` installation
- extracted MSYS2 installer files
- metadata in `toolchain-windows-ucrt64`

## Windows x64

1. Download the MSYS2 x86_64 installer archive separately and save it locally as `toolchains/third_party/msys2-x86_64-latest.7z`. Do not commit the archive.
2. Run `toolchains/toolchains/windows-ucrt64/setup_windows_ucrt64.ps1`
3. Run `toolchains/library/windows-ucrt64/install_libraries_windows_ucrt64.ps1`
4. From the project root, run `python -m pip install Pillow` and `python assets/make_art.py`. This generates the local icon required by both apps; the generated PNGs are not tracked.
5. Run `building/windows-x64/build_emulator.ps1`

One-shot setup (steps 2–3) is available via:

- `toolchains/build_windows_toolchain.ps1`
- VS Code task **Toolchains: Build Everything**

The setup script writes metadata into:

- `<buildDir>\toolchain-windows-ucrt64`

MSYS2 is installed under:

- `<buildDir>\msys64`

The Windows dependency set is installed with `pacman` into the `UCRT64` environment and currently includes:

- `mingw-w64-ucrt-x86_64-gcc`
- `mingw-w64-ucrt-x86_64-gdb`
- `mingw-w64-ucrt-x86_64-cmake`
- `mingw-w64-ucrt-x86_64-ninja`
- `mingw-w64-ucrt-x86_64-pkgconf`
- `mingw-w64-ucrt-x86_64-boost` (Boost.Asio for the COM/RTU transport)
- `mingw-w64-ucrt-x86_64-wxwidgets3.2-common`, `...-msw` and `...-msw-libs` (desktop GUI)
- `mingw-w64-ucrt-x86_64-nlohmann-json` (settings and register map)

## Proxy Settings

The Windows proxy is used only for `pacman` downloads, not for application builds.

- `toolchains.proxy.windows` (`http://localhost:10082`) — MSYS2 `pacman`
