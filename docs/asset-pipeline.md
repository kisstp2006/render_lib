# Asset pipeline and cooked resource architecture

This document records the pre-implementation audit, architecture and usage of
the engine's source asset -> asset descriptor -> cooked resource pipeline. The
pipeline is deliberately independent from an editor UI. It exposes property,
browser, batch-import and preview-provider foundations that an editor can bind
to later.

## Pre-implementation audit

Before this pipeline was introduced the engine had no general asset database,
serializer, UUID type, typed resource handle, dependency graph or cooked file
format.

Existing code that is retained:

- `TextureData`, `MeshData`, `Material`, `Scene` and `EnvironmentSettings` are
  renderer-neutral CPU representations and remain the values consumed by both
  render backends.
- OpenGL and Vulkan own pointer-keyed GPU caches. They remain backend caches;
  cooked resources feed the shared CPU representations instead of duplicating
  GPU ownership in the asset system.
- `LoadGltfScene`, `textures::LoadFromFile`, `environments::LoadHdrFromFile`
  and `.cube` LUT loading remain available as development compatibility paths.
- The runtime `World` already supplies stable-in-process entity handles,
  hierarchy and component registration. Scene resources build on it rather
  than creating a second entity system.
- The runtime plugin registry is suitable for adding asset types and resource
  loaders from plugins once the host API is extended in a later phase.

Existing code that needs extension:

- direct image and glTF loaders need importer-facing metadata/dependency APIs;
- `World` needs serializable component type enumeration;
- the application needs an optional cooked-resource registry at runtime;
- samples use a shared development asset host that imports, cooks and resolves
  GUID handles before building the backend-neutral render scene.

New modules required:

- **AssetCore**: GUIDs, deterministic descriptor serialization, migrations,
  registry, dependency database, content fingerprints and atomic writes;
- **Resources**: validated cooked headers, typed handles, loader registry and
  weak resource cache;
- **Assets**: importers, validators and cookers for the formats the renderer
  currently understands;
- **assetc**: headless import, transform, validation, inspection and registry
  generation.

The implemented build targets are `engine_asset_core`, `engine_resources`,
`engine_asset_pipeline`, `engine_asset_cookers` (heavy offline transforms),
`engine_assets` (concrete types), and `engine_asset_tools` (UI-independent
authoring models).

There is no editor property inspector, content browser or thumbnail system in
the repository. The asset type registry therefore stores property schemas,
inspector/preview provider hooks, icons and thumbnail cache keys without
introducing a GUI framework.

## ezEngine reference audit

The implementation was informed by the following concrete ezEngine areas:

- `EditorFramework/Assets/AssetDocument` and `AssetDocumentInfo`: document
  settings, transform/package/thumbnail dependencies and transform entry
  points;
- `AssetDocumentManager` and `AssetDocumentGenerator`: centrally registered
  document types, source extension discovery and multiple ranked import modes;
- `AssetCurator` and `AssetUpdates`: GUID lookup, duplicate GUID repair,
  reverse dependencies, circular dependency detection, profile-aware settings
  hashes and transitive invalidation;
- `TextureAsset`, `TextureCubeAsset`, `MeshAsset` and `MeshImportUtils`:
  type-specific settings, external texture conversion, coordinate-system
  conversion and generated material dependencies;
- `Foundation/Utilities/AssetFileHeader`: a small versioned header at the start
  of every transformed output;
- `Core/ResourceManager`: typed resource handles, loader registration,
  asynchronous-ready state and resource caching.

Applied principles:

- source files, descriptors and runtime payloads are distinct;
- descriptors own stable GUIDs while paths are movable metadata;
- type behavior is registered instead of distributed across switch blocks;
- dirty state is content/settings/dependency/profile/version based, never only
  timestamp based;
- dependencies are transformed before their consumers;
- cooked files are replaced atomically only after successful validation;
- runtime loading validates type, version, sizes and checksum before exposing
  data;
- direct source loading remains a development fallback during migration.

Intentionally not copied:

- Qt document objects, editor/engine IPC mirroring and editor windows;
- reflection-heavy property serialization;
- separate worker processes for texture conversion;
- a global static resource manager.

Those facilities are valuable at ezEngine scale but would couple this small
runtime to an editor and make testing harder. This engine uses ordinary C++
registries owned by the application/tool and deterministic text descriptors.

## File roles

1. **Source file**: a development input such as PNG, HDR or GLB. Runtime builds
   do not parse it in the normal path.
2. **Asset descriptor**: a human-readable `.sla-*` file containing a GUID,
   versions, relative source paths, settings, dependencies, transform state and
   diagnostics.
3. **Cooked resource**: a platform/profile-specific `.slres` binary in the
   cache, with a validated common header and type-specific payload.
4. **Runtime registry**: a compact GUID -> type/path table used by the resource
   manager without scanning descriptor files.

The `sla` prefix means **Source-Like Asset** and is centrally declared by the
built-in asset type registrations; callers never need to hard-code extensions.

## Dependency direction

```text
Foundation -> AssetCore -> Resources -> AssetPipeline
Scene -------------------------------> AssetCookers -> Assets (concrete types)
AssetPipeline + Resources -----------> AssetTools
Assets + AssetTools -----------------> assetc / future editor
TextureData / MeshData --------------> OpenGL and Vulkan GPU caches
```

AssetCore has no renderer or editor dependency. Resources does not know how a
texture or mesh is rendered. Assets supplies the concrete cookers and loaders.

## Descriptor safety and versioning

Descriptors use a deterministic escaped key/value representation. Map keys and
arrays are sorted before writing so settings fingerprints are stable across
machines. Writes use a sibling temporary file followed by an atomic replace.
Migration is a sequence of registered `N -> N+1` callbacks. File migration
creates a backup before replacing the descriptor and rejects unknown future
versions without modifying the original.

## Editor-facing foundations (no editor UI)

Each registered asset type can expose:

- a property schema with groups, types, defaults and visibility predicates;
- a preview provider returning renderer-neutral preview metadata or bytes;
- icon and display-name metadata;
- source import modes and priority;
- searchable tags, status and diagnostics through the asset database;
- deterministic thumbnail keys derived from the transform fingerprint;
- batch source import through the same API used by `assetc`.

These contracts support a future inspector, content browser, file drop handler
and thumbnail service without making the current task an editor implementation.

The concrete `AssetInspectorModel`, `AssetBrowserModel`, versioned
`AssetDragPayload` and asynchronous `AssetPreviewService` live in
`AssetWorkspace.h`. Inspector edits are validated and saved atomically, mark
the asset and its dependents dirty, and do not trigger a transform until the
caller invokes it. Preview requests are CPU-side, thread-safe and de-duplicated;
the future UI owns conversion into a native OpenGL/Vulkan UI texture.

## Implemented asset types

| Type | Descriptor | Runtime payload | Sources |
| --- | --- | --- | --- |
| Texture2D | `.sla-texture` | `texture2d` | PNG, JPEG, TGA, BMP, PSD, GIF, PIC, PNM/PPM/PGM, HDR |
| Color grading LUT | `.sla-lut` | `color-grading-lut` | industry-standard `.cube` 3D LUT |
| Cubemap | `.sla-cubemap` | `texture-cubemap` | equirectangular HDR/LDR or six explicit faces |
| Skybox | `.sla-skybox` | `skybox` | descriptor-authored, references Cubemap by GUID |
| Material | `.sla-material` | `material` | descriptor-authored or generated by glTF import |
| Static mesh | `.sla-mesh` | `static-mesh` | glTF 2.0 `.gltf` / `.glb` |
| Scene | `.sla-scene` | `scene` | descriptor-authored |

`RegisterBuiltinAssetTypes()` installs all concrete importers, validators,
transformers, preview/property providers and runtime loaders. No switch in the
pipeline or resource manager needs modification when a new registered type is
added.

### Texture processing

Texture cooking decodes into linear float working data, supports up to four
input images, per-output-channel source selection/constants, swizzle, flips,
90-degree rotations, normal-green inversion, premultiplication, alpha removal,
bump-to-normal conversion and transparent-pixel dilation. It generates
sRGB-correct or normal-renormalized mipmaps, optional alpha-coverage
preservation and complete sampler state. Cooked mip chains upload directly in
both OpenGL and Vulkan; legacy `TextureData` with only level zero still uses
backend mip generation.

The central format policy emits raw RGBA8/RGBA32F or native GPU blocks. Desktop
profiles select BC7 for high-quality color, BC1/BC3 for faster color/alpha and
BC5 for tangent-space normals; mobile profiles select ASTC 4x4. Explicit format
overrides are available per asset. `rgbcx`/`bc7enc` encode BC1/3/5/7 and Arm
`astcenc` encodes ASTC with fast, medium or thorough quality presets. Edge
blocks are padded deterministically, every mip stores its exact hardware block
payload, and OpenGL/Vulkan upload it without CPU decompression. HDR remains
RGBA32F because BC6H/ASTC-HDR runtime tiers are not enabled yet.

### Environment processing

Cubemap cooking uses the same face convention as the shared renderer shaders.
It converts equirectangular panoramas or validates six equal square faces,
applies per-face orientation corrections, produces seam-aware direction-space
mips, cosine-weighted diffuse irradiance and a GGX importance-sampled specular
prefilter chain. Skybox appearance and lighting controls remain a separate
lightweight resource.

### Model and scene processing

glTF import emits one primary Static Mesh descriptor and generated Texture2D
and Material descriptors. External and buffer-embedded images become regular
project sources. Generated asset ownership is recorded; user-modified generated
descriptors are reused without overwrite, and reimport preserves every stable
GUID. The cooked mesh stores explicit vertices, indices, per-part transforms,
bounds, material handles and coordinate conversion. Normal/tangent generation
continues to use the tested glTF loader. The dedicated AssetCookers module uses
`meshoptimizer` for exact-vertex remap/welding, vertex-cache ordering, overdraw
ordering and vertex-fetch compaction. Attribute-aware simplification produces
up to eight real LOD levels with a triangle target and normalized geometric
error; an optional aggressive fallback is available for topology that cannot
reach the target under conservative constraints.

Collision cooking is renderer/physics-runtime neutral. It merges transformed
parts, welds render seam duplicates by position, optionally simplifies the
triangle mesh, builds a deterministic median-split triangle BVH, and stores the
result in the versioned static-mesh payload. Convex-decomposition mode uses
V-HACD with bounded hull count, voxel resolution, volume error and vertices per
hull; every hull also receives validated runtime traversal data. A future
physics backend can consume this payload without repeating source import or
geometry processing.

Scene descriptors store stable object GUIDs, parent hierarchy, tags/layers,
local TRS, versioned components with property maps, asset/prefab references,
skybox, active camera and scene environment/lighting maps. Export rejects
duplicates, missing parents/references, hierarchy cycles and unknown component
types when a component query is supplied. Editor metadata round-trips in the
descriptor but is intentionally absent from the cooked payload.

## Runtime loading and hot reload

`ResourceManager` resolves typed `AssetHandle<T>` values through the compact
runtime registry, validates the `.slres` magic/type/version/size/SHA-256
checksum, then invokes the registered typed loader. Loaded objects use a weak
cache. Transform completion invalidates the affected GUID and notifies reload
listeners. The application owns registry/resource-manager instances; there is
no hidden global singleton.

OpenGL and Vulkan continue to cache their native resources by the shared CPU
object pointer. Thus the asset system does not introduce a second GPU resource
owner. Both backends understand cooked mip chains, float textures, filtering,
addressing, anisotropy and mip bias.

## Command-line workflow

Build `assetc`, then run it from the project root:

```powershell
assetc scan
assetc import Source/albedo.png Source/model.glb
assetc transform-all
assetc --platform windows --profile mobile transform-all
assetc validate
assetc list bottle
assetc deps 01234567-89ab-cdef-0123-456789abcdef
```

Useful options are `--project`, `--assets`, `--cache`, `--platform` and
`--profile`. `transform` skips a clean asset, while `force` always cooks it.
Successful transforms can be persisted to `assets.slareg` for production
runtime startup.

## Adding an asset type

Create one `AssetTypeRegistration` containing the unique type ID, descriptor
extension, source extensions/import modes, versions, runtime type and the
callbacks that apply. Register its typed resource loader against the same
runtime type. Importers may return multiple `ImportedAsset` records; cookers
return one bounded payload plus newly discovered source/asset dependencies.
Register sequential migration callbacks for every old descriptor version.

## Current deliberate boundaries

- There is no concrete editor/window/widget implementation. The inspector,
  browser, drag/drop and preview models are ready for one, as requested.
- BC6H and ASTC-HDR are not enabled, so HDR Texture2D assets stay RGBA32F.
- LOD/collision assets are fully cooked and runtime-loadable, but automatic
  screen-size LOD selection and a rigid-body physics consumer are separate
  renderer/physics roadmap items.
- OBJ/FBX are not advertised: the engine only has a glTF importer and no Assimp
  or FBX SDK dependency.
- Animated mesh, skeleton and animation assets are not registered because the
  engine has no skeletal animation runtime. Audio/font assets are likewise not
  invented without corresponding runtime systems.
- Direct glTF/image/HDR/LUT loaders remain available for development migration;
  production code should use the cooked registry.

All seven sample executables now initialize the same sample asset workspace.
glTF meshes, generated materials/textures, HDR environments and optional
`.cube` LUTs are imported into stable descriptors under the build directory,
cooked only when their fingerprint changes, then loaded through the runtime
registry. Procedural meshes and generated checker/cookie textures remain
in-memory data by design because they have no source asset.

## Verification

`engine_asset_tests` covers GUID/descriptor round trips, stable settings and
source hashes, migration backups, duplicate GUIDs, profile/source invalidation,
texture policy/mips/channel packing, cook-only `.cube` LUT round trips,
BC1/3/5/7 and ASTC deterministic block
sizes, HDR and six-face cubemaps, IBL payloads, meshoptimizer cache/fetch
optimization, monotonic LOD simplification/error, triangle BVH and convex
collision cooking, versioned mesh/collision runtime load, coordinate
conversion, glTF dependency generation and stable reimport, scene hierarchy,
inspector/browser/drop/preview foundations, corrupt cooked headers and atomic
replacement. It is registered in CTest as `engine_asset_pipeline`.
