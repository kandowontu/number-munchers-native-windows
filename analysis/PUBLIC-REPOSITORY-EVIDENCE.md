# Public repository evidence boundary

The local preservation workspace contains more than 6 GB of raw DOSBox video,
decoded PPM/PNG/JPEG frames, comparison tables, and transient diagnostic
outputs. Those generated artifacts are deliberately excluded from Git because
they are not required to build, run, or test the native application and exceed
ordinary Git hosting limits.

The public repository retains the written audits, compact disassemblies and
relocated audit images, source code, deterministic framebuffer/audio hashes,
all automated tests, and every embedded runtime asset required by a clean
build. The original DOS executables and full disk-image/ZIP preservation media
are not runtime dependencies and are not committed.

The release executable is independently inspected from an isolated temporary
directory by `production_artifact_self_containment_test`; that gate verifies
all 31 embedded resources and rejects non-Windows dynamic dependencies without
executing the GUI.
