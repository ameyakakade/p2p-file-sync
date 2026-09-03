# P2P File Sync

**Sync files over local network using peer-to-peer architecture.**

A decentralized file synchronization solution for seamless file sharing and synchronization across devices on a local network.

## Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Building](#building)
- [Usage](#usage)
- [Project Structure](#project-structure)
- [Contributing](#contributing)
- [License](#license)

## Features

- **Peer-to-Peer Architecture**: Direct file transfers between devices without a central server
- **Local Network Sync**: Efficient synchronization across LAN
- **Cross-Platform**: Built with C++ for broad compatibility
- **Decentralized Design**: No single point of failure
- **GUI Interface**: User-friendly interface powered by ImGui

## Requirements

- **C++ Compiler** (C++11 or later)
  - GCC/Clang on Linux/macOS
  - MSVC on Windows
- **CMake** or compatible build system
- **nob.h**: Single header C++ build system (included in the project)
- **SDL2**: Cross-platform multimedia library
- **ImGui**: Bloat-free GUI library

## Building

This project uses [nob.h](https://github.com/tsoding/nob.h), a minimalist build system for C/C++ projects.

### Quick Start

```bash
# Compile the build system
g++ -o nob nob.cpp

# Build the project
./nob

# Rebuild from scratch
./nob rebuild
```

### Platform-Specific Instructions

**Linux/macOS:**
```bash
g++ -o nob nob.cpp
./nob
```

**Windows (PowerShell):**
```powershell
cl.exe nob.cpp /Fe:nob.exe
.\nob.exe
```

**Windows (MinGW):**
```bash
g++ -o nob.exe nob.cpp
./nob.exe
```

## Usage

After building, run the application:

```bash
./p2p-file-sync
```

### Basic Workflow

1. Launch the application
2. The GUI will display available peers on the local network
3. Select files to sync
4. Choose target peer(s)
5. Monitor sync progress through the interface

## Project Structure

```
p2p-file-sync/
├── nob.cpp                 # Build system configuration
├── README.md              # This file
├── src/                   # Source code (C++)
├── thirdparty/
│   ├── imgui/            # GUI library
│   └── sdl2_win/         # SDL2 dependencies (Windows)
└── build/                # Build output (generated)
```

### Key Components

- **Network Layer**: P2P communication protocol
- **File Transfer**: Efficient file synchronization engine
- **GUI**: ImGui-based user interface for peer management and file operations

## Dependencies

### Core Dependencies
- **nob.h**: Build configuration system
- **SDL2**: Window management and input handling
- **ImGui**: Immediate mode GUI toolkit

### Language Composition
- C++: 89.6%
- C: 10.4%

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## Troubleshooting

### Build Fails
- Ensure your C++ compiler is up to date (C++11 or later)
- Verify all dependencies are installed
- Check that nob.cpp is in the project root directory

### Runtime Issues
- Confirm devices are on the same network
- Check firewall settings allow P2P connections
- Review the logs for detailed error messages

## License

This project is open source and available under the MIT License.

## References

- [nob.h Documentation](https://github.com/tsoding/nob.h)
- [ImGui Repository](https://github.com/ocornut/imgui)
- [SDL2 Documentation](https://www.libsdl.org/)

---

For questions or issues, please open an issue on the GitHub repository.
