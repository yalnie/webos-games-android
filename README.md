# Mainstream on Android

Android compatibility layer and port of the webOS native game **Mainstream** (`com.ru.lgerp.mainstream`).

## Overview
- **Native Engine Loader**: Loads and runs the original webOS 32-bit ARM ELF game binary via an SDL2 wrapper for Android (`armeabi-v7a`).
- **Graphics & Audio**: Hardware-accelerated OpenGL ES 2.0 rendering and SDL2_mixer audio playback.
- **Controls**: Touchscreen and Android TV remote / pointer navigation.
- **Save Data & Offline**: Automatic asset unpacking and local offline save management.

## Building

```bash
./gradlew assembleDebug
```
The resulting APK will be generated at `app/build/outputs/apk/debug/app-debug.apk`.

Automated builds are also generated via **GitHub Actions** on every push to this branch.

## Disclaimer

> **Legal Notice:** This project is an independent, non-commercial fan effort created for preservation and compatibility purposes. 
> 
> * **Ownership:** The original game, including all associated assets (graphics, audio, and proprietary scripts), is the intellectual property of **LG RUSSIA R&D Lab**. This repository does not claim any ownership over these assets.
> * **Purpose:** This project provides an Android compatibility layer (wrapper) to allow the game to run on modern Android and Android TV hardware. It is not intended for commercial distribution or profit.
> * **No Affiliation:** This project is not affiliated with, authorized, maintained, sponsored, or endorsed by LG RUSSIA R&D Lab or any of its affiliates.
> * **Liability:** The software is provided "as is", without warranty of any kind. The maintainers are not responsible for any misuse of this project or potential copyright issues arising from the distribution of game assets.

## License

This project's wrapper code is licensed under the **MIT License**. See the `LICENSE` file for details. Game assets are excluded from this license.
