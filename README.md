# Established

Established is an English-natural programming language compiled into a Block Tree and executed by a native C++ runtime. Source files use the `.esther` extension.

## Build

With CMake:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Or compile directly with a C++17 compiler:

```sh
c++ -std=c++17 -O2 -DNDEBUG esther.cpp -o esther
```

## Run

```sh
./build/esther run examples/hello.esther
./build/esther check examples/classes.esther
./build/esther ir examples/functions.esther
```

The CLI has three modes: `run` executes a program, `check` parses and validates it, and `ir` prints its Block Tree. Imports from a file are resolved relative to the importing `.esther` file.

## Install from npm

After the npm package has been published:

```sh
npm install --global established-esther
esther run examples/hello.esther
```

The npm package downloads the matching native executable during installation. Supported targets are macOS x64/arm64, Linux x64, and Windows x64. Node.js 20 or newer is required for the installer and CLI wrapper.

## Examples

- `examples/hello.esther`
- `examples/number-guessing.esther`
- `examples/shopping-list.esther`
- `examples/functions.esther`
- `examples/classes.esther`

The runtime uses only the C++ standard library. Its standard-library modules cover console input/output, file streams, terminal drawing, and cleanup; terminal graphics are intentionally text-based in this first runtime.

## Publish

Push this project to your GitHub repository, configure npm trusted publishing for the `established-esther` package and `.github/workflows/release.yml`, then push a version tag such as `v0.1.0`. The workflow builds platform binaries, creates a GitHub Release, and publishes the npm package with provenance. No npm token is stored in the repository. See `PACKAGING.md` for first-time setup.