#ifndef PPC_SUPPORT_VERSION_HPP
#define PPC_SUPPORT_VERSION_HPP

// Every version number PPC reports originates here.
//
// These are six genuinely different things and conflating them causes real
// bugs, so each gets its own constant with an explicit meaning:
//
//   kCompilerVersion     this build of PPC. Changes every release, including
//                        pure bug-fix releases that change nothing else.
//   kLanguageVersion     the PunPun source language PPC accepts. Says nothing
//                        about how complete the implementation is.
//   kLanguageAbi         epoch for source-level compatibility. Two compilers
//                        with the same value accept the same programs.
//   kRuntimeAbi          epoch for the pp_* C runtime interface. Generated code
//                        and libppcrt must agree on this or they must not link.
//   kPackageFormat       on-disk package layout epoch.
//   kLockfileFormat      Punpun.lock layout epoch.
//   kCacheEpoch          PPC-internal cache format. Bumped whenever anything
//                        that could change a cached artifact changes, which
//                        includes internal representations no other tool sees.
//
// The rule that keeps these honest: a version here describes what this
// repository actually implements, never what the roadmap intends. Do not raise
// kLanguageVersion because a milestone was announced.

namespace ppc {
namespace version {

/// This build of the compiler.
inline constexpr const char *kCompilerVersion = "1.4.0";

/// The PunPun language version PPC targets.
///
/// PPC implements the 1.0 stable language surface: generics with
/// monomorphization, algebraic enums with exhaustiveness checking, ownership and
/// borrow checking, checked arithmetic, structured task groups, verified HTTPS,
/// and the native GUI foundation. 1.4 adds function types and literals,
/// iteration over a sequence, and contracts usable as types with dispatch on
/// them. All three are additions: 1.4 remains source-compatible with the stable
/// 1.0 language surface, so the language version and ABI stay at 1.0/1. The
/// migration dialect still parses, and now warns.
inline constexpr const char *kLanguageVersion = "1.0";

/// Source-compatibility epoch, per spec/1.0/abi-and-packages.md.
inline constexpr int kLanguageAbi = 1;

/// C runtime interface epoch. Must match PUNPUN_RUNTIME_ABI_VERSION in
/// runtime/ppcrt.h; a static assertion in driver.cpp enforces that, so the two
/// cannot drift silently.
inline constexpr int kRuntimeAbi = 1;

inline constexpr int kPackageFormat = 1;
inline constexpr int kLockfileFormat = 1;

/// Internal cache format epoch.
///
/// Bump this whenever a change could make a previously cached artifact wrong:
/// MIR or bytecode layout, codegen output, runtime source, default flags, or
/// the fingerprint algorithm itself. Bumping unnecessarily costs one rebuild.
/// Failing to bump when required produces a stale artifact, which is far worse,
/// so when in doubt, bump.
inline constexpr int kCacheEpoch = 3;

/// Which language features this build actually implements, as opposed to what
/// the language defines. Reported by `ppc language-info` so a build system can
/// see the difference between "the language has this" and "this compiler does".
inline constexpr const char *kImplementationStatus = "stable-1.4-compiler";

}  // namespace version
}  // namespace ppc

#endif
