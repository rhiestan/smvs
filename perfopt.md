# Performance optimizations (`perfopt` branch)

This fork of [SMVS](https://github.com/flanggut/smvs) (Shading-aware
Multi-view Stereo) adds a modern build and performance optimizations. The work
lives on the `perfopt` branch (the default branch of this fork); `master`
tracks the upstream repository, so it can be synchronized with it.

Timings below were measured on a 32-thread Windows machine (Clang 22) and are
compared with the original `-msse4.2` build.

## Build and CI

- CMake presets for Windows (Clang, MSVC target), Linux and macOS, with Ninja
  and vcpkg. Builds against the current MVE.
- **No SSE4 requirement.** The baseline is plain x86-64 and SIMD code uses
  [Google Highway](https://github.com/google/highway). The SGM cost
  aggregation keeps the packed 16-bit layout and the algorithm of the SSE code,
  written as plain loops; its output is byte-identical to the SSE build.
  Popcount uses the compiler builtin.
- GitHub Actions builds and tests on Windows, Linux and macOS (replacing the
  Travis configuration). Pushing a tag packages `smvsrecon` and `simplify`
  (with the OpenMP runtime) and publishes a GitHub release.

## Optimizations

- **SGM aggregation**: the minimum over the previous costs is computed in
  O(n) instead of O(n^2), with the same result, and raw pointers replace
  bounds-checked access.
- **Gauss-Newton step**: the gradient and Hessian accumulation and the surface
  regularization run in a runtime-dispatched Highway kernel. It uses no fused
  multiply-add, so the result does not depend on the instruction set. Unit
  tests compare it with the scalar formulas.
- Results: a small scene (kermit) went from 64 s to about 14 s, and a larger
  one (sceaux) from 172 s to 132 s.
