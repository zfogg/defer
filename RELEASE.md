# Recast release package

Extract the ZIP anywhere. It contains static and shared Recast libraries,
headers, the generated `Recast.cmake` integration module, pkg-config files,
the `recast` tool, and LLVM licensing information.

Use the matching compiler family and architecture for the archive. Set
`CMAKE_PREFIX_PATH` to the extracted directory for CMake consumers, or set
`PKG_CONFIG_PATH` to `lib/pkgconfig`. Linux musl archives are fully static;
macOS and Windows retain only operating-system runtime dependencies.
