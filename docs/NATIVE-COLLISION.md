# Collision native — expérience v87

Branche `codex/60fps-engine`, GOG v5, 26 octobre 2026. **Désactivée par défaut** : `SH4_NATIVE_COLLISION=1` active le candidat. Le VPK expérimental n'est pas le ZIP des testeurs.

## Périmètre remplacé

`collision_native.c/.h` fournit une interface de scène bornée et de requête, sans `CPU *`, registre invité, appel x86 par sommet, allocation ni cache persistant. Un appel remplace le parcours sphère/BSP `0x41d670`, le passage des listes `0x420c10`, la préparation et le filtrage `0x41f570`, les normalisations `0x41ac30` utilisées dans cette chaîne et la collecte ordonnée `0x420a70`. La liste conserve la limite originale de 256 polygones candidats et son ordre de saturation.

Le module lit encore le format GOG à travers une vue des octets de scène. Il ne possède pas encore une géométrie décodée indépendante. Les contacts finaux, les listes dynamiques ajoutées par `0x5621b0`, les segments/rayons et leurs autres appelants restent sur leurs chemins existants. Ce lot n'est donc ni toute la collision ni toute la simulation native.

Le parcours itératif remplace la récursion et le filtrage manipule directement les vecteurs et bornes locaux. Les arrondis float, l'ordre des opérations double et les seuils sont conservés ; aucune modification des obstacles, de la précision, des ennemis ou du rendu n'est recherchée. La zone de travail globale et la liste de candidats sont recopiées à la frontière pour les consommateurs qui restent recompilés.

## Frontière et replis

Le `WRAP` `nat_collision_sphere` dans `collision.c` accepte uniquement le CPU principal et les deux sites dont les retours sont `0x561ba2` et `0x561f60`. Les appelants écrasent les résultats volatils EAX/ECX/EDX et les conditions x87 avant de les utiliser ; le cœur natif n'en dépend pas. Les registres conservés par la convention d'appel, la pile x87 et son contrôle sont préservés. Les autres entrées, y compris les appels récursifs de la référence, conservent l'original. Ce contrat ne signifie pas que tous les registres volatils et les octets inutilisés de pile sont identiques après l'appel.

Les constantes du GOG, les fonctions de normalisation/collecte et les limites de mémoire sont vérifiées. Les vues qui chevauchent la requête, la zone de travail partagée ou la pile active refusent le chemin natif. Les formats non couverts, valeurs non finies, dépassements de portée/profondeur/nombre de nœuds reviennent à l'original. Le calcul est transactionnel : aucun octet de jeu n'est écrit avant son acceptation complète. Aucun pointeur n'est conservé entre requêtes ; une libération ou réutilisation de ressource ne peut pas laisser un cache de géométrie périmé dans cette version.

Le correctif existant qui efface une racine BSP périmée après une ressource nulle reste en place. Les données et sauvegardes du joueur ne changent pas de format.

## Vérifications

- `port/test/collision_chain_test.py` : 100 000 requêtes synthétiques, **2 091 876 candidats**, 6 471 listes saturées, 55 424 écritures de requête. Binaire x86 original exécuté sous Unicorn, références C générées et module natif donnent les mêmes listes, compteurs, requêtes et 168 octets de travail. Arbres à plusieurs niveaux, branches rejetées, triangles/quads, formats à trois sommets effectifs, plans inclinés/dégénérés, coordonnées aléatoires et listes initialement remplies sont couverts.
- Six vues invalides/non couvertes (portée, cycle, nombre d'enfants, données de polygones, format de sommets, NaN) refusées sans écriture invitée partielle.
- PC : premier parcours de trois pièces terminé ; les 10 000 premières requêtes réelles ont été comparées à la chaîne générée, sans écart ni refus, puis le module natif a continué seul.
- Vita : parcours de comparaison des requêtes terminé dans eau 4:11, Past 8:24 et métro 2:14 ; les points de contrôle de 10 000 à **40 000 requêtes** n'ont montré aucun écart ni refus. Le dernier point de contrôle est une borne inférieure, pas le nombre final exact d'appels.
- PC : 23/23 captures de régression identiques, six captures stationnaires identiques au candidat précédent et six captures avec déplacement/balayage identiques entre référence et module natif.
- Vita : six captures identiques, examinées visuellement. Même binaire, interpolation coupée, palette et passes de sommets natives actives : eau **40,882 → 39,346 ms**, Past **39,614 → 39,571 ms**, métro **34,973 → 33,873 ms**. Respectivement 395/410, 408/358 et 346/416 images mesurées dans les fenêtres hors captures. Le gain reste limité et ne démontre pas 30 Hz.
- Sauvegarde d'origine SHA `04315d28a0639804cf37c1c9336278a8ae46a300133ae1daf58c4c99dcccca38`, binaire qualifié SHA `a00ef94dbab228565dedbb188a802ae12ef5c21f9e487b65d4c1e995b3789527` et configuration normale relus/restaurés après les séries. Lancement normal consigné.

`SH4_COLLISION_VERIFY=N` compare les N premières requêtes natives acceptées aux sorties de l'original, en conservant le résultat et l'état CPU originaux pendant la vérification. Un écart est journalisé et désactive immédiatement le candidat pour le reste du processus. Ce diagnostic ne mesure pas la vitesse du chemin natif, puisqu'il exécute aussi l'ancien code.

Les preuves, exécutables, symboles et sauvegardes restent localement dans `build/room-audit/native-collision-v87/`. Les tests précédents ne couvrent pas tous les états de progression, les contacts finaux, toutes les trajectoires d'ennemis ni toutes les mutations possibles de ressources. La qualification des déplacements, escaliers et interactions reste nécessaire avant toute promotion.

## Suite du module

L'essai v88 conservait les bornes/normales/plans de 1 024 triangles/quads dans une table de 128 Kio, avec comparaison exacte de tous les octets sources avant réutilisation. Son corpus original/generated/native/cache de 100 000 requêtes passait (3 621 636 réutilisations), ainsi que 40 000+ comparaisons réelles PC et Vita et les 23 captures PC. Les six images Vita restaient identiques. Pourtant les moyennes sans/avec cette table étaient **39,408/39,545 ms** dans l'eau, **39,528/39,556 ms** dans Past et **33,961/34,003 ms** dans le métro : aucun gain. **Essai rejeté et table retirée du code courant.** Les preuves et binaires restent dans `build/room-audit/native-collision-cache-v88/` ; ne pas relancer ce même cache sans nouveau coût démontré.

La limite conservatrice de coordonnées ±2^20 ajoutée pendant cet audit conserve l'original pour les cas susceptibles de débordement flottant et de branches x87 non ordonnées ; sept vues invalides/non couvertes sont testées. La composante w finie n'est pas une coordonnée et n'a pas cette limite.

Migrer ensuite les contacts et consommateurs pour retirer la copie de compatibilité des états invités. Priorité de performance immédiate : la préparation de modèles dans Past, où cette collision native n'a presque aucun effet mesuré. Les deux modules restent des étapes du portage, pas une preuve d'une cadence globale acquise.

## Liste de polygones directe (v108, expérimentale)

Le profil de la prison v103 a retrouvé `0x41f570` hors du raccordement BSP déjà remplacé : `0x420c10` l'appelle aussi directement. Le service partagé expose maintenant `sh4_collision_polygons(view, polygons, count, query)` ; le parcours BSP conserve son comportement et réutilise le même calcul. Il n'y a ni nouvelle copie de géométrie ni cache de ressource ajouté.

Le nouveau pont, désactivé sans `SH4_NATIVE_SPHERE_LIST=1`, accepte uniquement le retour `0x420c33`, le collecteur ordonné original et les modes flottants audités sur le CPU principal. Les contrôles de plages, de recouvrements et de constantes demeurent actifs. Même une liste vide/négative initialise les valeurs de travail originales : elle n'est pas assimilée au retour immédiat du [module segments](NATIVE-SEGMENTS.md).

`port/test/sphere_list_test.py` compare le x86 GOG, la référence générée, le service partagé et le pont de production. 100 000 requêtes donnent 1 850 171 candidats ordonnés, 6 031 listes saturées et 30 019 écritures de sphère, sans différence sur toute la zone globale de collision, la requête, les géométries et la convention d'appel. Une seconde série de 20 000 requêtes couvre aussi les rayons négatifs, plusieurs valeurs de w et huit familles de refus sans écriture partielle. Les quatre mots de contrôle flottant utilisés par le banc sont exercés. Le pont compare ses 1 000 premiers résultats à l'original, puis sa voie native effective est utilisée.

Le code ARM réellement compilé passe 10 000 comparaisons avec le noyau hôte qualifié contre le x86, y compris les modes FTZ/DN ; toute la requête, le travail et les candidats sont identiques. Après la factorisation, l'ancien parcours BSP passe de nouveau 20 000 comparaisons GOG/généré/natif et ses sept familles de refus. `SH4_SPHERE_LIST_VERIFY=N` garde l'original pendant la comparaison et désactive la voie native au premier écart.

Le pont évite ensuite de remettre à zéro toute la liste privée et de recopier ses 256 entrées dans les deux sens : seul le suffixe réellement ajouté est écrit. Le préfixe et les entrées inutilisées restent en place dans le jeu. La série finale de 100 000 comparaisons du pont, avec les cas de bord supplémentaires, donne 1 782 257 candidats, 5 775 saturations et 27 875 écritures de sphère, sans différence sur l'ensemble des états comparés. Le calcul géométrique ARM reste identique.

Les trois pièces sentinelles passent au moins **60 000 vérifications PC et 60 000 Vita**, sans différence ni refus. Le parcours PC élargi demande douze destinations et en atteint dix : les deux autres reviennent à l'appartement dans cet état narratif et sont exclues de la couverture. Dans les scènes atteintes, les bornes inférieures relevées sont 80 000 comparaisons de listes sphériques et 120 000 de segments, sans écart ni refus. Deux scènes atteintes utilisent un point d'origine non qualifié comme entrée normale. Cela ne reconstitue pas une partie complète.

La régression PC finale donne **23 images identiques**. Les six captures Vita du parcours avec sticks, les douze poses relevées et les 16 images des cinématiques 1/8 sur chaque plateforme restent identiques à leurs références ; les images ont été examinées. Sauvegarde, binaire et environnement normaux sont restaurés et relus après la série.

| Avec déplacement, mêmes autres options natives | Liste originale | Liste native |
|---|---:|---:|
| Prison 4:11 | 37,522 ms | 36,301 ms |
| Past 8:24 | 33,300 ms | 33,753 ms |
| Métro 2:14 | 33,349 ms | 33,356 ms |

Le gain concerne surtout la prison. Le résultat Past inclut une fenêtre à 36,6 ms avec une pointe à 109,2 ms ; les journaux montrent aussi des lectures synchrones des archives audio pendant ce parcours. Cet écart n'est ni masqué ni déclaré résolu par la collision native. La cadence globale stable à 30 Hz reste non acquise.

Les preuves sont dans `build/room-audit/sphere-list-v108/`. Le raccordement reste désactivé par défaut ; aucun VPK public ou ZIP des testeurs n'est remplacé.
