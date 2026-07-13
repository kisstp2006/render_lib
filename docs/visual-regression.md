# Golden-image visual regression

The visual suite detects unintended rendering changes and continuously checks
that OpenGL and Vulkan still produce equivalent images. It covers four stable
samples: PBR materials, the glTF water bottle, local lights, and the HDRI studio.

Each capture produces two images:

- `.png`: the final display-referred sRGB result after exposure, bloom,
  tonemapping, color grading and anti-aliasing.
- `.hdr`: linear scene radiance after the optional TAA resolve, but before
  exposure, bloom and tonemapping. This catches clipped or incorrectly scaled
  highlights that a final PNG could hide.

Both applications run at 640x360, one sample per pixel, a fixed 1/60 second
simulation step, hidden window, disabled runtime diagnostics and frame 16.
This removes wall-clock animation, overlay and timing-query noise from the
comparison.

## Running the suite

Build and test against the committed baseline images:

```powershell
cmake --build build --config Release --target golden_images
```

Artifacts are written to `build/visual-regression/<scene>/`. For every
comparison the suite retains:

- `<comparison>.diff.png`: absolute RGB error with an exposure-like gain;
- `<comparison>.heatmap.png`: black/blue for small error, yellow at the
  per-channel threshold, and red/white above it;
- `<comparison>.json`: reference/candidate paths, tolerances, failing pixel
  count, MAE, relative error, RMSE, maximum error, normalized error and PSNR;
- `opengl.log` and `vulkan.log`: renderer diagnostics from each capture.

The normal target never modifies a baseline. A missing image is a hard error.

## HDR-aware tolerance

PNG files are decoded from sRGB to linear light before comparison. Radiance
HDR files remain linear. A channel is accepted when:

```text
abs(reference - candidate)
    <= 0.015 + 0.04 * max(abs(reference), abs(candidate), 0.05)
```

The absolute term prevents noise in dark pixels from being exaggerated. The
relative term scales naturally for values above 1.0, so bright HDR highlights
are compared by proportional rather than 8-bit error.

Backend-specific regression checks allow at most 1% failing pixels and a mean
normalized error of 0.20. Direct OpenGL/Vulkan parity keeps the same strict
per-channel formula, but permits at most 5% failing pixels and mean normalized
error 0.25. The wider pixel fraction is for localized triangle coverage,
shadow edges and GPU transcendental-instruction differences; it does not make
the per-pixel radiance threshold looser.

## Updating baselines

Only update golden images after confirming that the rendering change is
intentional:

```powershell
cmake --build build --config Release --target update_golden_images
```

This captures and replaces both OpenGL and Vulkan LDR/HDR references, then
immediately runs the complete parity suite. Inspect the final images, diff and
heatmap artifacts before committing files under `tests/golden/baselines/`.
Never update references solely to make an unexplained failure disappear.

Golden images can vary with GPU vendor and driver revisions. A production CI
lane should pin the adapter, driver, OS and color settings, and baseline
updates should record any intentional environment change.

## Standalone comparator

The comparison utility can also be used for arbitrary PNG or Radiance HDR
files:

```powershell
build\tests\Release\engine_visual_compare.exe reference.hdr candidate.hdr `
  --diff result.diff.png --heatmap result.heatmap.png --report result.json
```

It exits with `0` for pass, `1` when valid images exceed tolerance, and `2` for
invalid arguments, unreadable images, dimension mismatch or output errors.
All thresholds have command-line overrides; run without enough arguments to
see the complete option list.

## CTest/CI integration

GPU visual tests are opt-in because they require a working display session and
both graphics APIs:

```powershell
cmake -S . -B build -DENGINE_ENABLE_GPU_VISUAL_TESTS=ON
ctest --test-dir build -C Release -L visual --output-on-failure
```

The ordinary CPU unit suite still exercises HDR round-tripping, identical and
tolerated comparisons, intentional failure detection, and diff-image output
without requiring a GPU.
