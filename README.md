# Stacky

Stacky is a small Windows utility that displays the contents of a folder as a popup list of clickable icons. It is useful for grouping related app shortcuts into a single taskbar icon.

## Fork Status

This repository is a maintained fork of the original [`pawelt/stacky`](https://github.com/pawelt/stacky) project.

The upstream project README explains that the original project is no longer maintained. This fork keeps the app working on a modern Windows / Visual Studio toolchain and carries a small set of behavior fixes.

## Changes In This Fork

This fork currently includes:

- removal of the top `Open: <stack path>` row from the activated stack menu
- corrected menu command handling after hiding that row
- modernized Visual Studio project settings for the VS 2022 `v143` toolset
- GitHub Actions build automation for `Release | Win32`
- shortcut-launch fixes for modern Windows, including Office shortcut resolution from the 32-bit build

## Build Artifacts

The historical `binaries/` directory is part of the original project history, but it should not be treated as the current distribution channel for this fork.

For this fork, the expected way to get a fresh build is:

1. build locally from `vsproj/stacky.sln`, or
2. download the `stacky-release-win32` artifact from the latest successful GitHub Actions run

## Building

### Local build

Open `vsproj/stacky.sln` in Visual Studio 2022 and build:

- `Configuration`: `Release`
- `Platform`: `Win32`

The project is currently configured for the `v143` platform toolset.

Expected output:

```text
vsproj\x86\Release\stacky.exe
```

### CI build

This repository includes a GitHub Actions workflow at `.github/workflows/build-stacky.yml` that:

- checks out the repository
- locates MSBuild with `vswhere`
- builds `vsproj\stacky.sln` as `Release | Win32`
- uploads `stacky.exe` as the `stacky-release-win32` artifact

## How To Use It

1. Create a normal Windows shortcut to `stacky.exe`.
2. Create a folder and place shortcuts to your programs in that folder.
3. Edit the Stacky shortcut and append the stack folder path to the `Target` field, for example:

   ```text
   D:\Programs\Stacky\stacky.exe D:\Stacks\Games
   ```

4. Pin that shortcut to the taskbar.

Clicking the shortcut opens the stack menu for that folder.

## Why Use It

Stacky lets you replace a row of separate taskbar shortcuts with one grouped launcher.

For example, instead of pinning Excel, Outlook, PowerPoint, Teams, and Word separately, you can place those shortcuts in one folder and pin a single Stacky shortcut that opens them as a menu.

The app is also useful for stacks of project folders, document folders, media folders, and other frequently used locations.

## Why Stacky Feels Fast

Stacky caches stacked icons in a hidden cache file inside the stack folder. That means it does not need to re-read every `.lnk` file and extract every icon every time the menu opens.

When the stack folder changes, the cache is rebuilt automatically.

That cache-first approach is why Stacky opens immediately even when a folder contains multiple shortcuts.

## Repository Notes

- Source code lives in `src/`
- Visual Studio project files live in `vsproj/`
- CI workflow lives in `.github/workflows/build-stacky.yml`

## Upstream Credit

Full credit for the original project goes to the upstream Stacky author and contributors. This fork keeps the project usable for current Windows setups while preserving the original app concept and implementation.
