# Potion Fast Sans generation

These development scripts reproducibly generate and verify the four production
faces in `assets/vendor/fast-font/fonts`.

The source files are the four `Fast_Sans_*.otf` faces from Fast-Font commit
`aeae0775d9251365eae3b133cbf26ce0366f6108`. The generator verifies every
source SHA-256, preserves existing `ccmp` lookups, appends the contextual
Fast-Font lookups, and assigns the private `Potion Fast Sans` family metadata.
Generated production fonts must not be hand-edited.

From this directory:

```sh
python3 generate_fonts.py --source-dir /path/to/Fast_Sans --output-dir ../../assets/vendor/fast-font/fonts
python3 verify_fonts.py --source-dir /path/to/Fast_Sans --font-dir ../../assets/vendor/fast-font/fonts
```

FontTools and optional `hb-shape` are host-side development dependencies. They
are not packaged for the Kindle.
