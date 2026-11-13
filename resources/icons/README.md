# Application icon

`emu51.png` is the approved original artwork: a charcoal microchip with amber
seven-segment **51** digits, generated with OpenAI's built-in image generation
tool for emu51. The same artwork is used on all desktop platforms.

The checked-in derivatives are:

- `emu51.icns`: macOS bundle/Finder/Dock icon, up to 1024 pixels.
- `emu51.ico`: Windows executable icon, with 16–256 pixel frames.
- `emu51-*.png`: Linux hicolor theme icons and conversion inputs.
- `emu51-*.rgba`: straight-alpha RGBA window icons embedded into the GLFW
  frontend on Windows and Linux, independent of the current working directory.

To regenerate the derivatives on macOS, run from the public source root:

```sh
swift scripts/generate_icons.swift
```

This uses system CoreGraphics/ImageIO and `iconutil`. Normal application builds
on every platform consume the checked-in files and need no conversion tools.
