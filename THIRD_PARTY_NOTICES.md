# Third-party notices — SH4 Vita

The port uses third-party software. Its authors retain their copyrights and the applicable terms below. These notices do not claim ownership of Silent Hill 4, its original executable, resources, or generated translations. Players supply their own supported GOG installation.

## Graphics and shader services

- **vitaGL**, Rinnegatamante and contributors: [source](https://github.com/Rinnegatamante/vitaGL). The source files identify LGPL version 3 or later. See `LICENSES/LGPL-3.0.txt` and `LICENSES/GPL-3.0.txt`. The SH4-specific modifications are supplied as patches under `port/patches/`; the exact patch/library combination must be matched to each release. No claim is made that an arbitrary current checkout reproduces an older VPK.
- **vitaShaRK**, Rinnegatamante and contributors: [source](https://github.com/Rinnegatamante/vitaShaRK), LGPL version 3 or later according to source headers. See `LICENSES/vitaShaRK-LGPL-3.0.txt` and the GPL text. Its use is separate from the user's proprietary `libshacccg.suprx`, which is not supplied by this project.
- **SceShaccCgExt**: [source](https://github.com/Rinnegatamante/SceShaccCgExt), repository license GPL version 3. See `LICENSES/SceShaccCgExt-GPL-3.0.txt`. This component is included in the current local executable; its exact installed revision and the implications for distributing the combined executable remain under review. Do not treat it as LGPL solely because vitaShaRK uses it.

## Bundled and linked support libraries

- **pl_mpeg**, Dominic Szablewski: [source](https://github.com/phoboslab/pl_mpeg), MIT; see `LICENSES/MIT.txt` and `port/src/third_party/pl_mpeg/NOTICE.md`. The bundled decoder has SH4 port modifications, including Layer I audio support; it must not be represented as an unchanged upstream version.
- **TLSF**, Copyright (c) 2006–2016 Matthew Conte: [source](https://github.com/mattconte/tlsf), BSD 3-clause terms reproduced in `LICENSES/TLSF-BSD-3-Clause.txt` from the bundled header.
- **SDL2**, Sam Lantinga and contributors: [source](https://github.com/libsdl-org/SDL), zlib license; see `LICENSES/SDL2-Zlib.txt` for the installed Vita package's notice. Desktop builds and preparer dependencies require their own matching inventory.
- **math-neon**, Lachlan Tychsen-Smith, Vita port by Rinnegatamante: [source](https://github.com/Rinnegatamante/math-neon); MIT notice reproduced in `LICENSES/math-neon-MIT.txt`.
- **VitaSDK/vita-headers** contributors: [source](https://github.com/vitasdk/vita-headers); see `LICENSES/vita-headers-LICENSE.txt`. Compiler runtime, C/C++ runtime, zlib, taiHEN stub and transitive library notices are still being inventoried for the release toolchain.

This notice collection is a work in progress. The documents must accompany the eventual binary distribution, together with the required corresponding sources or relinking materials. The current collection alone does not certify a VPK as ready for publication or VitaDB acceptance. See `docs/DISTRIBUTION-LICENSE-AUDIT.md`.
