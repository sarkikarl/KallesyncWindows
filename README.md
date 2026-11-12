# KalleSync (Windows Client)

> ⚠️ **Work in progress.** KalleSync is not finished. Expect bugs, hardcoded values, missing features, and breaking changes.

> 📦 **This repository is only half of the project.** It contains the **Windows client**. KalleSync also needs a separate **Linux host/server** component (screen capture, input injection, session login), which is **not included here**. This client won't do anything useful without it.

## What it is

KalleSync is a low-latency remote desktop tool. It streams a Linux machine's screen to a Windows PC over the local network and sends the Windows keyboard and mouse back to Linux.

The Windows client:

- Shows the Linux desktop in a Direct3D 11 window (windowed or fullscreen).
- Receives JPEG-compressed frames over UDP using IOCP and decodes them with libjpeg-turbo.
- Captures the keyboard and mouse with low-level Windows hooks, maps keys to Linux keycodes, scales mouse coordinates, and sends input at ~120 Hz.
- Exchanges telemetry and status with the Linux side (thread states, frame times, dropped frames, errors).
- Authenticates with a challenge/response handshake and encrypts traffic with per-session keys.
- Has an audio receive path that is still experimental.

## How it works

```
 Windows (this repo)                         Linux (separate, not included)
 ───────────────────                         ──────────────────────────────
 Prompt for shared password + Linux login
 "KALLE" hello ──────────────────────────►   listen on handshake port
            ◄──────────────────────────────  32-byte random challenge
 HMAC response + encrypted credentials ──►   verify HMAC, PAM login,
                                             start desktop session
            ◄──────────────────────────────  config: ports + session keys

 Viewport  ◄── encrypted JPEG frames (UDP) ── screen capture
 KBM hook  ─── encrypted keyboard/mouse ───►  input injection
 Telemetry ◄─────────── status ───────────►   status
```

## Project layout

| Path | Purpose |
|------|---------|
| `KalleSync/Kallesync.cpp` | Entry point, window, D3D11 setup and rendering |
| `KalleSync/udpViewPort.cpp` | IOCP UDP receiver, frame reassembly, decrypt, JPEG decode |
| `KalleSync/kbm.cpp` | Keyboard/mouse hooks and Windows-to-Linux key mapping |
| `KalleSync/VeiwportTracking.cpp` | Tracks the viewport window for mouse mapping |
| `KalleSync/Telementry.cpp` | Status/telemetry exchange with Linux |
| `KalleSync/audio.cpp` | Audio receive and playback (experimental) |
| `KalleSync/config.cpp`, `temp.cpp` | Startup prompts and handshake |
| `KalleSync/protocol.h` | Wire structs and enums shared with the Linux side |
| `KalleSync/packet_pipeline.*`, `key_manager.hpp`, `rolling_key.hpp` | Packet handling and key management |
| `crypto/` | Crypto helpers, handshake definitions, and `setup_auth.cpp` |

## Building

**Requirements**

- Windows 10/11, x64
- Visual Studio with the C++ desktop workload (platform toolset v145)
- [libjpeg-turbo](https://libjpeg-turbo.org/) (`turbojpeg.lib` / `turbojpeg.dll`), for example through vcpkg

**Steps**

1. Open `KalleSync.slnx` in Visual Studio.
2. Select **Release | x64**.
3. Build. Make sure `turbojpeg.dll` is next to `KalleSync.exe`.

## Setup and usage

1. **On the Linux machine**, create the shared auth key:
   ```bash
   g++ -o setup_auth crypto/setup_auth.cpp && ./setup_auth
   ```
   This writes `auth.key`, which the Linux host loads at startup. The password itself isn't stored.
2. Start the Linux host (from the separate Linux project).
3. **On Windows**, run `KalleSync.exe`. A console asks for:
   - the shared KalleSync password
   - the Linux username and password for the desktop session
4. Controls:
   - `F11` or `Alt+Enter`: toggle fullscreen
   - `Esc`: leave fullscreen

## Known issues / TODO

- **Hardcoded settings.** The Linux IP (`192.168.1.69`), handshake ports (55070/55071) and the 1920×1080 resolution used for mouse scaling are set in `config.cpp`. There's no config file or UI yet.
- **Scaling and blank screen bugs.** Changing resolution or scaling on the Linux side can cause a blank or white screen or wrong mouse mapping. A fix is in progress.
- Audio is experimental and may not work.
- Cursor streaming isn't implemented yet (the struct is reserved in `protocol.h`).
- Little error recovery: if the connection drops, the client has to be restarted.
- No installer and no release builds yet.
- The Linux host isn't published in this repo.

## Status

This is an early, unfinished personal project. Feedback is welcome, but it isn't ready for real use yet.
