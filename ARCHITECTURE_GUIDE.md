# Architecture Guide Reference

## Location
`ARCHITECTURE.md` in the project root.

## Purpose
Comprehensive documentation of all implemented features, techniques, libraries, and patterns used in this codebase. **Must be followed when adding new features** to ensure consistency.

## Key Sections

1. **Build System** - CMake conventions, SIMD flags, optional libraries
2. **Coding Patterns** - Config structs, enum classes, callbacks, object pools, RAII
3. **Performance Optimizations** - SIMD, memory mapping, zero-allocation parsing, caching
4. **Data Structures** - Core types and enums
5. **Strategy Patterns** - OnTick interface, Config presets, trading operations
6. **Testing Patterns** - Test structure, parallel testing with `RunWithTicks()`
7. **File Organization** - Directory structure conventions

## Critical Patterns to Follow

### New Header Files
- Use `backtest` namespace
- Include guards: `#ifndef FILENAME_H`
- Doxygen documentation
- Config struct with presets if configurable

### Performance
- SIMD path for 8+ elements with scalar fallback
- Memory-mapped I/O for large files
- Object pools for frequently allocated objects
- Pre-allocated buffers in hot paths
- Dirty cache pattern for SIMD caches

### Testing
- Step-by-step output with progress
- Use `RunWithTicks()` for parallel testing
- Pass/fail return codes

## When to Update ARCHITECTURE.md
- Adding new coding patterns
- Introducing new optimization techniques
- Changing project structure
- Adding new types of components

---
*Created: 2025*
