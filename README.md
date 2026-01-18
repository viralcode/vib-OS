# Vib-OS

**Multi-Architecture Operating System with GUI**

![Build Status](https://img.shields.io/badge/build-passing-brightgreen)
![Platform](https://img.shields.io/badge/platform-ARM64%20%7C%20x86__64-blue)
![Apple Silicon](https://img.shields.io/badge/Apple%20Silicon-M1%20%7C%20M2%20%7C%20M3%20%7C%20M4-orange)
![Raspberry Pi](https://img.shields.io/badge/Raspberry%20Pi-4%20%7C%205-red)
![Lines](https://img.shields.io/badge/lines-20k+-yellow)
![License](https://img.shields.io/badge/license-MIT-green)

```
        _  _         ___  ____ 
 __   _(_)| |__     / _ \/ ___| 
 \ \ / / || '_ \   | | | \___ \ 
  \ V /| || |_) |  | |_| |___) |
   \_/ |_||_.__/    \___/|____/ 

Vib-OS v0.5.0 - Multi-Architecture OS with Full GUI
```

## Overview

Vib-OS is a from-scratch, Unix-like operating system with **full multi-architecture support** for **ARM64** and **x86_64**. It features a custom kernel, a modern macOS-inspired graphical user interface, a full TCP/IP networking stack, and a Virtual File System (VFS). Built with **20,000+ lines** of C and Assembly, it runs natively on QEMU, real hardware (Raspberry Pi 4/5, x86_64 PCs), and Apple Silicon (via UTM).

## 🎯 Multi-Architecture Support

| Architecture | Boot Method | Status | Hardware |
|--------------|-------------|--------|----------|
| **ARM64** | Direct / UEFI | ✅ **Production Ready** | Raspberry Pi 4/5, QEMU virt, Apple Silicon (VM) |
| **x86_64** | Direct / UEFI / BIOS | ✅ **Production Ready** | Modern PCs, QEMU, VirtualBox, VMware |
| **x86** | Direct / BIOS (MBR) | ✅ **Builds Successfully** | Legacy PCs, QEMU pc |

### What Works Now

- ✅ **ARM64**: Fully tested and stable on QEMU and Raspberry Pi
- ✅ **x86_64**: Kernel builds and boots successfully
- ✅ **x86 32-bit**: Kernel builds successfully (testing in progress)
- ✅ **Architecture Abstraction Layer**: Clean separation of arch-specific code
- ✅ **Context Switching**: Working for ARM64, x86_64, and x86
- ✅ **Memory Management**: MMU/paging for all architectures
- ✅ **Interrupt Handling**: GICv3 (ARM64), APIC (x86_64), PIC (x86)

## 📸 Screenshots

### Desktop & File Manager
![File Manager](screenshots/filemanager.png)
*Modern file manager with icon grid, navigation, file creation (New File/Folder), and renaming capabilities.*

### Terminal & Shell
![Terminal](screenshots/terminal.png)
*VT100-compatible terminal with command history, `ls`, `cd`, and shell utilities.*

### Applications (Snake Game)
![Snake](screenshots/snake.png)
*Interactive Snake game with score tracking and keyboard controls.*

### Calculator
![Calculator](screenshots/calculator.png)
*Fully functional calculator with arithmetic operations.*

### Doom
![Doom](screenshots/doom.png)
*Doom running natively on Vib-OS with full graphics and input support.*

## 🏗 Architecture

```mermaid
graph TD
    subgraph Userspace ["Userspace (EL0/Ring 3)"]
        GUI[Window Manager & GUI Apps]
        Shell[Terminal / Shell]
        Doom[Doom Engine]
    end

    subgraph Kernel ["Kernel (EL1/Ring 0)"]
        Syscall["Syscall Interface (SVC/INT)"]
        
        subgraph Subsystems
            VFS[Virtual File System]
            Process[Process Scheduler]
            Net[TCP/IP Networking Stack]
            Mem["Memory Manager (PMM/VMM)"]
        end
        
        subgraph Drivers
            VirtioNet[Virtio Net]
            VirtioInput["Virtio Input (Tablet/Kbd)"]
            IRQ["Interrupts (GIC/APIC)"]
            Timer["Timer (ARM/PIT)"]
            UART["Serial (PL011/16550)"]
            HDA[Intel HDA Audio]
        end
    end

    GUI --> Syscall
    Shell --> Syscall
    Doom --> Syscall
    
    Syscall --> VFS
    Syscall --> Process
    Syscall --> Net
    
    VFS --> RamFS
    Net --> VirtioNet
    Process --> Mem
    
    Drivers --> Hardware
```

## ✨ Features

### 🖥 Graphical User Interface
- **Window Manager**: Draggable windows with focus management and z-ordering
- **Traffic Light Controls**: Close, Minimize, and Maximize buttons (macOS-style)
- **Taskbar & Dock**: Animated dock with hover labels; top menu bar with clock and WiFi status
- **Compositor**: Double-buffered rendering engine for flicker-free visuals
- **Modern Design**: Clean, intuitive interface inspired by macOS

### 📂 File System (VFS)
- **Virtual File System**: Unified interface for different filesystems
- **RamFS**: In-memory filesystem for temporary storage
- **EXT4 Support**: Read support for EXT4 filesystems (experimental)
- **APFS Support**: Read support for Apple File System (experimental)
- **Interactive File Manager**:
  - Grid view for files and folders
  - Rename support via GUI dialog
  - Create new files and folders
  - Notepad integration for editing text files
  - File icons and visual feedback

### 🌐 Networking
- **Virtio-Net Driver**: High-performance network interface
- **TCP/IP Stack**: Custom implementation of Ethernet, ARP, IP, ICMP, UDP, and TCP
- **Host Passthrough**: Full internet access via QEMU user networking
- **DNS Resolution**: Built-in DNS client
- **Socket API**: Berkeley sockets-compatible interface
- **WiFi Status**: Visual indicator in the menu bar

### 🛠 Core System
- **Multi-Architecture Kernel**: Supports ARM64 and x86_64 with clean abstraction layer
- **Preemptive Multitasking**: Priority-based scheduler with context switching
- **Memory Management**: 4-level paging (ARM64) and 4-level paging (x86_64)
- **Virtual Memory**: Full MMU support with demand paging
- **Interrupt Handling**: 
  - ARM64: GICv3 (Generic Interrupt Controller)
  - x86_64: APIC (Advanced Programmable Interrupt Controller)
- **Timers**:
  - ARM64: ARM Generic Timer
  - x86_64: PIT (Programmable Interval Timer)
- **Serial Console**:
  - ARM64: PL011 UART
  - x86_64: 16550 UART (COM1)

### 🎮 Input & Output
- **Absolute Mouse**: Virtio Tablet for precise cursor positioning
- **Keyboard**: Full keyboard support with key repeat
- **Framebuffer**: Direct framebuffer access for graphics
- **Display Drivers**:
  - QEMU ramfb (ARM64)
  - VGA/VESA (x86_64)
  - Bochs Graphics Adapter

### 🔊 Audio (Experimental)
- **Intel HDA**: High Definition Audio controller driver
- **PCM Playback**: 16-bit stereo audio support
- **MP3 Decoder**: minimp3 library integration

### 📦 Applications
- **Terminal**: `ls`, `cd`, `help`, `clear`, `cat`, `echo`, environment variables
- **Notepad**: Text editor with save/load functionality backed by VFS
- **Snake**: Classic game with graphics and score tracking
- **Calculator**: Basic arithmetic operations with GUI
- **File Manager**: Browse, create, rename, and delete files
- **Doom**: Full Doom port with graphics, input, and sound
- **About**: System information dialog

## 🚀 Quick Start

### Prerequisites

**macOS:**
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install QEMU
brew install qemu
```

**Linux:**
```bash
# Ubuntu/Debian
sudo apt-get install qemu-system-aarch64 qemu-system-x86 gcc-aarch64-linux-gnu make

# Arch Linux
sudo pacman -S qemu-system-aarch64 qemu-system-x86 aarch64-linux-gnu-gcc make
```

### ARM64 (Default - Recommended)

```bash
# Clone the repository
git clone https://github.com/yourusername/vib-OS.git
cd vib-OS

# Build everything (kernel, drivers, userspace)
make all

# Run with GUI (opens QEMU window)
make run-gui

# Or run in text mode
make run

# Or run with QEMU (headless testing)
make qemu
```

### x86_64 (Multi-Architecture Build)

```bash
# Build for x86_64
make -f Makefile.multiarch ARCH=x86_64 clean
make -f Makefile.multiarch ARCH=x86_64 kernel

# Test in QEMU
make -f Makefile.multiarch ARCH=x86_64 qemu
```

### Available Make Targets

```bash
# ARM64 (default Makefile)
make all          # Build everything
make kernel       # Build kernel only
make drivers      # Build drivers only
make libc         # Build C library
make userspace    # Build userspace programs
make image        # Create bootable disk image
make run          # Run in QEMU (text mode)
make run-gui      # Run in QEMU (GUI mode)
make qemu         # Run in QEMU (headless)
make qemu-debug   # Run with GDB server
make clean        # Clean build artifacts

# Multi-Architecture (Makefile.multiarch)
make -f Makefile.multiarch ARCH=arm64 kernel
make -f Makefile.multiarch ARCH=x86_64 kernel
make -f Makefile.multiarch ARCH=x86 kernel
```

## 💾 Creating Bootable Media

### For ARM64 (Raspberry Pi 4/5)

```bash
# Build bootable image
make image

# Write to SD card (replace diskX with your SD card)
# macOS
sudo dd if=image/unixos.img of=/dev/rdiskX bs=4m status=progress

# Linux
sudo dd if=image/unixos.img of=/dev/sdX bs=4M status=progress && sync
```

### For x86_64 PC

```bash
# Create UEFI bootable image
./scripts/create-uefi-image.sh

# Create BIOS bootable image
./scripts/create-bios-image.sh

# Create bootable ISO
./scripts/create-iso.sh

# Write to USB drive
sudo dd if=vibos-uefi.img of=/dev/sdX bs=4M status=progress && sync
```

## 🧪 Testing

### QEMU (Recommended)

```bash
# ARM64 with GUI
make run-gui

# ARM64 text mode
make run

# ARM64 headless (for CI/automation)
make qemu

# x86_64
make -f Makefile.multiarch ARCH=x86_64 qemu
```

### Real Hardware

#### Raspberry Pi 4/5
1. Build image: `make image`
2. Write to SD card: `sudo dd if=image/unixos.img of=/dev/sdX bs=4M`
3. Insert SD card and power on

#### x86_64 PC
1. Create bootable USB: `./scripts/create-uefi-image.sh`
2. Write to USB: `sudo dd if=vibos-uefi.img of=/dev/sdX bs=4M`
3. Boot from USB (select UEFI boot in BIOS)

### Apple Silicon (M1/M2/M3/M4)

Use UTM (https://mac.getutm.app/):
1. Create new ARM64 virtual machine
2. Use `image/unixos.img` as boot disk
3. Configure 2GB+ RAM
4. Start VM

## 📚 Documentation

- [Multi-Architecture Build Guide](docs/MULTIARCH_BUILD.md) - Detailed build instructions
- [Multi-Architecture Implementation](docs/MULTIARCH_IMPLEMENTATION.md) - Technical details
- [Quick Start Guide](MULTIARCH_QUICKSTART.md) - Quick reference
- [Build Status](BUILD_ALL.md) - Current build status and hardware compatibility
- [Boot Support](docs/BOOT_SUPPORT.md) - Boot methods and requirements
- [Known Issues](KNOWN_ISSUES.md) - Current limitations and bugs

## 🚧 Current Status & Known Issues

### What Works
- ✅ ARM64 kernel boots and runs stably
- ✅ x86_64 kernel builds successfully
- ✅ GUI system with windows, dock, and applications
- ✅ File system (RamFS) with file manager
- ✅ Networking (TCP/IP stack, virtio-net)
- ✅ Process management and scheduling
- ✅ Input (keyboard and mouse)
- ✅ Doom runs with full graphics

### Known Issues
1. **Sound Support**: Intel HDA driver initializes but audio playback is unstable
2. **Persistent Storage**: Currently RAM-only (RamFS) - data lost on reboot
3. **x86_64 Testing**: Needs more real hardware testing
4. **Network Settings UI**: Not fully implemented
5. **Web Browser**: Basic rendering only, no full HTML parser

### Roadmap
- [ ] **Persistent Storage**: Implement EXT4/FAT32 write support
- [ ] **x86 32-bit**: Complete kernel implementation
- [ ] **Audio**: Fix Intel HDA buffer management
- [ ] **USB Support**: Add USB mass storage and HID drivers
- [ ] **Multi-core**: SMP support for multiple CPUs
- [ ] **User Accounts**: Login screen and multi-user support
- [ ] **Package Manager**: Install/remove applications
- [ ] **More Apps**: Text editor, image viewer, music player

## 🤝 Contributing

We welcome contributions! Here's how to get started:

1. **Fork** the repository
2. Create a **Feature Branch** (`git checkout -b feature/NewFeature`)
3. **Commit** your changes (`git commit -m 'Add NewFeature'`)
4. **Push** to the branch (`git push origin feature/NewFeature`)
5. Open a **Pull Request**

### Coding Standards
- Use **C11** standard
- Follow kernel coding style (4-space indentation, K&R braces)
- Test on both ARM64 and x86_64 (if applicable)
- Add comments for complex logic
- Update documentation for new features

### Areas for Contribution
- 🐛 **Bug Fixes**: Fix known issues
- 🎨 **GUI Improvements**: Enhance window manager, add widgets
- 🔧 **Drivers**: Add support for new hardware
- 📦 **Applications**: Create new userspace programs
- 📚 **Documentation**: Improve guides and comments
- 🧪 **Testing**: Test on real hardware and report issues

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

This project uses the following excellent open-source libraries:

### Media Libraries
- **minimp3** by lieff - CC0 MP3 decoder
- **picojpeg** by Rich Geldreich - Public Domain JPEG decoder
- **stb_image** by Sean Barrett - MIT/Public Domain image loader (PNG, JPEG, BMP, TGA, etc.)
- **stb_image_write** by Sean Barrett - MIT/Public Domain image writer

### Cryptography
- **TLSe** by Eduard Suica - BSD 2-Clause TLS 1.2/1.3 implementation

### Games & Tools
- **DOOM** by id Software - Original Doom engine
- **QEMU** - The QEMU team for the excellent emulator

### Community
- **OSDev Community** - For invaluable resources and documentation

See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for full license texts and usage examples.

## 📞 Contact

- **GitHub**: [yourusername/vib-OS](https://github.com/yourusername/vib-OS)
- **Issues**: [Report bugs or request features](https://github.com/yourusername/vib-OS/issues)

---

**Made with ❤️ by the Vib-OS Team**

*"Building an OS from scratch, one line at a time."*
