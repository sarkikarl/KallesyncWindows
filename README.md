# KalleSync Windows Client

Low-latency remote desktop client that streams a Linux desktop to a Windows PC over the local network, with keyboard and mouse input sent back at ~120 Hz.

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Direct3D 11](https://img.shields.io/badge/Direct3D-11-orange.svg)](https://docs.microsoft.com/en-us/windows/win32/direct3d11/atoc-dx-graphics-direct3d-11)

## Why This Exists

Most remote desktop tools are built for convenience.  
KalleSync is built for **latency** — the gap between moving your mouse on the Windows side and seeing the cursor move on the Linux side.

It targets local-network use cases where you want near-instant response: controlling a headless Linux box, a lab machine, or a build server from a Windows workstation.

## Architecture

```
Linux Server                          Windows Client
─────────────                         ──────────────
Screen capture                        IOCP receive thread
(X11 / DRM)  ──JPEG over UDP──►       ├── libjpeg-turbo decode
                                      └── Direct3D 11 render

                                      Low-level keyboard + mouse hooks
                                      (~120 Hz)
                                      └── UDP send thread
                                          (mouse coord scaling +
                                           Win32 → Linux key mapping)
```

**Receive path**  
An I/O completion port thread pulls UDP packets, reassembles JPEG frames, decodes them with libjpeg-turbo, and presents them in a Direct3D 11 swap chain.

**Input path**  
Low-level Windows hooks capture keyboard and mouse events, translate Windows virtual-key codes to Linux keycodes, scale mouse coordinates to the remote resolution, and fire them back over UDP at roughly 120 Hz.

**Security**  
A connection starts with an HMAC challenge-response handshake. On success, both sides derive a per-session encryption key so every frame and every input packet is authenticated and encrypted for that session only.

## Features

- Direct3D 11 rendering window (no GDI, no intermediate bitmaps)
- UDP transport with IOCP for overlapped I/O
- libjpeg-turbo decoding (fast, SIMD-accelerated)
- Low-level keyboard & mouse hooks with Win32 → Linux keycode mapping
- Mouse coordinate scaling between local and remote resolutions
- HMAC challenge-response authentication + per-session encryption keys
- Telemetry: per-frame timing, dropped-frame counters, thread state, and error reporting

## Performance Notes

- Frame decode-to-present is the critical path. libjpeg-turbo’s SIMD path keeps decode under ~2 ms for 1080p at quality 85 on modern hardware.
- IOCP avoids the overhead of one thread per connection and scales cleanly if you later multiplex multiple streams.
- Input is sent at 120 Hz regardless of render rate, so typing and cursor movement feel immediate even when the video is capped at 60.

## Telemetry

The client logs structured telemetry you can use for debugging or post-mortems:

- **Frame times** — decode latency, present latency, end-to-end
- **Dropped frames** — packets lost or arriving too late to composite
- **Thread states** — which IOCP / render / input threads are alive or stalled
- **Errors** — handshake failures, decrypt errors, device-lost Direct3D conditions

This is the data you’d pull when someone asks “what went wrong at 2 AM.”

## Roadmap

- [ ] Adaptive bitrate (drop JPEG quality under packet loss instead of dropping frames)
- [ ] Audio forwarding (Opus over a second UDP channel)
- [ ] Clipboard sync between Windows and Linux
- [ ] Multi-monitor support on the client side
- [ ] Linux server component so the whole stack lives in one repo

## Build

```bash
git clone https://github.com/sarkikarl/KallesyncWindows.git
cd KallesyncWindows
```

Open `KalleSync.sln` in Visual Studio 2022 (or newer), set the solution platform to **x64**, and build.

Dependencies (`libjpeg-turbo`, Windows SDK) are expected to be installed locally or vendored — see the project properties for include/lib paths.

## License

TBD
```
