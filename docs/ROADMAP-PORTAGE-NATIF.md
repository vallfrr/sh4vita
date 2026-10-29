# Feuille de route du portage natif de Silent Hill 4 sur PS Vita

**Plan historique daté.** Pour la branche réellement ouverte, les travaux déjà retenus ou retirés et la prochaine action courante, lire [HANDOFF.md](HANDOFF.md) avant de suivre cette feuille de route.

État au 26 octobre 2026 — plan de travail, **pas** engagement de cadence ni liste de correctifs déjà réalisés. La branche d'expérimentation est `codex/60fps-engine`. Les candidats qui y sont présents ne sont pas automatiquement des versions à remettre aux testeurs.

## But et contraintes

La cible finale demandée est une **base de jeu native adaptée à la Vita**, reconstruite à partir du comportement et des ressources du jeu original. Les remplacements de boucles déjà réalisés sont des étapes de migration ; ils ne constituent ni cette reconstruction complète ni une mesure de son gain potentiel. Il faut remplacer progressivement les systèmes entiers, leurs structures et leurs échanges, puis retirer les dépendances devenues inutiles à l'état CPU et à la mémoire x86 simulés. L'[architecture de migration](ARCHITECTURE-NATIVE.md) distingue cette cible des essais de performance intermédiaires.

L'entrée de référence reste **la version PC GOG v5 de Silent Hill 4**, identifiée par le SHA-256 de `SILENT HILL 4.exe` : `cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0`. Les modèles, textures, sons, vidéos, scripts, animations et règles du jeu proviennent toujours des fichiers originaux préparés par `sh4prep`. Un éventuel index ou format accéléré est **dérivé localement de ces fichiers**, versionné et régénérable ; il ne remplace pas la preuve de possession du jeu et n'entre ni dans Git ni dans le VPK.

L'exécutable GOG contient du code machine, pas les sources C du studio : « réécrire » signifie retrouver son comportement à partir de ce binaire, puis substituer une implémentation native démontrée équivalente. Les métadonnées prétraitées viennent des ressources appartenant au joueur. Le cache GPU du VPK est un artefact technique compilé à partir des shaders utilisés par cette version du jeu ; il suit le port et son ABI, pas le dossier de données préparées du joueur.

La cible prioritaire est **30 mises à jour logiques par seconde en jeu réel** (budget 33,33 ms), y compris dans les pièces difficiles et avec des ennemis. La cible secondaire est **60 images affichées par seconde** par interpolation lorsque deux rendus tiennent dans le budget matériel. Le moteur avance par itération ; le faire tourner à 60 itérations accélérerait le jeu. Une moyenne de 30 fps dans une pièce immobile ne suffit donc pas : il faut mesurer la distribution des temps d'image et les pauses pendant les déplacements, combats, transitions, morts et sauvegardes.

Contraintes permanentes : rendu et commandes fidèles, collisions et progression inchangées, compatibilité des sauvegardes, mise à jour possible du seul VPK, repli sûr si une donnée préparée ou un cache manque, aucune donnée GOG dans le dépôt ou le VPK, et aucun correctif déclaré « vérifié » sur la base d'un seul test PC ou d'une vue statique. Les changements de la branche expérimentale passent des portes de comparaison avant d'être promus.

## Ce que les mesures disent déjà

| Constat | Preuve actuelle | Conséquence pour le plan |
|---|---|---|
| La logique est sous 30 Hz dans plusieurs pièces difficiles. | Sur le parcours Vita stationnaire de la branche expérimentale, interpolation coupée : prison aquatique 4:11 ~41,6 ms, Past 8:24 ~43,2 ms après les deux boucles natives, métro 2:14 ~35,5 ms. Le parcours avec mouvement de Past mesure ~40,3 ms. Les mesures ne sont pas interchangeables : leur position, entrée et version doivent être conservées. | Priorité au fil de jeu et au coût par image, puis aux parcours normaux ; ne pas attribuer un gain aux effets GPU sans profil correspondant. |
| `F_004287f0` domine Past 8:24. | Environ un tiers des échantillons du fil de jeu dans le profil de ce cas ; le profil ralentit le programme et donne un rang, pas un temps absolu. | Étudier cette fonction de bout en bout avant de multiplier les micro-optimisations. |
| Deux passages sur les sommets ont déjà été remplacés. | La passe de bornes et la passe UV dans `recomp/x86rec.py` / `recomp/model_uv_native.inc` réduisent Past immobile d'environ 52,8 à 43,2 ms dans la série retenue. | Garder ces gains expérimentaux, mais traiter le reste du chemin et vérifier sur davantage d'états. |
| Les copies et appels d'état ne sont pas une solution suffisante seuls. | Copie MMX native : ~1,3 ms dans Past ; cache exact de bornes : ~0,1 ms ; cache UV final : aucun hit exact ; raccourci sur dix sites `SetRenderState` : pas de gain PC fiable. | Ne pas recommencer ces essais sous un autre nom sans nouveau profil ou changement de structure. |
| La GPU et les soumissions comptent dans certaines scènes. | Hôpital : ~390 dessins, ~8 ms de soumission CPU et 12–14 ms GPU dans une mesure antérieure. Past : ~531 dessins, fil GL ~12 ms, préparation ~20 ms, jeu ~43 ms dans la série profilée. | Optimiser chaque fil selon la scène. Un gain dans le fil de jeu peut être masqué si le fil graphique devient le nouveau plafond. |
| Les pauses de première utilisation et les défauts intermittents restent distincts des fps. | Cache GPU compilé livré dans le VPK, mais liaison/chargement résiduels ; deux testeurs ont vu une corruption Henry/menu non encore reproduite exactement sur la console locale. | La fiabilité du rendu et du cache est une porte de publication, pas un motif de supprimer le cache sans preuve causale. |

Sources locales : [état du projet](STATUS.md), [essais de performance](PERF-60-EXPERIMENTS.md), [validation Vita](VALIDATION-2026-10-25.md), [incidents testeurs](TESTERS-2026-10-26.md), [données préparées](PREPARED-DATA.md).

Pour donner l'ordre de grandeur : ramener une image logique de 43,2 à 33,3 ms demande d'économiser **environ 9,9 ms sur son chemin critique**. Une optimisation de 1 ou 2 ms peut être valide, mais ne termine pas ce travail. Le coût d'une fonction dans un profil échantillonné n'est pas un gain que l'on peut encaisser intégralement, et des fils qui se chevauchent ne s'additionnent pas simplement.

## Méthode commune à chaque remplacement

1. **Fixer la référence.** Noter le commit, le SHA du jeu GOG, le VPK, la bibliothèque vitaGL isolée, les options, la sauvegarde et son empreinte, les réglages d'interpolation et de diagnostic. Conserver une référence PC et Vita sur le même état de jeu. Les captures de saut direct dans une pièce ne prouvent pas les événements d'une partie normale.
2. **Prouver l'origine du comportement.** Partir de l'exécutable et des ressources GOG v5, des adresses dans [GAME.md](GAME.md), du C généré et des appels observés. Décrire entrées, sorties, registres, pile x87, drapeaux, mémoire invitée lue/écrite, ordre des appels, allocations et effets de bord. Identifier les branches réellement atteintes avant d'en supprimer une.
3. **Mesurer avant de coder.** Profiler le fil de jeu, le fil de préparation, le fil GL, la GPU et l'I/O séparément. Exprimer le coût par image logique, pas seulement les fps affichés. Relever médiane, p95, p99 et plus longue pause, ainsi que le nombre de dessins, d'états, d'allocations, les octets copiés et la mémoire disponible.
4. **Ajouter une voie native réversible.** Utiliser un `WRAP`/`OVERRIDE` dans `recomp/x86rec.py` ou un remplacement ciblé dans `port/src/`; garder l'ancienne voie générée derrière un interrupteur de test jusqu'à équivalence. Ne pas éditer `port/gen/` directement. Un `WRAP` doit rester précis : ni saut de rendu supposé invisible, ni raccourci d'animation non prouvé.
5. **Comparer à l'original et au candidat précédent.** Tests différentiels sur registres, FPU, mémoire et appels observables ; scénarios adverses (zéro, limites, NaN/signed zero si pertinents, objets animés, sortie de pièce, mort/rechargement) ; régression PC de 23 captures ; paires de captures Vita à mêmes coordonnées/état ; parcours humain ou scripté normal quand l'effet dépend de la progression.
6. **Mesurer le gain net sur Vita.** Même route et même configuration, plusieurs répétitions, cache froid et chaud si nécessaire ; garder le changement seulement si le gain dépasse la variabilité et n'aggrave ni p95/p99, ni pauses, ni mémoire, ni rendu. Si la charge se déplace vers un autre fil, le mesurer explicitement.
7. **Documenter la décision.** Version, protocole, captures, journaux, chiffres et limites. Rejeter ou revenir en arrière dès qu'une régression apparaît. Les données de jeu, `port/gen/`, `re/out/`, les logs et les sauvegardes de test restent hors Git.

### Tableau de bord minimal

Pour chaque scène, enregistrer `logique` (ms), `préparation` (ms), `GL CPU` (ms), `GPU` (ms si disponible), temps d'affichage physique, p50/p95/p99/max, pauses de transition, RAM invitée/hôte libre et pic, dessins et changements d'état, hits/compilations/liaisons de programmes GPU, erreurs de texture et de mémoire. Le tableau doit distinguer **première visite**, **revisite**, **immobile**, **déplacement**, **combat**, **cinématique** et **chargement**. Les compteurs de développement restent désactivés dans la version joueur et leur coût est mesuré séparément.

Scènes sentinelles : Past 8:24 (géométrie), prison aquatique 4:11 (collision/ennemis), métro 2:14 (mixte), hôpital 7:32 (soumission/GPU), forêt 3:13 (streaming), bâtiment 5:15 (première liaison GPU), appartement, escalator du métro et cycle mort/rechargement (rapports des testeurs), plus `m_08` pour la vidéo. Ajouter des variantes avec ennemis, caméra mobile et états narratifs réels ; ne pas confondre leurs résultats avec les 386 chargements statiques déjà audités.

## Programme de réécriture, dans l'ordre des dépendances

### A. Cartographie reproductible du jeu d'origine

**A1 — Inventaire des fonctions et de leurs coûts.** À partir des tables de `recomp/`, des ~9 462 fonctions recompilées et des traces de la version PC GOG, produire un index `adresse → rôle supposé → appels → ressources → scènes atteintes → temps mesuré`. Les 116 thunks et les fonctions x87 dynamiques méritent une entrée explicite lorsqu'elles sont chaudes. Ne donner un nom de fonction qu'après vérification de ses entrées/sorties. Résultat : classement par coût exclusif et inclusif, occurrence par pièce, risque de réécriture.

**A2 — Banc différentiel original/port.** Faire tourner un corpus d'appels ciblés du binaire GOG et de la recompilation avec les mêmes données de mémoire, puis comparer les changements d'état. Étendre `recomp/difftest.py` et `difftest_x87.py` seulement aux fonctions candidates, avec traces minimales qui ne polluent pas les mesures Vita. Le modèle de référence doit permettre de distinguer un défaut hérité du jeu d'une régression du port.

**A3 — Parcours narratifs de référence.** Construire quelques sauvegardes/points de contrôle couvrant une progression normale, des ennemis, objets, menus, cinématiques, mort, retour à l'appartement et sauvegarde. Conserver leurs empreintes dans les preuves locales, jamais dans Git. Les 228 pièces/386 cas et onze tunnels fournissent la couverture de ressources ; ces parcours ajoutent la couverture d'états.

**Sortie A :** chaque fonction choisie pour une réécriture a un contrat et au moins un scénario où son coût a été mesuré. Sans cela, elle reste une hypothèse.

### B. Géométrie et modèles : cible principale du fil de jeu

**B1 — Décomposer `F_004287f0`.** Cartographier les boucles de lecture des modèles, les transformations, bornes, UV, préparation des constantes, états D3D et appels de dessin. Distinguer géométrie fixe, animée, déformée et générée par effet. Mesurer le coût de chaque segment dans Past, hôpital et une scène avec Henry/ennemis ; le pourcentage global de la fonction ne suffit pas à décider lequel réécrire.

**B2 — Remplacer les autres boucles de sommets chaudes.** Après les passes bornes/UV déjà expérimentales, isoler les transformations de coordonnées, normales, couleurs et indices effectivement chaudes. Décoder une fois les formats originaux lorsqu'ils sont immuables ; écrire directement dans les tampons du port lorsque la durée de vie le permet. Éviter les copies temporaires sans modifier l'ordre des sommets, le winding, les UV ou les arrondis visibles. Pour les modèles animés, prouver l'instant exact où l'entrée change avant tout cache.

**B3 — Fonctions de modèle natives entières.** Quand les fragments sûrs ont été validés, remplacer une fonction complète par une implémentation C/ARM claire, avec un contrat de mémoire invitée et des tests différentiels. Garder les chemins rares de la version GOG derrière un repli original tant qu'ils ne sont pas qualifiés. Le but est d'enlever la surcharge des dizaines de milliers d'opérations `R32/W32`, d'état CPU invité et de transitions d'appels, pas d'altérer les données du jeu.

**B4 — Données de modèles préparées localement.** Étudier un index dérivé par `sh4prep` : emplacements, formats, bornes et sous-maillages extraits des ressources GOG, avec SHA des sources, version de schéma et contrôle à la lecture. Ne précalculer que des valeurs indépendantes du temps, de la pose et de la caméra. Conserver les fichiers d'origine dans `ux0:data/sh4` et un repli qui les lit normalement. Si l'index change dans une mise à jour du VPK, le nouveau code doit lire l'ancien schéma ou régénérer l'index sans toucher aux sauvegardes ; le joueur ne doit pas être forcé de recopier 3,6 Go pour une optimisation.

**B5 — Critère de passage.** Comparaison PC de modèles immobiles et animés, variantes d'éclairage/fog/transparence, captures Vita de Henry, ennemis, cinématiques et menu ; gain net du fil de jeu sur plusieurs pièces, sans hausse de RAM qui compromet les 4–12 Mo de marge utilisateur observés après initialisation.

### C. Collision, visibilité et logique spatiale

**C1 — Finir l'inventaire du profil collision.** `0x573a20` (intersection rayon/triangle) a déjà un `WRAP` natif. Mesurer maintenant le filtrage des polygones (~6,3 % des échantillons du profil eau initial), la traversée BSP, les tests objet/ennemi et le nombre de requêtes par image. Identifier si le coût vient de l'arithmétique, de la structure des données ou d'appels redondants.

**C2 — Nativiser sans changer les résultats.** Réécrire les boucles de parcours et le filtrage par étapes ; préserver l'ordre des candidats, les seuils flottants, les normales et l'identité du premier impact. Un index spatial dérivé des collisions GOG peut éviter de relire/décoder certaines structures, mais ne doit pas modifier les hitboxes ou l'accès à une porte/objet. L'ancienne voie sert de comparaison à grande échelle sur des rayons enregistrés.

**C3 — Couvrir les cas qui ont déjà cassé.** Ressource de collision nulle et racine BSP effacée, sortie/entrée de pièce, escaliers, portes, trous, ennemis mobiles, coups de mêlée et projectiles. Une capture identique ne valide pas les collisions : comparer contacts et trajectoires sur la référence PC.

### D. Émulation D3D8, commandes et moteur de rendu

**D1 — Supprimer les conversions répétées mesurées.** Dans `port/src/d3d8/{draw,resource,device,prep,qgl}.c`, profiler la traduction des états, la résolution des shaders, les constantes, les verrous de ressources et les copies de sommets. Transformer en structures natives permanentes les métadonnées stables d'un objet D3D, avec génération/invalidation explicite lorsque l'objet change ou se libère. Les précédents caches de bornes/UV/constantes sans gain interdisent un nouveau cache général sans preuve de coûts sauvés.

**D2 — Réduire le coût de préparation et de soumission.** Garder l'ordre de dessin, les barrières entre cibles, la profondeur/stencil partagés et les effets alpha. Étudier la taille/coalescence des enregistrements SPSC de `prep.c`, le placement des conversions entre les fils, les allocations par dessin et les appels vitaGL. Des appels `SetRenderState` répétés ne sont pas forcément coûteux : la couche de préparation élimine déjà des enregistrements inchangés. Mesurer les appels réellement soumis.

**D3 — Valider le chemin des textures.** Formats DXT décodés en 16 bits, mipmaps, transparence, rendus vers texture, libération/réutilisation des objets et menus. Les photos des testeurs montrent des pixels corrompus sur Henry et du texte ; leur cause exacte n'est pas prouvée. Un audit de durée de vie des textures, programmes et uniformes, avec repro sur une deuxième Vita, précède toute simplification de ce code.

**D4 — Évaluer un backend `sceGxm` direct seulement si le fil GL/GPU est le plafond.** L'hôpital est un candidat ; Past ne l'est pas encore. Un tel backend peut contourner une partie du coût de soumission vitaGL, mais implique une réécriture importante des états D3D8, textures, profondeur, post-traitement et synchronisation. Le prototype doit rendre une scène représentative pixel par pixel avant extension ; si les effets deviennent inexacts ou le budget mémoire explose, abandonner. Aucune baisse de résolution, suppression d'ennemis, de transparence, de brouillard ou de post-effet ne compte comme gain du portage natif demandé ici.

### E. Shaders et programme GPU

**E1 — Qualifier le cache existant sur plusieurs consoles.** Le VPK fournit des programmes GPU compilés et versionnés ; les entrées manquantes/invalides doivent retomber sur la compilation normale. Mesurer séparément lecture, validation, liaison, compilation et premier dessin, à froid et à chaud. Vérifier l'identité du programme par empreinte de source et la durée de vie des handles, y compris le cycle mort/rechargement signalé.

**E2 — Préparer uniquement ce qui est stable.** Les traductions de shaders et les programmes compilés peuvent être bâtis pendant le développement à partir de la version GOG supportée, puis livrés comme ressources techniques du VPK si compatibles avec l'ABI ciblée. Ne pas traiter un shader recompilé comme une texture originale ; ne jamais supposer qu'un cache produit sur une Vita couvre toutes les variantes d'éclairage, d'animation et de menu. Les données de jeu restent préparées sur le PC du joueur ; le cache GPU suit le VPK et peut changer à chaque version.

**E3 — Éliminer les pauses restantes.** Si une liaison de ~0,7–1 s reste perceptible à la première scène malgré le cache, essayer un chargement anticipé **borné** de programmes prédits par le prochain écran, dans le budget RAM et sans bloquer la logique. Un test doit prouver que le préchargement n'augmente pas les temps de chargement ou les erreurs de mémoire. La préparation globale de 2–3 minutes à chaque démarrage n'est pas un objectif.

### F. Streaming, fichiers, prétraitement et mémoire

**F1 — Tracer les longues pauses d'I/O.** Distinguer transfert FTP (hors jeu), lecture de ressources, décompression, parsing, création de textures et attente GPU. Le read-ahead et les lectures directes ont déjà supprimé d'anciens blocages de 55–230 ms ; chercher seulement les nouveaux appels synchrones prouvés par trace. Garder l'ordre attendu de chargement pour les scripts et effets.

**F2 — Exploiter `sh4prep` sans figer le jeu.** Déduire localement, à partir des fichiers GOG, des tables d'offsets, tailles, empreintes, formats et dépendances de pièce pour éviter un scan/parsing répétitif. Chaque donnée générée porte la version de son schéma et le SHA de sa source ; le runtime vérifie et régénère ou revient aux données brutes si elle manque. Les mises à jour du VPK ne doivent jamais effacer `save/sh4.sav` ou imposer une nouvelle préparation complète lorsque le GOG v5 de base n'a pas changé.

**F3 — Tenir un budget mémoire réel.** Mesurer le pic et la fragmentation des 200 Mo invités demandés au build, du tas hôte, des allocations vitaGL/CDRAM/phycont et des piles de fils. Recenser toutes les allocations par transition et après plusieurs heures ; borner les caches et recycler les tampons par scène. Ne pas déplacer un freeze CPU vers un échec d'allocation de texture ou un `sceKernelCreateThread` manqué. Rejouer les défauts de 5 504 octets et de read-ahead vus chez les testeurs.

### G. Animation, événements, son et vidéos

**G1 — Animation native si elle ressort du profil.** Inventorier skinning, interpolation de pose, cheveux/vêtements, caméra et effets de cinématique dans l'exécutable GOG. Une transformation native doit préserver la pose et l'instant de chaque événement. Les photos de Henry corrompu ne prouvent pas à elles seules un problème d'animation ; d'abord localiser si le défaut vient de l'entrée, du tampon de sommets, de la texture ou du shader.

**G2 — Scripts et jeu.** Dans la première phase de performance, conserver progression, inventaire, ennemis et options comme référence tant que les blocs lourds ne sont pas remplacés. Dans la reconstruction native complète, porter ensuite ces systèmes et leurs tables d'événements avec une correspondance explicite des états, du hasard, du temps et des sauvegardes ; leur faible coût ne les exclut pas de la cible finale. Vérifier sauvegarde/rechargement, langues, paramètres de caméra, trous, fenêtres et événements d'appartement. La caméra des fenêtres est reportée à la demande de l'utilisateur, mais reste un blocage de qualification fonctionnelle.

**G3 — Audio et films.** Le son est déjà mélangé sur le cœur 2 et le décodeur MPEG-1/NEON fonctionne ; `m_08` reste proche de 24 fps. Profiler décodage, copie YUV/RGB565, synchronisation audio/vidéo et contention avec la GPU avant de modifier ce chemin. Ne pas présenter 24 images vidéo par seconde comme un échec de la logique à 30 Hz. Vérifier les cinématiques en condition réelle, leur bande-son et la reprise du jeu.

### H. Interpolation et cadence affichée

**H1 — Garder la logique à 30 Hz.** Stabiliser d'abord le temps de l'image logique en mouvement et combat. Mesurer les dépassements du budget 33,33 ms et leurs causes ; régler les pauses isolées même si la moyenne est acceptable. Une cadence « constante » exige des marges, car les parcours et consoles varient.

**H2 — Rendre l'interpolation moins chère.** Profil des enregistrements/relectures dans `qgl.c`, capacité de la mémoire de replay, volume d'uniformes et coût de deux rendus. Past a déjà mis en évidence une limite du tampon à 1 536 Kio ; le porter naïvement à 2 048 Kio a aggravé le temps mesuré (~61 ms dans un essai). Étudier un encodage plus compact, des bornes par type de commande et la suppression prouvée des doublons de replay ; refuser toute omission qui change les effets ou la profondeur.

**H3 — Définir le succès correctement.** Vérifier 30 images logiques sans pauses sensibles, puis mesurer 60 présentations physiques stables et l'absence de tearing/artefacts dans les scènes où deux passes tiennent. L'overlay vert de fps peut refléter l'affichage, une vidéo ou une interpolation ; il n'est pas une preuve de 60 mises à jour du jeu. Si l'hôpital reste limité par la GPU, conserver le repli adaptatif plutôt qu'afficher des images irrégulières.

## Jalons et décisions

| Jalon | Livrable concret | Porte de sortie |
|---|---|---|
| M0 — Référence | Profil reproductible du dernier VPK, corpus original PC, routes Vita normales, états de sauvegarde et chiffres par fil. | On sait attribuer les p95/p99 et pauses de Past, eau, métro, hôpital ; aucune capture de diagnostic confondue avec une partie réelle. |
| M1 — Modèles | Contrat de `F_004287f0`, sous-passes natives qualifiées, éventuellement fonction entière avec repli. | Gain Vita reproductible sur au moins deux scènes, captures PC/Vita et animation identiques ou écarts expliqués. |
| M2 — Collision | Filtrage/BSP natifs uniquement si profil utile. | Même impact et même comportement d'ennemis/portes/trous sur corpus original ; baisse mesurée du temps logique. |
| M3 — D3D CPU | Ressources et commandes moins coûteuses ; éventuel prototype GXM isolé. | Pas de perte de textures/effets ; gain sur scène où ce fil est le plafond, sans régression des autres. |
| M4 — Pauses | I/O, liaison shader, allocations et mémoire instrumentées puis corrigées. | Première visite, revisite et longue session sans pauses nouvelles ni échec mémoire ; repli cache testé. |
| M5 — 30 Hz | Parcours normaux avec combats et cinématiques. | p95/p99 compatibles avec un jeu fluide dans les pièces difficiles ; toute exception est publiée avec sa scène et sa mesure. Ne pas annoncer « partout » sur un audit statique. |
| M6 — 60 affichées | Interpolation/rendu stabilisés là où le matériel le permet. | Mesure de présentations physiques et d'artefacts sur plusieurs pièces, avec repli cohérent dans les plus lourdes. |
| M7 — Version test/puis stable | Retour des testeurs, installation propre, sauvegardes, mise à jour VPK seule, guide et journal des limites. | Aucun défaut bloquant reproductible ; incidents Henry/menu/crash revérifiés sur une deuxième console ; rapport complet. |

Les jalons ne sont pas un calendrier. **Point d'arrêt** : si plusieurs essais structuraux d'un bloc chaud n'apportent pas de gain significatif sur Vita ou introduisent des risques de fidélité, consigner la limite et passer au bloc suivant. L'objectif « 60 affichées constantes partout » reste une hypothèse à éprouver, pas une promesse. Le résultat utile minimum est un port sûr avec des pauses réduites et une cadence logique de 30 Hz aussi stable que les scènes et le matériel le permettent.

M0 à M7 qualifient le port et sa cadence ; ils ne signifient pas que tout le moteur est natif. La fin de la reconstruction demandée exige aussi les critères de retrait du runtime x86 décrits dans l'architecture. Une étape doit indiquer séparément le gain mesuré, le périmètre réellement remplacé et les dépendances originales restantes. Décompiler toutes les fonctions en pseudo-C ne satisfait pas à lui seul ces critères.

## Ordre des premières actions

1. Figer un protocole M0 sur la branche expérimentale et confirmer l'état de la Vita avant toute commande ; préserver la sauvegarde originale. Ne pas toucher au ZIP envoyé aux testeurs pendant cette série.
2. Profiler `F_004287f0` par segment en Past 8:24 et dans une scène animée, avec compteur à coût borné ; relever les appels de dessin et changements d'état réellement émis.
3. Construire le banc différentiel de sa première sous-passe encore coûteuse, puis tenter un seul remplacement natif isolé. Comparer au PC original, à la référence PC du port et à la Vita avant d'étendre.
4. Si le gain du fil de jeu survit aux scènes mobiles, mesurer le nouveau plafond : collision en eau, préparation/GPU à l'hôpital, I/O et liaison à la première visite. Continuer selon les chiffres, pas selon la taille apparente d'un fichier C.
5. Garder séparée la qualification du candidat correctif testeur (Henry/menu et crash) : elle conditionne toute future version publique, même si les optimisations obtiennent de bons fps.

## Fichiers de travail

- Décodage et remplacement du binaire GOG : `recomp/x86rec.py`, `recomp/model_uv_native.inc`, `port/src/rt/{x86rt,natives}.c` et les sorties générées **ignorées** `port/gen/`.
- Traduction et rendu : `port/src/d3d8/{draw,resource,device,prep,qgl}.c` ; bibliothèque vitaGL dans une copie isolée et patchs versionnés sous `port/patches/`.
- Entrées, audio et vidéo : `port/src/dx/{dinput,dsound,movie}.c` ; initialisation et budget Vita : `port/src/vita/main.c`.
- Préparation des ressources dérivées : `tools/sh4prep/` et [schémas existants](PREPARED-DATA.md), avec vérification du GOG v5.
- Contrôles : `recomp/difftest.py`, `recomp/difftest_x87.py`, `port/test/`, `tools/pc_regress.sh`, `tools/room_runner.py`, `tools/room_perf.py`, [workflow de build et console](WORKFLOW.md).

Le code publié et les commits éventuels sont attribués uniquement à `vallfrr <valentin.fortier51110@gmail.com>`. Aucun fichier du jeu, sortie `port/gen/` ou `re/out/`, journal, capture ou sauvegarde de test n'entre dans l'historique.
