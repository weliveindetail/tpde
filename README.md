# TPDE Compiler Back-End Framework

TPDE is a fast compiler back-end framework that adapts to existing SSA IRs.
The primary goal is low-latency compilation while maintaining reasonable (`-O0`) code quality, e.g., as baseline compiler for JIT compilation or unoptimized builds.
Currently, TPDE only targets ELF-based x86-64 and AArch64 (Armv8.1) platforms.

This repository contains:

- TPDE: the core compiler framework.
- TPDE-Encodegen: a utility for easing the use of TPDE by deriving code generators through LLVM's Machine IR.
- TPDE-LLVM: a standalone back-end for LLVM-IR, which compiles 10--20x faster than LLVM -O0 with similar code quality, usable as library (JIT or object file) and as tool (`tpde-llc`). We provide a [patch](llvm.ab51eccf88f5.patch) for integrating into Clang/Flang 19.1 (adds `-ftpde`).

Consult the [documentation](docs/) for more information.

### Publications

- Tobias Schwarz, Tobias Kamm, and Alexis Engelke. TPDE: A Fast Adaptable Compiler Back-End Framework. [arXiv:2505.22610](https://arxiv.org/abs/2505.22610) [cs.PL]. 2025.

### License

Generally: Apache-2.0 WITH LLVM-exception. (Detailed license information is attached to every file.)
