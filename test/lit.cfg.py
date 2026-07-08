# lit configuration for the Nybbler test suite.
#
# Discovers *.ll tests and runs them through opt-22 with the freshly built
# plugin loaded. The plugin path and tool paths are supplied by the generated
# lit.site.cfg.py (see CMakeLists.txt / lit.site.cfg.py.in); when running lit
# directly against the source tree without CMake, sensible defaults are used.

import os
import sys

import lit.formats

config.name = "Nybbler"
config.test_format = lit.formats.ShTest(execute_external=False)
config.suffixes = [".ll", ".test"]
config.test_source_root = os.path.dirname(__file__)

# Tool substitutions. Prefer the versioned LLVM 22 binaries from the spec.
opt = getattr(config, "opt_tool", "opt-22")
filecheck = getattr(config, "filecheck_tool", "FileCheck-22")
lli = getattr(config, "lli_tool", "lli-22")

# Path to libNybbler.so. Set by lit.site.cfg.py after a CMake build; fall back
# to a build/ dir next to the source tree for convenience.
plugin = getattr(config, "nybbler_plugin", None)
if plugin is None:
    plugin = os.path.join(
        os.path.dirname(config.test_source_root), "build", "libNybbler.so"
    )

tools_dir = os.path.join(os.path.dirname(config.test_source_root), "tools")
shape_dir = os.path.join(config.test_source_root, "shape")
diff_dir = os.path.join(config.test_source_root, "diff")

config.substitutions.append(("%opt", opt))
config.substitutions.append(("%FileCheck", filecheck))
config.substitutions.append(("%lli", lli))
config.substitutions.append(("%nybbler", plugin))
config.substitutions.append(("%python", sys.executable))
config.substitutions.append(("%diff_runner", os.path.join(tools_dir, "diff_runner.py")))
config.substitutions.append(("%coverage_check", os.path.join(tools_dir, "coverage_check.py")))
config.substitutions.append(("%shape_dir", shape_dir))
config.substitutions.append(("%diff_dir", diff_dir))
