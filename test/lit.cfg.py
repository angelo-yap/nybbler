# lit configuration for the Nybbler test suite.
#
# Discovers *.ll tests and runs them through opt-16 with the freshly built
# plugin loaded. The plugin path and tool paths are supplied by the generated
# lit.site.cfg.py (see CMakeLists.txt / lit.site.cfg.py.in); when running lit
# directly against the source tree without CMake, sensible defaults are used.

import os

import lit.formats

config.name = "Nybbler"
config.test_format = lit.formats.ShTest(execute_external=False)
config.suffixes = [".ll"]
config.test_source_root = os.path.dirname(__file__)

# Tool substitutions. Prefer the versioned LLVM 16 binaries from the spec.
opt = getattr(config, "opt_tool", "opt-16")
filecheck = getattr(config, "filecheck_tool", "FileCheck-16")

# Path to libNybbler.so. Set by lit.site.cfg.py after a CMake build; fall back
# to a build/ dir next to the source tree for convenience.
plugin = getattr(config, "nybbler_plugin", None)
if plugin is None:
    plugin = os.path.join(
        os.path.dirname(config.test_source_root), "build", "libNybbler.so"
    )

config.substitutions.append(("%opt", opt))
config.substitutions.append(("%FileCheck", filecheck))
config.substitutions.append(("%nybbler", plugin))
