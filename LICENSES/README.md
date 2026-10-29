# Third-party license texts

These documents preserve third-party terms. They do not license the SH4 port as a whole, the original game, or generated game code. No project-wide license has been selected by adding this directory.

| File | Provenance |
|---|---|
| `GPL-3.0.txt`, `LGPL-3.0.txt` | `COPYING` and `COPYING.LESSER` from vitaGL checkout `2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8`. |
| `vitaShaRK-LGPL-3.0.txt` | [vitaShaRK LICENSE](https://github.com/Rinnegatamante/vitaShaRK/blob/df24065e65098b2d1ac533760109ad4367573f28/LICENSE). Reference text; the exact installed archive revision is not yet established. |
| `SceShaccCgExt-GPL-3.0.txt` | [SceShaccCgExt LICENSE](https://github.com/Rinnegatamante/SceShaccCgExt/blob/102192366c25fe7b8fec073d665494b6172605c3/LICENSE). Reference text; the exact installed archive revision is not yet established. |
| `SDL2-Zlib.txt` | The installed VitaSDK SDL2 `share/licenses/SDL2/LICENSE.txt`. Its revision header reports `SDL-release-2.32.8-0-g98d1f3a45`; desktop SDL is a separate dependency. |
| `TLSF-BSD-3-Clause.txt` | Copyright and terms copied from the bundled `port/src/third_party/tlsf/tlsf.h`, with comment prefixes removed. |
| `math-neon-MIT.txt` | License comment from [math_acosf.c](https://github.com/Rinnegatamante/math-neon/blob/0faab814782c071ff4015527f1ca955ab1ccc470/source/math_acosf.c). Exact installed archive revision still needs verification. |
| `vita-headers-LICENSE.txt` | [vita-headers LICENSE.md](https://github.com/vitasdk/vita-headers/blob/ebc8f4f7ac8313fe4f74ca95724116b224bad184/LICENSE.md), matching the SDK's recorded revision. |
| `MIT.txt` | Standard MIT license text from the [SPDX license list](https://github.com/spdx/license-list-data/blob/main/text/MIT.txt), without assigning ownership to the port maintainer. Used with the existing pl_mpeg attribution in `THIRD_PARTY_NOTICES.md`. |

This is an initial collection, not a complete dependency/source-compliance certification. See [the audit](../docs/DISTRIBUTION-LICENSE-AUDIT.md) for unresolved source, linking, generated-code and packaging questions. Merely including license texts does not satisfy all source or relinking obligations.
