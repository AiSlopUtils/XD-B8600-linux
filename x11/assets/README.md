# Holo wallpaper asset

`holo-wallpaper-528x320.rgb565` is a device-ready derivative of
`themes/holo/wall.png` from
[lcpz/awesome-copycats](https://github.com/lcpz/awesome-copycats) at commit
`affb71fa9ea69460208590f90383b3b0e8bab9f0`.

- Upstream file: 1920x1080 PNG
- Upstream SHA-256: `561577c4b1a258cde487630357f332cfb42b5067afd4dcfa1feccecaf8b5bd02`
- Device file: 528x320, packed RGB565, most-significant byte first
- Device SHA-256: `76b13fe73d7e527f0906477a1d14f19d9854295de07c076ece1d5bc5b5c5c948`
- Exact size: 337,920 bytes

The derivative is center-cropped after a Lanczos cover-scale, then quantized
to the XD-B8600 framebuffer format. It is loaded in small strips directly by
the patched window manager, so the target needs no PNG decoder or wallpaper
daemon.

The upstream awesome-copycats project, including this adapted theme asset, is
licensed under
[Creative Commons Attribution-ShareAlike 4.0 International](https://creativecommons.org/licenses/by-sa/4.0/).
Attribution: Holo theme by Luca CPZ (`lcpz/awesome-copycats`). This derivative
is distributed under the same CC BY-SA 4.0 terms and is not covered by the
project's 0BSD license.

For reference, the conversion used during development was:

```sh
ffmpeg -i wall.png \
  -vf 'scale=528:320:force_original_aspect_ratio=increase:flags=lanczos,crop=528:320,format=rgb565le' \
  -c:v rawvideo -f rawvideo holo-wallpaper.le.rgb565
dd if=holo-wallpaper.le.rgb565 of=holo-wallpaper-528x320.rgb565 conv=swab
```
