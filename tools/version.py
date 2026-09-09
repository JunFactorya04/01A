"""
PlatformIO pre-build script: stamps the firmware with `git describe` so the
Setting > Info screen (and the legacy factory-test screen) always shows the
actual build's version instead of a hand-typed literal someone has to
remember to bump every release (this is exactly how "v1.3" ended up shipping
inside a v1.4 release build).

Exactly on a release tag -> clean "v1.4". Any commits past a tag, or a dirty
working tree -> "v1.4-3-gabc1234" / "v1.4-dirty", so a non-release build can
never be mistaken for one. Falls back to "dev" if git isn't available at all
(e.g. building from a downloaded zip with no .git folder) so the build can
never fail because of this script.
"""
Import("env")

import subprocess


def get_version():
    try:
        out = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            stderr=subprocess.DEVNULL,
        )
        return out.decode().strip()
    except Exception:
        return "dev"


version = get_version()
print(f"[version.py] GEOPIX_FW_VERSION = {version}")
env.Append(BUILD_FLAGS=[f'-D GEOPIX_FW_VERSION=\\"{version}\\"'])
