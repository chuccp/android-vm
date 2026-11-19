# Vendored JRE source

This directory records the OpenJDK 11 source provenance and licenses used by
the Guest runtime. It is not a host JRE.

- Upstream: `https://github.com/openjdk/jdk11u`
- Tag: `jdk-11.0.34+0`
- Commit: `7110e0fe7dd05da3795f835d78b6761dfcb1fd29`
- License: GPLv2 with the OpenJDK Classpath Exception

The vendored `java.base/share/classes` source has been merged into
`runtime-libs/src/main/java`. Existing Guest-adapted overrides win over the
upstream source when paths overlap.

License notices are kept in this directory instead of being repeated in
every source file. Source comments and documentation comments are removed to
keep the preloaded Guest library compact.

The Java compiler only receives classes named by
`runtime-libs/runtime-components.json`; the rest remains source-only.
HotSpot, native C/C++, platform binaries, build tooling and JDK utilities are
not vendored.
