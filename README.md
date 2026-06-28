# stream-app

Experimental low-latency screen streaming prototype written in C++.

The current implementation captures the Windows desktop with FFmpeg `gdigrab`,
encodes frames to H.264, sends them as MPEG-TS over UDP, and renders the stream
in a small GLFW/OpenGL client window.

## Status

This is an experiment, not a production streaming stack. The repository is kept
small on purpose so the capture, encode, transport, decode, and render path is
easy to inspect.

Implemented:

- Windows desktop capture through FFmpeg
- H.264 encoding with low-latency options
- MPEG-TS over UDP transport
- FFmpeg-based client decoder
- GLFW/OpenGL frame rendering

Not implemented yet:

- WebRTC signaling or peer-to-peer transport
- Audio capture
- Packet loss recovery
- Authentication or encryption
- Cross-platform desktop capture defaults

## Requirements

- CMake 3.14 or newer
- A C++17 compiler
- FFmpeg development headers and libraries
- GLFW source in `lib/glfw`

This repository currently vendors GLFW and a Windows FFmpeg development package
under `lib/`. If you prefer system packages, adjust `FFMPEG_ROOT` in
`CMakeLists.txt`.

## Build

From the repository root:

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

If you use vcpkg or another toolchain file, pass it from the command line:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Debug
```

## Run

Start the server first:

```powershell
.\build\Debug\server.exe
```

Then start the client:

```powershell
.\build\Debug\client.exe
```

Useful server options:

```powershell
.\build\Debug\server.exe --host 127.0.0.1 --port 9000 --fps 30 --bitrate 800000
.\build\Debug\server.exe --output udp://127.0.0.1:9000
```

Useful client options:

```powershell
.\build\Debug\client.exe --url udp://127.0.0.1:9000?fifo_size=5000000
```

## Roadmap Ideas

- Add a small signaling server and replace raw UDP with WebRTC.
- Move FFmpeg resources into RAII wrappers.
- Add a frame timing model instead of a fixed client-side sleep.
- Add capture source selection and resolution controls.
- Add a minimal smoke test that validates decoder initialization against a
  generated test stream.
