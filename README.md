# EDPC - Dynamic Pantograph Configurator

EDPC (EgEhE's Dynamic Pantograph Configurator) is a native desktop engineering application designed to configure, mathematically solve, 3D-simulate, and package dynamic catenary-tracking pantographs for Minecraft Transit Railway (MTR) train models.

I engineered this project as a polyglot systems application utilizing Rust, C++17, C, and Vulkan/GLSL to achieve a clean separation between computational safety and high-throughput real-time hardware rendering.

## Key Features

1. **Multi-Pantograph Profile Management:**
   * Support for multiple pantograph models within a single train pack.
   * Hardcoded, fully randomized 8-character script IDs and 5-character alphanumeric model identifiers generated during packaging.
   * Complete decoupling of model instances to prevent identifier collisions across vehicle variants.

2. **Real-Time 3D Kinematic Preview:**
   * Full Vulkan hardware-accelerated 3D viewport.
   * Real-time pose calculation of base (`p_base`), lower arm (`p1`), upper arm (`p2`), and collector head (`p3`).
   * Simulated catenary wire inspection, idle park height, and depot park mode simulation.

3. **Train Pack Inspection and Vehicle Mount Configuration:**
   * Automated inspection of both `.zip` packs and unpacked directory hierarchies.
   * Per-vehicle roof mounting parameters: mounting height, lateral X offset, longitudinal Z offset, 180-degree yaw rotation, and catenary detection forward offsets.
   * Quick roof presets for front, rear, and center vehicle positions.

4. **Automated Export and Packaging:**
   * Automatic insertion of script bindings into `mtr_custom_resources.json`.
   * Packaging of OBJ, MTL, and texture assets into proper pack subdirectories.
   * Direct export to an MTR-compatible Minecraft `.zip` resource pack.

5. **Integrated Release Checker:**
   * Persistent version badge in the bottom status bar (`v1.0.1`).
   * One-click release checking with direct link redirection to GitHub Releases.

---

## Requirements and Building

### System Prerequisites
* Linux (x86_64) or Windows
* C++17 compliant compiler (GCC 11+, Clang 13+, or MSVC 2019+)
* CMake 3.20 or newer
* Rust toolchain (Cargo, stable)
* Qt 6 Development Packages (`qt6-base-dev` including `Core`, `Gui`, `Widgets`, `Network`)
* Vulkan SDK and compatible Vulkan 1.2+ GPU driver

### Build from Source

```bash
# Clone the repository
git clone https://github.com/EgEhEw/EDPC.git
cd EDPC

# Build the Rust core static library
cargo build --release --manifest-path edpc_core/Cargo.toml

# Configure and compile the C++ application
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Running the Application

```bash
# Using the launcher script:
./run_edpc.sh

# Or running the compiled binary directly:
./build/edpc
```

---
