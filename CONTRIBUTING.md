# Contributing

## Development setup

Install the build dependencies listed in the [README](README.md), plus `clang-tidy`, `doxygen` and [pre-commit](https://pre-commit.com/):

```bash
pre-commit install
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Layout

Public headers are in `include/waterlinkedsonar/`, grouped by concern: `http/` for the HTTP API client, `udp/` for the data stream, `rip/` for the Range Image Protocol and image conversions, and `ntp/` for the SNTP query, while `detail/` holds the vendored span implementation. `src/` mirrors the four concern directories and also holds the private headers, which the tests include directly. `test/` mirrors the same directories, with shared test helpers in `test/support/` and test data in `test/data/`.

## Style

- `clang-format` (`.clang-format`) and `gersemi` format the code through pre-commit.
- CI runs `clang-tidy` with the checks in `.clang-tidy`; run it locally with `git ls-files '*.cpp' | xargs clang-tidy -p build`.
- Every public declaration has an LLVM-style `///` Doxygen comment. `doxygen` builds the reference into `build/html` and fails on any warning.

## Pull requests

- Open pull requests against `main`. CI must pass.
- Follow [Conventional Commits](https://www.conventionalcommits.org/) for commit messages (`feat:`, `fix:`, `docs:`, `ci:`, `chore:`).
- Add or update tests for behaviour changes and note user-visible changes under `Unreleased` in [CHANGELOG.md](CHANGELOG.md).
