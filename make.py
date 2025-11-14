from aods import (
    Context,
    BuildType,
    run,
    pkgconfig_get_variable,
    assert_installed,
    default_flags,
)

import sys
import os

build_type = BuildType.RELEASE if "--release" in sys.argv else BuildType.DEBUG

ctx = Context.default("main")

ctx.add_include(
    [
        "include",
        "util",
        "/usr/local/include/w",
        ctx.build_dir,
    ]
)

ctx.add_dependency("wayland-client")

ctx.add_source(["src/" + f for f in os.listdir("src") if f.endswith(".c")])

ctx.add_flag(default_flags(build_type))
ctx.add_flag("-lw")

ctx.build()
