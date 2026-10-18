# EDPC - Dynamic Pantograph Configurator

EDPC (EgEhE's Dynamic Pantograph Configurator) is a native desktop engineering application designed to configure, mathematically solve, 3D-simulate, and package dynamic catenary-tracking pantographs for Minecraft Transit Railway (MTR) train models.

I engineered this project as a polyglot systems application utilizing Rust, C++17, C, and Vulkan/GLSL to achieve a clean separation between computational safety and high-throughput real-time hardware rendering.

---

## Technical Architecture and Design Decisions

I designed the system around a decoupled hybrid architecture: a Rust static core library responsible for mathematics, file I/O, and code generation, paired with a modern C++/Qt6 desktop shell and a native Vulkan rendering engine.

```text
+-------------------------------------------------------------+
|                     Qt 6 Desktop UI (C++)                   |
|         Reactive Widgets / Viewports / Update Checker       |
+------------------------------+------------------------------+
                               |
                               | Direct C-ABI Function Calls
                               v
+-------------------------------------------------------------+
|                  edpc_core.h C-ABI Bridge                   |
+------------------------------+------------------------------+
                               |
                               v
+-------------------------------------------------------------+
|                   Rust Core Engine (Rust)                   |
|   4-Bar Kinematics | OBJ Parsers | ZIP I/O | Script Engine  |
+-------------------------------------------------------------+
                               |
                               | Contiguous Vertex / Texture Buffers
                               v
+-------------------------------------------------------------+
|              Vulkan 1.4 Hardware Pipeline (C++/C)           |
|        SPIR-V Shaders (GLSL) | GPU Command Buffers          |
+-------------------------------------------------------------+
```

### 1. Rust Domain Engine (`edpc_core/`)

I implemented the entire analytical and domain layer in Rust to ensure memory safety, data-race freedom, and predictable execution with zero garbage collection overhead.

* **Kinematics Engine:** I implemented forward and inverse kinematics solving for 4-bar pantograph linkages, real-time catenary tracking equations, and adaptive Z-offset compensation for low-height extension stability.
* **Asset Parsing:** I wrote custom Wavefront OBJ/MTL geometry parsers that extract vertex positions, normals, and UV texture coordinates directly into contiguous memory buffers.
* **Pack Processing and Code Generation:** I built the routines for parsing, mutating, and writing Minecraft Transit Railway configuration files (`mtr_custom_resources.json`), manipulating ZIP archives in-memory, and generating randomized ECMAScript animation scripts (`[id]_pantograph.js`).
* **Artifact Compilation:** The crate compiles directly into a standalone static library (`libedpc_core.a`).

### 2. C++ Application Layer and Viewport Engine (`src/`)

I chose modern C++ (C++17) to interface natively with the Qt 6 widget framework and low-level Vulkan APIs.

* **User Interface:** I designed the user interface using Qt 6 Widgets (`QMainWindow`, `QTabWidget`, `QTableView`). It manages application state, vehicle mount configuration tables, multi-profile pantograph management, and dual-language runtime localization (English and Turkish).
* **Networking and Auto-Update:** I integrated `QNetworkAccessManager` to poll the GitHub REST API (`/repos/EgEhEw/EDPC/releases/latest`) asynchronously for release manifests, comparing semver tags against the internal version constant.
* **Vulkan 1.4 Pipeline:** I authored a dedicated Vulkan rendering pipeline using `QVulkanWindow`. It handles physical and logical device acquisition, swapchain management, render passes, graphics pipeline creation, depth buffer allocations, uniform buffer updates, and descriptor sets.
* **Camera System:** I implemented an interactive 3D orbit camera supporting pitch, yaw, pan, and zoom operations, allowing inspection of pantograph linkages and reference contact wires in real time.

### 3. C Interoperability and Khronos Vulkan Headers (`include/`, `third_party/`)

The repository includes C components for two specific technical reasons:

* **Foreign Function Interface (FFI):** To bridge Rust and C++ across compiler boundaries without runtime marshalling penalties, I established a standard C-ABI interface in `edpc_core.h`. The Rust crate exposes functions marked `#[no_mangle] extern "C"`, which my C++ translation units invoke directly.
* **Vulkan API Specification:** I vendored the official Khronos Group Vulkan headers (`vulkan.h`, `vk_platform.h`, and related extension headers) under `third_party/include/`. Because the canonical Vulkan specification is written in C, Git records these headers as C language source files.

### 4. GLSL Shaders (`src/shaders/`)

To render the 3D meshes and reference catenary wires on the GPU, I authored vertex and fragment shaders in GLSL (`mesh.vert`, `mesh.frag`). These shaders are compiled ahead of time into SPIR-V bytecode (`mesh_vert.spv`, `mesh_frag.spv`) and embedded into the application executable for runtime pipeline initialization.

---

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
   * Persistent version badge in the bottom status bar (`v1.0`).
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

## License and Copyright

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)**.

* **Author and Copyright:** **EgEhEw** (<egegokcuogli@hotmail.com>)
* **Terms and Conditions:**
  * You are free to run, study, modify, and redistribute this software.
  * **Copyleft Requirement:** Any modified or derivative works, including forks, must remain open-source under the identical **GNU GPL-3.0** license. Closed-source or proprietary redistribution of derived code is strictly prohibited.
  * **Attribution Requirement:** All original copyright notices and developer attribution to **EgEhEw** must be preserved in all copies and substantial portions of the software.
