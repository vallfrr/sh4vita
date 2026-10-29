# 60 Hz display experiments (2026-10-26)

This is an experimental branch. The target is smooth 60 displayed frames per second with the game logic stable at 30 Hz. Neither target is achieved in the difficult rooms below.

## Native model palette follow-up

The opt-in `SH4_NATIVE_PALETTE=1` path batches the original twenty model-matrix uploads without changing their final constants or skipped slots. It passes 20,000 comparisons against the original GOG x86 instructions, the 23-frame PC reference, six paired PC images and eight paired Vita images. In the same-binary Vita comparison, Past improves from 44.121 to 41.142 ms stationary and from 40.939 to 39.062 ms with movement and camera sweep. This is a modest gain, still above 33.33 ms. The option remains off by default and is not in the tester ZIP. See [the French contract, evidence and next profiling target](NATIVE-MODEL.md).

The follow-up isolates the retained bounds/UV passes into native `-O2` functions with range/alias fallback and unchanged double arithmetic. With palette batching enabled, Past measures 41.498 ms for the retained passes, 40.890 with isolated bounds only, 40.042 with isolated UV only, and 39.757 with both. These modest gains do not reach 30 Hz. The 20,000 differential cases compare against the retained native passes; six PC images and each candidate's two Vita images remain identical. `SH4_NATIVE_VERTICES=1|bounds|uv` is experimental and off by default. Broader Vita gameplay qualification is pending.

## Method

The same scripted Vita tour revisits Water Prison 4:11, Past 8:24, and Subway 2:14. `SH4_INTERP=0` isolates logical frame cost. Low-volume diagnostics remain enabled to measure performance. These stationary room samples are not a full playthrough or a measurement of the physical display cadence. After each trial the runner verifies restoration of the original save (SHA256 `04315d28a0639804cf37c1c9336278a8ae46a300133ae1daf58c4c99dcccca38`), old qualified eboot (SHA256 `a00ef94dbab228565dedbb188a802ae12ef5c21f9e487b65d4c1e995b3789527`), and normal launch. Local evidence is under ignored `build/room-audit/perf60-v84/`.

| Vita build | Water 4:11 | Past 8:24 | Subway 2:14 | Decision |
| --- | ---: | ---: | ---: | --- |
| v83 reference, repeated | 41.706 ms | 54.046 ms | 35.505 ms | Baseline |
| `F_004287f0` at `-O2` | 42.232 ms | 52.838 ms | 35.862 ms | Rejected: two regressions |
| Inline native MMX copy at four call sites in `F_004287f0` | 41.358 ms | 52.766 ms | 35.405 ms | Retained experimentally; too small to meet goal |
| Whole-program GCC LTO, on top of inline copy | — | 53.628 ms | — | Rejected: slower in the difficult room |

The inline-copy trial produced six byte-identical Vita captures against the repeated reference and 23/23 exact PC regression frames. The LTO trial's sampled image was visually equivalent to the inline-copy image, but captured at a different moment. None of these tests establishes game-wide non-regression. LTO was built only in an isolated local build, so no LTO build flag was promoted.

## What the Galaxy port actually suggests

The [Galaxy Vita technical notes](https://github.com/CBOC-Vita-Lab/galaxy-vita/blob/main/docs/TECHNICAL.md) document native ARM compilation of the Petari decompilation, ThinLTO, retained graphics state, thread splitting, and fewer GPU scenes. The [Galaxy Vita README](https://github.com/CBOC-Vita-Lab/galaxy-vita/blob/main/README.md) reports variable performance, including 43 fps average in its busy Comet Observatory scene; its existence does not establish constant 60 fps for SH4. A source comparison against the current [Petari repository](https://github.com/SMGCommunity/Petari) confirms that the port has a separate Vita renderer and modified decompiled code, but Petari HEAD postdates the port snapshot; raw file differences cannot all be called optimizations.

SH4 already separates game, graphics, and preparation work across Vita cores and ships a shader cache. In Past 8:24, sampled profiling puts about 33% of the game-thread samples in generated `F_004287f0`, with the native MMX copy wrapper around 13.5%. The profile itself slows execution and is useful for ranking targets only. This makes the translated x86 geometry path a stronger target than GPU resolution for that room.

The Galaxy state-cache idea was checked with a diagnostic-only Vita build: in a representative Past 8:24 window, only 156 of 17,940 vertex-array uniform calls had the same key and value; their 72,384 bytes were 0.27% of 27,282,528 bytes submitted. The existing SH4 draw path already suppresses many identical uniform generations. A second uniform-array cache would have little room to improve this scene. Its diagnostic code was removed.

With interpolation enabled, Past 8:24 overflows the default 1536 KB replay buffer. An opt-in 2048 KB buffer retained intermediate submissions but increased logical frame time to about 61.3 ms, so it was not promoted. Sustained 60 displayed fps still requires a larger reduction in guest geometry cost and replay cost, verified in normal gameplay and with image comparisons.

## Further isolated trials

The following runs all target **Past 8:24** (`w8_s24`), with interpolation disabled, the same 180-frame settle and 450-frame dwell, and the same Vita runner. The one-room analyzer currently labels its first result "water 4:11" because its fixed three-room name list starts there; the captured room and tour file confirm that these rows are Past 8:24. The repeated reference is 52.766 ms. Timings are indicative for this scripted camera and include low-volume diagnostics, so sub-millisecond differences should be treated as noise.

| Trial | Mean logical frame | Evidence and decision |
| --- | ---: | --- |
| Specialize the hot 64-byte guest copy to non-overlapping `memcpy` | 52.490 ms | ~0.28 ms: rejected |
| Compile all generated guest C at `-O3` | 49.664 ms | ~3.1 ms: rejected as insufficient for the goal; no game-wide image validation |
| Defer vertex shader constant writes until draw | 55.526 ms | PC 23/23 exact, but Vita slower; rejected |
| Run the D3D8 preparation on the game thread (`SH4_PREP=lazy`) | 67.987 ms | Current core separation is beneficial; rejected |
| Bypass the x86 indirect dispatch for known model vertex constant calls | 54.562 ms | PC 23/23 exact, Vita slower; rejected |
| `-Ofast` on generated `F_004287f0` only | 54.497 ms | Slower, with possible floating-point semantic risk; rejected without promotion |
| Rewrite three game COM forwarding functions as native C | 53.573 ms | PC 23/23 exact, Vita slower; rejected |
| Keep frequently modified vertex buffers in GL VBOs | 52.735 ms | PC 23/23 exact; inline vertex copying fell from ~1,235 to ~860 KB/frame on Vita, but frame time did not improve; rejected |

Every Vita trial ended by restoring the original save and qualified eboot; each has a `restored.json` proof in the ignored local evidence directory. All rejected source changes were reverted, and the generated C was regenerated from the retained recompiler. No tested option has reached 30 logical fps in this room.

The model diagnostic observed **324 calls per frame** to `F_004287f0`: 238 distinct descriptors and 86 repeated descriptors with identical first 84 bytes within that frame. The repeated calls can still be separate render passes; skipping them would remove drawings. The routine traverses model groups, fills buffers, adjusts per-vertex coordinates, and submits D3D draws. The game thread is about 52.8 ms here, while the GL render thread is about 12 ms and the preparation thread around 20 ms; shifting preparation back onto the game thread made the result much worse. The required reduction is roughly **19.4 ms per logical image** to reach 33.3 ms. Rewriting or precomputing the vertex conversion needs exact dependency tracking for animations and state, then PC/Vita image comparison across multiple rooms. Editing the player's game data in the preparer alone would not change the already ported render/update algorithm.

## Native model vertex passes (experimental branch)

A PC instrumented run of Past 8:24 measured about **12,621 vertices per frame** in each of two loops inside `F_004287f0`: one bounds scan and one vertex copy/texture-coordinate conversion. The first scanned four extrema using x87 comparisons. Replacing that loop with native C in `recomp/x86rec.py` reduced Past from 52.766 to 48.328 ms in the three-room Vita tour. Two one-room PC captures, all six three-room Vita captures, and the 23-frame PC regression were byte-identical to the reference. The other Vita rooms stayed near their reference timings (Water 41.459, Subway 35.356 ms).

The second loop copied each 64-byte vertex and transformed two coordinates. A native C replacement in `recomp/model_uv_native.inc` preserves the original branch thresholds, arithmetic order, guest-stack call slots, and final x87 comparison status. The isolated trial produced two byte-identical PC captures, 23/23 identical PC regression frames, and two identical Vita captures. Its Past one-room result was 43.458 ms. After promoting it to the generator, the three-room Vita tour measured:

| Room | Before these two changes | Native bounds + UV | Difference |
| --- | ---: | ---: | ---: |
| Water Prison 4:11 | 41.358 ms | 41.597 ms | +0.239 ms (noise scale) |
| Past 8:24 | 52.766 ms | 43.199 ms | **-9.567 ms** |
| Subway 2:14 | 35.405 ms | 35.488 ms | +0.083 ms (noise scale) |

All **six Vita captures** in the three-room tour were byte-identical to the inline-copy reference. The original save and qualified eboot were verified restored after each Vita test (`native-bbox`, `native-bbox-gen3`, `native-uv`, `native-uv-gen3` under the local evidence directory). No fatal or GL error appeared in these runs. The changes remain on the experimental branch: Past still averages **43.2 ms**, about **9.9 ms above** the 33.3 ms target. Static camera samples and matching captures do not establish a constant 30 Hz rate or game-wide correctness during animated scenes and normal play. A fresh profile and broader dynamic-scene tests are required before release qualification.

A separate Past 8:24 tour with scripted movement and camera sweep measured **47.399 ms** for the reference and **40.303 ms** for the optimized build. Both Vita captures were byte-identical across builds. This view still exceeds 33.3 ms; scripted movement is not a full normal-play traversal. Both runners verified the original save and eboot on return.

A new sampled Vita profile after the two retained replacements still ranks `F_004287f0` first (33.8% of samples), followed by `dev_SetVertexShaderConstant` (11.2%), `dev_DrawIndexedPrimitive` (3.6%) and `dev_DrawIndexedPrimitiveUP` (3.3%). The sampler perturbs frame timing, so these percentages rank work rather than quantify the remaining milliseconds. A third native 12-byte attribute-copy loop produced identical PC/Vita captures but only **0.16 ms** in its one-room Vita comparison; it was removed. A stage-timing probe around nested model loops could not partition exclusive time reliably and was also removed. Further tiny copies or flag-expression tweaks are unlikely to close the gap; the next investigation needs a change to the amount of per-model work or the number of state submissions, with exact render comparisons and normal-play performance measurements.

An exact-value cache for `SetVertexShaderConstant` skipped unchanged writes and preserved two PC and two Vita captures, but the Past one-room mean worsened from **43.458 to 44.250 ms**. It was removed. A PC-only probe of the final UV conversion found 9,360 calls over 60 frames; 8,340 reused the same source address and vertex bytes, but **none** reused all transformation parameters. Caching the completed UV output would therefore miss or return an incorrect result. This diagnostic was removed. Neither experiment changed the retained branch implementation.

A corrected PC probe found that **8,760 of 9,360 model-bound calculations over 60 frames** reused the same source coordinates and input bounds. An experimental exact cache stored the four output bounds and a copy of every input XY pair, with a 2 MB allocation ceiling; each hit compared all coordinates before reuse. Two PC and two Vita captures stayed byte-identical, but the Past one-room Vita mean changed only from **43.458 to 43.346 ms**, below meaningful repeatability. The cache was removed: its memory and maintenance cost are not justified by this gain. The PC probe itself was removed from generated code. This shows that a high cache-hit rate does not necessarily reduce frame time when validation still scans all vertices.

I also tried always recording vertex-shader constant writes instead of comparing them first. Two PC captures remained identical, but the Past PC preparation stream rose from about **9,828 to 11,287 records per frame** in comparable windows. The Vita did not create a game log after the scripted launch, so no hardware performance conclusion was possible. The runner was stopped and verified restoration of the original save and eboot; this trial was reverted rather than promoted. A subsequent Vita test needs a confirmed awake, unlocked console.

## Bounded structural review

A PC-only state probe in Past 8:24 counted about **1,463 `SetRenderState` calls per frame**, only **103** of which changed a value. `SetTextureStageState` had about **2,952 calls**, **2,010** changing a value. The existing preparation layer already suppresses unchanged records. A targeted shortcut around ten render-state call sites in `F_004287f0` preserved two PC captures but showed no PC speed improvement (the sampled game-body windows were about 2.5 ms versus 1.9 ms in the earlier reference). Host frame pacing differed between runs, so this is not a Vita performance comparison. The shortcut and probe were removed, and both PC and Vita builds were regenerated from the retained source.

The first two native vertex passes already removed the clearest repeated arithmetic. A final UV-output cache had no exact hits because its transformation parameters changed each call; an exact bounds cache did not save measurable Vita time because validation still scanned every vertex. The remaining model path submits real state changes and draw calls. Further blanket state skipping, draw merging, or caching of animated results would risk missing geometry or incorrect effects. This investigation stops here rather than stacking more unverified micro-optimizations. The **30 Hz logical target remains open**; the experimental branch retains only the previously measured native bounds and UV changes.

## Essais supplémentaires du 27 octobre : v91 à v93

Les essais ci-dessous utilisent les passages natifs de palette, sommets et collisions, l'UV corrigée v89 et l'interpolation désactivée. Les variantes sont successives, avec les trois mêmes pièces et six captures Vita comparées. Aucune texture ni précision n'est réduite.

| Essai | Water référence/candidat | Past référence/candidat | Subway référence/candidat | Décision |
|---|---:|---:|---:|---|
| Bornes NEON par ordre entier IEEE (v91) | 39,370 / 39,433 ms | 39,598 / 39,468 ms | 33,959 / 33,939 ms | Aucun gain utile ; retiré |
| Annulation exacte des échelles UV égales à 1/4 (v92) | 39,159 / 39,160 ms | 39,470 / 39,200 ms | 33,845 / 33,807 ms | Petit gain, essai optionnel, défaut OFF |
| Synchronisation limitée des registres des fonctions feuilles (v93) | 39,370 / 39,258 ms | 39,489 / 39,741 ms | 34,009 / 33,976 ms | Aucun gain utile ; retiré |

v91 a été vérifié par 20 000 comparaisons du code ARM réellement compilé avec le bloc x86 original, y compris subnormaux, NaN, zéros signés et entrées non alignées. Son ordre entier évitait le traitement des subnormaux des comparaisons flottantes NEON. Les six images Vita sont identiques, mais le gain ne justifie pas de garder le code.

v92 garde le calcul en double et annule seulement multiplication et division par le même 1/4 exact, lorsque la différence est finie. Toutes les autres valeurs gardent leur calcul. 40 000 comparaisons UV et 20 000 comparaisons des bornes contre le x86 original passent, avec des coordonnées finies couvrant tous les exposants pour cet essai. Les six images PC et six images Vita comparées sont identiques. Le gain mesuré dans Past est de 0,27 ms, sans conclusion de gain général. `SH4_NATIVE_UV_IDENTITY=1` reste une option expérimentale désactivée par défaut.

v93 analysait le C généré pour synchroniser seulement les registres utilisés par des fonctions feuilles sans appel externe inconnu. Il modifiait 6 467 sites et retirait 32 274 paires de synchronisation ; 20 000 comparaisons de CPU entier/pile/mémoire sur des séquences compilées passaient. La taille de code ARM diminuait de 118 652 octets. Malgré six images Vita identiques, les mesures n'établissent aucun gain ; le générateur conservé a été régénéré sans cette transformation. Les prototypes des deux essais rejetés restent seulement dans leurs preuves ignorées.

Les sauvegardes, binaires et environnements normaux ont été restaurés et relus après chaque série. Ces essais ne démontrent pas 30 fps constants. La prochaine mesure doit séparer le temps CPU réellement exécuté du temps mural passé dans les attentes du pipeline, avant une nouvelle réécriture importante. Le [banc de cinématiques](NATIVE-CINEMATICS.md) complète maintenant les parcours de pièces.

## Temps réellement exécuté par les threads (v94)

Le diagnostic optionnel `SH4_CPU_TIME=1` utilise les compteurs d'exécution du noyau via `sceKernelGetThreadInfo`, sans plugin noyau. Un intervalle actif de 20 ms au démarrage étalonne l'unité observée du compteur : 20 122 unités pour 20 001 microsecondes sur cet essai. Les millisecondes CPU sont donc indiquées comme **estimations**. La fréquence ARM observée est de 500 MHz dans cet environnement de console ; elle n'est pas modifiée par ce diagnostic. Sans l'option, aucun relevé de compteur ni étalonnage n'est effectué.

| Pièce | Temps mural/image | Thread jeu | Préparation | Rendu | Audio |
|---|---:|---:|---:|---:|---:|
| Water 4:11 | 39,035 ms | 37,779 ms CPU | 7,925 ms CPU | 5,725 ms CPU | 2,921 ms CPU |
| Past 8:24 | 39,357 ms | 37,845 ms CPU | 19,294 ms CPU | 13,317 ms CPU | 1,193 ms CPU |
| Subway 2:14 | 33,776 ms | 32,138 ms CPU | 4,724 ms CPU | 4,396 ms CPU | 5,815 ms CPU |

Ces temps sont concurrents et **ne s'additionnent pas** pour donner le temps d'une image. Le jeu reste fixé au cœur 0, le rendu au cœur 1 et la préparation au cœur 2 ; audio et préchargement peuvent migrer. Les périodes de capture et les fenêtres de chargement sont exclues. Le thread de journal consomme environ 0,05 à 0,08 ms par image dans ces fenêtres, et les threads de préchargement sont inactifs après stabilisation.

Le fil principal consomme presque toute la durée murale des images lourdes : ce relevé confirme un coût de calcul sur ce fil, au-delà d'une simple attente GPU ou d'un manque d'utilisation des trois cœurs. La préparation est aussi significative dans Past. Il faut maintenant identifier les chaînes de calcul dominantes du fil principal, puis réduire leur travail ou transférer des travaux indépendants avec un contrat de données explicite.

Les six captures Vita sont identiques à la référence v92 sans ce diagnostic ; la dernière régression PC donne 23 images identiques, avec le chemin UV quarter activé et le mode cinématique désactivé. Preuves : `build/room-audit/cpu-scheduler-v94/`. La configuration normale, la sauvegarde et le binaire ont été restaurés et relus après le relevé. Les coûts du profilage détaillé suivant ne seront pas utilisés comme mesures de cadence normale.


## Réutilisation des UV et copies de sommets, v96 à v104

Le [rapport du module natif](NATIVE-MODEL.md#propriété-des-uv-par-ressource-v96v97-expérimental) décrit le contrat de propriété par tampon, l'invalidation des écritures et le noyau indépendant du CPU invité. Un cache de résultats préparés seul n'accélérait presque pas le jeu : il ajoutait un passage mémoire. Le diagnostic de phases a séparé le coût des bornes et de l'application UV ; le préchargement explicite puis la copie en bloc des canaux courants ont levé une partie de ce coût, sans réduire la précision, les textures ou les fonctionnalités.

| Past 8:24, vue fixe, interpolation coupée | Temps mural moyen |
|---|---:|
| Référence sans réutilisation, v100 | 39,705 ms |
| UV préparées + préchargement, v100 | 35,827 ms |
| Répétition du préchargement, v101 nettoyé | 35,973 ms |
| UV préparées + copie en bloc, v101 | 33,706 ms |

Les comparaisons sont successives et leurs fenêtres excluent les captures. Les images restent identiques. Le candidat n'est pas à 30 fps constants dans toutes les pièces : la prison est encore à environ 41,5 ms dans la comparaison v102, le métro autour de 33,8 ms. Le scénario v102 écrivait des touches clavier et ne prouvait pas le déplacement par sticks : ses six images sont des preuves de rendu appariées, pas une qualification suffisante d'un parcours à la manette.

Le banc v104 injecte aussi les sticks pendant sa seule fenêtre explicite d'audit. Une paire dans Past conserve exactement quatre poses relevées et deux images, avec un déplacement réel de plusieurs mètres. Ses vues sont moins coûteuses que la vue fixe : référence 33,973 ms et candidat 33,300 ms. Ce n'est pas la même charge que le premier tableau ; les économies ne s'additionnent pas.

La qualification comprend 60 000 comparaisons UV au x86 GOG original, 20 000 comparaisons des bornes, 250 002 comparaisons du pont de cache, les 23 images PC finales, six captures PC avec déplacement, six captures Vita appariées v102 et les deux captures avec sticks v104. Les cinématiques 1 et 8 se terminent sur PC et Vita avec 16 images identiques à leurs références sur chaque plateforme ; elles ont été examinées. Cela ne couvre ni tous les états narratifs, ni toutes les cinématiques, ni une partie complète.

L'interpolation du candidat v102 mesure 42,372 / 38,389 / 35,973 ms dans prison / Past / métro, avec 423/429, 0/367 et 333/337 images intermédiaires par images logiques des fenêtres conservées. Past dépasse toujours les 1 536 Kio de rejeu. Un essai de regroupement des attributs v98 ne réduisait ce volume que de quelques Kio et n'améliorait pas la cadence : il a été retiré. Aucun constat de 60 images affichées constantes.

Ces changements restent optionnels sur la branche expérimentale. Le binaire, la sauvegarde et la configuration d'origine ont été restaurés et relus après toutes ces séries ; le ZIP des testeurs est conservé. La prochaine analyse cible la prison, où le gain UV est insuffisant, avec un profil échantillonné distinct qui ne sert pas de mesure de cadence normale.

## Chaîne native de segments (v105 à v107)

Le [module de segments](NATIVE-SEGMENTS.md) remplace un service complet de filtrage de listes et conserve les callbacks de contacts originaux. Il ne réduit ni précision, géométrie, textures ou fonctionnalités. Les entrées non prises en charge reprennent l'original sans appliquer de résultats partiels. Les tests incluent le x86 GOG, le pont de production, le code ARM compilé et des comparaisons de requêtes réelles.

La première paire stationnaire v105 donne prison 39,332 → 38,239 ms, Past 33,856 → 33,655 ms et métro 33,951 → 34,055 ms. Une seconde paire v106 retire les recherches répétées d'identités immuables, tout en conservant les vérifications des pointeurs et constantes modifiables : prison 38,753 → 37,322 ms, deux images identiques.

La paire v107 avec déplacement réel donne prison **41,139 → 37,946 ms**, Past **33,380 → 33,397 ms**, métro **34,710 → 33,512 ms**. Douze poses relevées, six images et les captures des cinématiques 1/8 restent identiques. Les gains concernent ces charges ; ils ne se cumulent pas entre séries et ne suffisent pas à déclarer les 30 fps constants. L'interpolation reste limitée, notamment par le dépassement du tampon de rejeu dans Past.

Un autre appelant de sphère/polygones réutilise maintenant le service géométrique natif : [v108](NATIVE-COLLISION.md#liste-de-polygones-directe-v108-expérimentale). Avec déplacement, la prison passe de 37,522 à 36,301 ms ; Past donne 33,300 / 33,753 ms et le métro 33,349 / 33,356 ms. Les six images et douze poses correspondent, les 23 images PC et 16 images de cinématiques de chaque plateforme aussi. Les options restent désactivées par défaut.

### Tampons d'interpolation plus grands, option existante

v108 compare séparément les options existantes `SH4_QFRAME_KB=2048` et `SH4_QRING_KB=2048` aux capacités habituelles de 1 536 / 1 024 Kio. Les six captures réelles sont identiques. Hors captures :

| Pièce | Tampons habituels | Tampons 2 Mio | Images intermédiaires / logiques, habituel puis 2 Mio |
|---|---:|---:|---|
| Prison | 38,312 ms | 38,412 ms | 365/369 puis 365/369 |
| Past | 38,201 ms | 38,931 ms | 0/424 puis 0/415 |
| Métro | 33,828 ms | 33,850 ms | 292/358 puis 411/417 |

Dans Past, les enregistrements atteignent environ 2 040 Kio et le dépassement disparaît avec 2 Mio. L'adaptation reste néanmoins désactivée dans les fenêtres stables : augmenter la capacité ne suffit pas à faire tenir les deux passes. Les écarts de compteurs dans le métro ne constituent pas des horodatages d'affichage physique. Aucun réglage par défaut n'est changé à la suite de cette comparaison.

### Pauses de streaming observées

Le parcours avec sticks relit les mêmes blocs de `demo/bgm.sh4` (542 Kio) et `demo/cm.sh4` (320 Kio), avec des lectures synchrones pouvant prendre environ 50 à 90 ms. Des pauses d'image de 80 à 110 ms apparaissent avec des compteurs de compilation de shaders nuls. D'autres lectures plus petites restent à localiser. Le préchargement conserve un seul bloc par handle, mais redémarrait même lorsque sa prochaine prédiction désignait exactement ce bloc déjà disponible.

L'[essai v110](STREAMING-EXPERIMENTS.md) conserve ce job/buffer pour les archives immuables, sans nouveau tampon. Sur 1 500 images avec sticks, les images ≥ 50 ms hors captures passent de **14 à 8**, les poses et deux captures restent identiques et la régression PC donne 23 images identiques. La moyenne 33,470 → 33,304 ms n'est pas une hausse de cadence générale. Les lectures à froid et les pointes périodiques sans I/O mesurée subsistent ; l'option reste expérimentale et désactivée par défaut.

## Profilage de grands traitements (v109)

Les options de profilage et de trace remplaçaient la propriété CMake des options du code généré, retirant notamment `-Os`. Elles sont maintenant ajoutées à cette propriété. Le build normal, options de diagnostic coupées, produit le même eboot que celui archivé avant cette correction : SHA-256 `3ce627ee1fbba1c406180edc06685d6a643c59be6e32c5e6e731b63fbc2b52b2` sur ce contrôle.

Un build de profilage isolé utilise un en-tête local ignoré qui désactive l'instrumentation de 9 465 fonctions et garde 31 entrées de grands traitements. Le désassemblage confirme les hooks sur une entrée sélectionnée et leur absence sur une routine mathématique exclue. Les descendants non instrumentés sont attribués à leur plus proche ancêtre sélectionné : les résultats ne sont pas des coûts exclusifs de chaque fonction originale.

Dans la prison, sur la fenêtre logique 4 050–4 650, le profil intercepte environ **3 541 appels par image**, contre environ 68 326 dans l'ancien profil détaillé. Le temps mural passe néanmoins de **35,463 ms** dans le binaire normal à **37,578 ms** dans le binaire de profilage. Cette perturbation d'environ 2,1 ms interdit encore de convertir directement les pourcentages en économies promises. Les deux captures sont identiques ; le binaire, la sauvegarde et l'environnement normaux sont restaurés après la paire.

Le contexte de rendu `0x55c650` représente environ 46,8 % des échantillons inclusifs, contre 21 % pour le contexte de simulation `0x553850`. Le rendu de modèles `0x4287f0` en représente 19,6 % dans l'arbre, avec ses descendants instrumentés ; le niveau `0x4272e0` représente 28,1 %. Les services de collision natifs consomment encore du temps, et `0x41dc50` reste recompilé, mais une succession de petits raccourcis de collision ne remplace pas la reconstruction du traitement de modèles. La prochaine réécriture importante doit traiter les structures et les échanges de ce module, en conservant les modes de matériau, les poses et les effets du GOG.

Les binaires, symboles, sélection, désassemblages et fenêtres sont conservés dans `build/room-audit/coarse-profile-v109/`. Ce dispositif n'est pas activé dans le VPK normal.

### Reprise du profil de modèles — 28 octobre

La référence distribuée du 28 octobre mesure 35,829 ms dans la fenêtre 4 050–4 650 de la prison 4:11. Un binaire isolé sélectionnant 50 fonctions (dont 19 descendants supplémentaires de l'orchestrateur) mesure 38,016 ms : son surcoût reste d'environ 2,2 ms. Les deux captures sont identiques. L'instrumentation des fonctions sélectionnées et son absence dans une fonction exclue ont été vérifiées dans le désassemblage ARM avant lancement.

Sur 19 282 échantillons et 600 images, l'arbre conserve 29,1 % dans `0x4272e0`, dont 22,0 % passent par `0x4297c0` et 20,4 % par `0x4287f0`. Les autres descendants exposés restent petits (`0x427e60` 2,4 %, `0x427f50` 1,6 % inclusifs). Le coût propre des changements d'état de texture apparaît à 4,3 % des échantillons : cela motive un essai isolé de l'encodage des états de quatre octets, sans remplacer à l'aveugle l'orchestrateur. Ces pourcentages ne sont pas des gains promis.

La première préparation de la sélection utilisait des noms sans les zéros initiaux des symboles générés. Elle a été interrompue avant mesure, la sauvegarde a été restaurée, et le profil ci-dessus utilise la sélection corrigée et vérifiée. Preuves sous `build/room-audit/model-profile-20261028/`. Le binaire de référence, la sauvegarde et la configuration normale ont été restaurés après la paire.

### Encodage des états de quatre octets — essai du 28 octobre

`SH4_PREP_SCALAR=1`, désactivé par défaut, spécialise `prep_set` pour les valeurs de quatre octets. La comparaison, l'écriture et la copie vers le record utilisent une taille constante ; l'ordre et les octets du record restent ceux du chemin générique. Le test différentiel compare 50 000 encodages, valeurs inchangées, offsets non alignés et autres tailles inclus. Le désassemblage ARM confirme l'absence d'appels de comparaison/copie génériques dans cette branche. La régression PC donne 23 captures identiques.

| Prison 4:11, interpolation coupée | Générique | Scalaire |
|---|---:|---:|
| Première paire, ordre générique puis scalaire | 35,752 ms | 35,029 ms |
| Répétition, ordre scalaire puis générique | 35,795 ms | 35,018 ms |

Chaque comparaison utilise le même binaire, la même fenêtre logique 4 050–4 650 et exclut les captures ; ses deux images sont identiques. Le gain observé de 0,72 à 0,78 ms est reproductible sur ces portions, mais reste insuffisant pour tenir 33,3 ms dans cette vue. Les autres scènes et les parcours mobiles doivent être qualifiés avant activation dans un paquet de test. Preuves : `build/room-audit/prep-scalar[-reverse]-20261028/`. La sauvegarde et le binaire de référence ont été restaurés entre les séries.

La paire fixe Past 8:24 mesure 33,585 ms en générique contre 33,366 ms en scalaire, avec deux captures identiques. Le gain supplémentaire sur cette scène est faible et proche du plafond de cadence ; il n'établit pas 30 fps constants. Le candidat reste opt-in, sans modification du paquet des testeurs. Preuves : `build/room-audit/prep-scalar-past-20261028/`.

## Images trop grandes pour l'interpolation (v128/v129)

Dans Past 8:24, une image occupe plus que les 1 536 Kio du tampon de rejeu. Le chemin initial copie et examine une grande partie des commandes avant de constater le dépassement, puis exécute directement le reste. Il ne produit alors aucune image intermédiaire. L'option expérimentale `SH4_INTERP_SKIP_LARGE=1` compte la taille de l'image et envoie directement la suivante si la précédente dépassait la capacité. Dès qu'une image redevient petite, le tampon et l'analyse reprennent. Les commandes GL et leur ordre ne changent pas.

Les paires utilisent **le même binaire Vita**, l'interpolation active et les autres options natives qualifiées identiques. Les deux angles de chacune des trois pièces fixes sont identiques octet pour octet ; dans les deux parcours avec sticks et caméra, les quatre positions/rotations relevées ainsi que les deux captures de chaque pièce correspondent exactement.

| Charge | Option coupée | Option active | Images intermédiaires |
|---|---:|---:|---|
| Water 4:11, fixe | 38,324 ms | 38,327 ms | 89 % / 99 % des fenêtres retenues |
| Past 8:24, fixe | 38,094 ms | **34,414 ms** | 0 % / 0 % |
| Subway 2:14, fixe après Past | 33,838 ms | 33,628 ms | 99 % / 82 % ; cette baisse transitoire a motivé le contrôle isolé |
| Past 8:24, déplacement | 33,833 ms | 33,573 ms | 5 % / 46 % ; première fenêtre lourde 37,2 → 34,9 ms |
| Subway 2:14, déplacement isolé et ordre inverse | 35,050 ms | 35,040 ms | 99 % / 99 % ; zéro image envoyée directement par l'option |

Les fenêtres n'ont pas toutes le même nombre d'images, notamment après la dernière capture : les moyennes sont des mesures de ces portions, pas une promesse de gain constant. Le contrôle isolé du métro n'a pas reproduit la baisse d'interpolation du tour fixe ; le mécanisme n'y était pas activé. Le gain net se concentre sur les passages dont les images dépassent réellement le tampon. Même dans la vue fixe de Past, **34,4 ms reste au-dessus de la cible de 33,3 ms** ; aucune garantie de 30 images logiques ou 60 images affichées constantes.

Les cinématiques communes 1 et 8 se terminent aussi avec et sans l'option, aux mêmes images logiques 6 798 et 7 109. Leurs **18 captures Vita sont identiques octet pour octet** entre les deux passes et à la référence v90 déjà examinée. Le journal ne contient pas d'erreur fatale ; une image seulement est envoyée directement après une trame volumineuse dans cette séquence. La régression PC termine avec 23 images identiques sur 23.

Cette option reste désactivée par défaut sur la branche expérimentale. Les scènes testées n'épuisent ni les transitions ni les états narratifs ; ces résultats ne justifient pas encore une activation dans le VPK distribué. Les preuves et binaires appariés sont dans `build/room-audit/interp-skip-large-v128/`, `interp-skip-large-motion-v129/` et `interp-skip-large-cine-v130/`. La sauvegarde, le binaire et la configuration normaux de la Vita ont été restaurés et relus après chaque série.
