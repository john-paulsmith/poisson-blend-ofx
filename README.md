# Poisson Blend OFX Plugin

A complete OpenFX (OFX) plugin that performs **Poisson blending** (seamless cloning) of a foreground image into a target/background image using OpenCV's `cv::seamlessClone`, while preserving full floating-point precision.

## Features

- **Pure OpenFX C API**: Uses only the raw OpenFX C API without any C++ support library dependencies
- **Poisson Blending**: Implements seamless cloning using OpenCV's `cv::seamlessClone` algorithm
- **Float Precision Preservation**: Uses a residual-based approach to maintain full float precision despite 8-bit processing
- **Multiple Blend Modes**: Supports Normal Clone, Mixed Clone, and Monochrome Transfer
- **Configurable Mask**: Optional mask input with adjustable threshold
- **Flexible Positioning**: Adjustable blend center for precise placement

## Technical Approach

### Float Precision Preservation

Since `cv::seamlessClone` only operates on 8-bit images, a naive float→8-bit→float conversion would destroy precision. This plugin uses a **residual-based approach** to preserve full float precision:

1. Convert float target and foreground images to 8-bit BGR
2. Run `cv::seamlessClone` on 8-bit images to get 8-bit result
3. **Compute residual**: Convert both 8-bit result and 8-bit target back to float, compute `residual = resultFloat - target8Float`
4. **Apply residual**: Add residual to original float target: `finalOutput = originalFloatTarget + residual`

This captures the Poisson blending correction as a float residual while preserving the underlying precision of the original image data.

## Dependencies

- **OpenFX Headers**: Automatically fetched from [AcademySoftwareFoundation/openfx](https://github.com/AcademySoftwareFoundation/openfx) via CMake FetchContent
- **OpenCV**: Required for `cv::seamlessClone` (install via package manager)
- **CMake 3.15+**: Build system

## Building

### Prerequisites

Install OpenCV on your system:

**Ubuntu/Debian:**
```bash
sudo apt-get install libopencv-dev
```

**macOS (Homebrew):**
```bash
brew install opencv
```

**Windows:**
Download and install OpenCV from the [official website](https://opencv.org/releases/) or use vcpkg:
```bash
vcpkg install opencv
```

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/john-paulsmith/poisson-blend-ofx.git
cd poisson-blend-ofx

# Create build directory
mkdir build
cd build

# Configure
cmake ..

# Build
cmake --build .

# The plugin will be built as a proper OFX bundle:
# build/PoissonBlend.ofx.bundle/
#   Contents/
#     Info.plist
#     Linux-x86-64/         (on Linux x86_64)
#       PoissonBlend.ofx
#     MacOS-x86-64/         (on macOS Intel)
#       PoissonBlend.ofx
#     MacOS-ARM-64/         (on macOS Apple Silicon)
#       PoissonBlend.ofx
#     Win64/                (on Windows 64-bit)
#       PoissonBlend.ofx
```

### Build with Custom OpenFX Headers

If you want to use your own OpenFX headers instead of fetching them:

```bash
cmake -DOpenFX_ROOT=/path/to/openfx ..
cmake --build .
```

## Installation

### Option 1: Using CMake Install (Recommended)

Install the plugin bundle to the system OFX plugin directory:

```bash
# Install to default location (requires admin/sudo on Linux/macOS)
cmake --install .

# Or install to a custom location
cmake --install . --prefix /custom/path
```

**Default installation paths:**
- **Linux**: `/usr/local/OFX/Plugins/PoissonBlend.ofx.bundle/`
  - For per-user install: Build with `-DCMAKE_INSTALL_PREFIX=$HOME/.local/share` and the bundle will install to `~/.local/share/OFX/Plugins/`
- **macOS**: `/Library/OFX/Plugins/PoissonBlend.ofx.bundle/` (system-wide)
  - For per-user install: Manually copy to `~/Library/OFX/Plugins/` (see Option 2 below)
- **Windows**: `C:\Program Files\Common Files\OFX\Plugins\PoissonBlend.ofx.bundle\`

### Option 2: Manual Copy

Alternatively, manually copy the entire `PoissonBlend.ofx.bundle` directory from the build directory to your OFX plugin directory:

**Linux:**
```bash
# System-wide (requires sudo)
sudo cp -r build/PoissonBlend.ofx.bundle /usr/local/OFX/Plugins/

# Per-user
mkdir -p ~/.local/share/OFX/Plugins
cp -r build/PoissonBlend.ofx.bundle ~/.local/share/OFX/Plugins/
```

**macOS:**
```bash
# System-wide (requires sudo)
sudo cp -r build/PoissonBlend.ofx.bundle /Library/OFX/Plugins/

# Per-user
mkdir -p ~/Library/OFX/Plugins
cp -r build/PoissonBlend.ofx.bundle ~/Library/OFX/Plugins/
```

**Windows:**
```powershell
# Copy to system location (requires admin privileges)
xcopy /E /I build\PoissonBlend.ofx.bundle "C:\Program Files\Common Files\OFX\Plugins\PoissonBlend.ofx.bundle"
```

## Usage

### Clips (Inputs/Outputs)

- **Source**: The target/background image where the foreground will be blended
- **Foreground**: The image to blend into the target
- **Mask** (optional): A mask defining the blend region (white = blend, black = ignore)
- **Output**: The blended result

### Parameters

#### Blend Center
- **Type**: Double 2D (X, Y coordinates)
- **Default**: (0.5, 0.5) - center of image
- **Description**: Position in the target image where the center of the foreground will be placed (normalized coordinates, 0.0 to 1.0)

#### Blend Mode
- **Type**: Choice
- **Options**:
  - **Normal Clone**: Standard Poisson blending (default)
  - **Mixed Clone**: Mixes gradients of source and destination
  - **Monochrome Transfer**: Transfers texture from source while preserving destination color
- **Default**: Normal Clone

#### Mask Threshold
- **Type**: Double
- **Range**: 0.0 to 1.0
- **Default**: 0.5
- **Description**: Threshold for binarizing the mask input. Pixels above this luminance value are included in the blend region.

### Example Workflow (Nuke)

1. Load your background/target image
2. Load your foreground image
3. Create a PoissonBlend node
4. Connect the background to the "Source" input
5. Connect the foreground to the "Foreground" input
6. (Optional) Create and connect a mask to the "Mask" input
7. Adjust the "Blend Center" to position the foreground
8. Select the desired "Blend Mode"
9. Fine-tune the "Mask Threshold" if using a mask

### Example Workflow (DaVinci Resolve/Natron)

The workflow is similar - connect your inputs and adjust parameters through the node interface.

## Supported Hosts

This plugin should work with any OFX-compatible host, including:
- Nuke
- DaVinci Resolve
- Natron
- Vegas Pro
- HitFilm
- And other OFX-compatible compositing/editing software

## Technical Details

### OpenFX Implementation

This plugin is implemented using **only the raw OpenFX C API**, without the C++ support library (`ofxsImageEffect.h`, etc.). It directly uses:

- `OfxImageEffectSuiteV1` for clip and image operations
- `OfxPropertySuiteV1` for property access
- `OfxParameterSuiteV1` for parameter handling

### Supported Contexts

- Filter context
- General context

### Pixel Formats

- Float RGBA (32-bit per channel)

### Threading

- Fully thread-safe rendering

### Tiling

- Does not support tiled rendering (requires full images due to Poisson solver's global nature)

## License

This plugin uses:
- OpenFX headers: BSD 3-Clause License
- OpenCV: Apache 2.0 License

See individual component licenses for details.

## Contributing

Contributions are welcome! Please feel free to submit issues or pull requests.

## Acknowledgments

- Academy Software Foundation for the OpenFX specification
- OpenCV team for the seamless cloning implementation
- The compositing and VFX community for feedback and testing
