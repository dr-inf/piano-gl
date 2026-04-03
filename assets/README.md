# Assets Directory

This directory contains the 3D models, environment maps, and MIDI files used by the Keys library and example application.

## Contents

### 3D Models

#### `piano.blend`
- **Description**: Blender source file containing the piano keyboard 3D model
- **Meshes**: WhiteKey, BlackKey, BackPlane, Base
- **Materials**: Procedural PBR materials (metallic-roughness workflow)
- **Blender Version**: 5.0.21
- **License**: MIT License
- **Copyright**: (c) 2025-2026 dr-inf (Florian Krohs)
- **Size**: ~156 KB

#### `scene.gltf` and `scene.glb`
- **Description**: Exported glTF 2.0 models from piano.blend
- **Format**:
  - `scene.gltf` - Text JSON format (134 KB)
  - `scene.glb` - Binary format (40 KB)
- **License**: MIT License (derived from piano.blend)
- **Copyright**: (c) 2025-2026 dr-inf (Florian Krohs)
- **Notes**:
  - Contains 88 piano keys (MIDI notes 21-108, A0-C8)
  - Procedural materials, no external textures required
  - Used by the Keys library for rendering

### Environment Maps

#### `env/mirrored_hall_2k.hdr`
- **Description**: HDR equirectangular environment map for image-based lighting
- **Source**: [Mirrored Hall from Polyhaven](https://polyhaven.com/a/mirrored_hall)
- **Format**: Radiance HDR (.hdr) with RLE compression
- **Resolution**: 2048 x 1024 pixels
- **Size**: ~6.6 MB
- **Usage**: Provides realistic lighting and reflections via IBL (Image-Based Lighting)
- **License**: [CC0 Public Domain](https://polyhaven.com/license)
- **Attribution**: While CC0 requires no attribution, it's appreciated:
  ```
  Environment Map: "Mirrored Hall" from Polyhaven.com (CC0)
  ```

This HDR depicts a classical interior hall with ornate ceiling, wooden paneling, and large windows. The warm, soft lighting is ideal for rendering piano scenes with elegant reflections.

### MIDI Files

#### `demo.mid`
- **Description**: Auto-generated arpeggiated piano demo clip for the example application
- **Notes**: A short C-sharp-minor arpeggio study inspired by the opening energy of Beethoven's Moonlight Sonata, 3rd movement
- **Duration**: ~6 seconds
- **License**: CC0 Public Domain (auto-generated, no creative input)
- **Usage**: Used by the example application for visual animation
- **Size**: 673 bytes

## Asset Usage

The Keys library requires:
- **Required**: A glTF/GLB model with piano key geometry (`gltfPath`)
- **Optional**: An HDR environment map for lighting (`envHdrPath`)

If no environment map is provided, the renderer falls back to a solid-color cubemap.

## Modifying Assets

### Editing the Piano Model

To modify the piano model:

```bash
# Open in Blender
blender piano.blend

# After making changes, export to glTF:
# File > Export > glTF 2.0 (.gltf/.glb)
# Settings:
#   - Format: glTF Separate (.gltf + .bin)
#   - Include: Selected Objects or All
#   - Transform: +Y Up
#   - Geometry: Apply Modifiers, UVs, Normals, Tangents
#   - Materials: Export materials
```

The glTF model is used by the library, while `piano.blend` is kept as the editable source.

## License Summary

| Asset | License | Source |
|-------|---------|--------|
| `piano.blend` | MIT | Created by dr-inf (Florian Krohs) |
| `scene.gltf` / `scene.glb` | MIT | Exported from piano.blend |
| `demo.mid` | CC0 Public Domain | Auto-generated |
| `env/mirrored_hall_2k.hdr` | CC0 Public Domain | [Polyhaven](https://polyhaven.com/a/mirrored_hall) |

## Attribution

For redistributed builds, the MIT-licensed piano assets should keep their copyright notice and the MIT license text somewhere in your distributed notices. A visible marketing credit is appreciated, but not required.

A simple notice can look like this:

```
Piano 3D Model: Copyright (c) 2025-2026 dr-inf (Florian Krohs)
License: MIT
Source: https://github.com/dr-inf/piano-gl

Environment Map: "Mirrored Hall" from Polyhaven.com (CC0)
```

All Polyhaven assets are CC0 (Public Domain), which means:
- ✅ Free for commercial use
- ✅ No attribution required (but appreciated)
- ✅ Modify and redistribute freely
- ✅ No copyright restrictions
