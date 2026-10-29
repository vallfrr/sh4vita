# Pack privé de test — 27 octobre 2026

`build/release/sh4vita-tester-v131-experimental.zip` contient le VPK expérimental, les exécutables `sh4prep` Windows x86-64, Linux x86-64 et macOS ARM64 déjà utilisés dans le pack précédent, une notice courte et les SHA-256 des fichiers. Aucun fichier GOG n'y est inclus. Le ZIP reste local : ce document n'est pas une Release publique.

Le VPK est compilé avec `SH4_TEST_OPTIMIZATIONS=ON`, `SH4_GUEST_MB=200`, la bibliothèque vitaGL isolée qui préserve la profondeur et le cache GPU ABI r1 validé (500 enregistrements). Les optimisations natives de palettes, sommets, UV, collisions, segments, liste de sphères, ADPCM et paquets audio sont activées, comme la conservation du préchargement et le saut du rejeu d'interpolation pour une image trop volumineuse. `env.txt` peut désactiver chaque option pour isoler un défaut. `SH4_TEX32` reste coupé en raison de la marge VRAM observée ; les textures restent au réglage de qualité actuel.

Ces voies ont été qualifiées séparément sur PC et Vita, avec des comparaisons d'images et de trajectoires documentées dans les rapports de performance. **Leur combinaison, les longues parties et toutes les cinématiques ne sont pas encore qualifiées.** Les corruptions intermittentes rapportées sur d'autres Vita et l'échec mémoire de l'ascenseur ne sont pas déclarés corrigés. Ce paquet sert à détecter les interactions entre options ; il ne constitue pas une version stable.

Pour reproduire le VPK à partir des sources de la branche expérimentale, il faut les fichiers `port/gen` issus du GOG v5 local, la bibliothèque vitaGL isolée avec cache et le répertoire compilé validé. Exemple depuis WSL, dans le dépôt :

```sh
B=/home/val/build/sh4vita-tester-opt bash port/build.sh \
  -DSH4_GUEST_MB=200 -DSH4_TRACE_CALLS=OFF -DSH4_PROFILE=OFF \
  -DSH4_TEST_OPTIMIZATIONS=ON \
  -DSH4_VITAGL_LIBRARY=/home/val/src/vitaGL-sh4-cgcache/libvitaGL.a \
  -DSH4_REQUIRE_SHADER_CACHE=ON \
  -DSH4_SHADER_CACHE_DIR=/mnt/j/sh4vita/build/room-audit/vpk-cache-v55
python tools/package_tester.py --vpk build/sh4.vpk \
  --output build/release/sh4vita-tester-v131-experimental.zip
```

La validation du paquet vérifie son intégrité, les trois préparateurs connus et les 500 entrées GPU dans le VPK. Le démarrage sur Vita doit confirmer `[build] experimental tester optimizations active` et des accès `[shader-cache] hit bundle abi=r1`. Les testeurs sauvegardent `sh4log.txt` avant toute relance lorsqu'un problème apparaît ; un crash requiert aussi le fichier `psp2core-*.psp2dmp` correspondant.

Contrôle effectué sur la Vita de développement avec **l'eboot exact extrait du ZIP final** : 35 secondes de démarrage/menu et démonstration, journal sans erreur fatale, plusieurs chargements depuis le cache GPU, et premières exécutions des voies ADPCM et paquets audio. Ce contrôle ne couvre pas encore une partie ni les autres voies natives. L'eboot original (`a00ef94d…`), la sauvegarde originale (`04315d28…`) et la configuration ont été restaurés et vérifiés ; le jeu normal a été relancé. SHA-256 du ZIP final : `d839a8285f41770ee0b7106dc55aa5d23e8bb5c8872a2d3041dd8bebd6690032`.
