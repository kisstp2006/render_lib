# Golden-image baselines

The committed `*.opengl.png` and `*.vulkan.png` files are final
display-referred sRGB references. The matching `.hdr` files contain the
linear, pre-exposure and pre-tonemap scene color. Generate or intentionally
replace them with:

```powershell
cmake --build build --config Release --target update_golden_images
```

Always inspect the generated files under `build/visual-regression`, especially
the `*.diff.png`, `*.heatmap.png`, and JSON metric reports, before accepting a
baseline update. Do not update a golden image merely to hide an unexplained
rendering change.
