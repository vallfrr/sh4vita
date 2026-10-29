# Filtrage natif des segments de collision — expérimental

Ce module réécrit la chaîne `0x4203a0 → 0x41f4a0 → 0x41e6c0 → 0x573a20` à partir de l'exécutable GOG v5 local. Il filtre les polygones traversés par un segment et conserve leur ordre. Ce n'est ni une réécriture de toutes les collisions, ni une modification des données du jeu. Les arbres BSP, les consommateurs de contacts et les trois callbacks de résultat restent ceux du jeu.

## Contrat

`collision_segment.c` travaille sur une vue bornée de la mémoire et un résultat privé, sans CPU invité, allocation, écriture dans le jeu ou cache persistant de pointeurs. Il conserve l'ordre des triangles en éventail, les arrondis intermédiaires et les comparaisons de la version GOG. Il garde au maximum 256 résultats ; un dépassement ou une entrée non prise en charge refuse toute la requête, sans appliquer de résultat partiel.

Le pont `collision_segment_bridge.c` est désactivé par défaut. `SH4_NATIVE_SEGMENTS=1` l'active seulement sur le CPU principal, avec trois couples appelant/callback audités :

| Retour de l'appelant | Callback conservé | Résultat |
|---|---|---|
| `0x420c63` | `0x420a70` | Collecteur ordonné |
| `0x420c9a` | `0x420a90` | Contact avant le plus proche |
| `0x420cda` | `0x420b50` | Contact arrière le plus proche |

Les autres contextes utilisent l'original. Le pont vérifie les plages, les recouvrements avec la requête/pile/état global, le mode flottant, les constantes et les pointeurs des fonctions mathématiques. La table de fonctions compilée est constante : ses neuf identités sont vérifiées une fois, tandis que les pointeurs et constantes modifiables du jeu restent vérifiés à chaque requête non vide. `SH4_SEGMENT_CHECK_EACH=1` rétablit la répétition de ces recherches pour la comparaison de performances. Une liste vide ou de longueur négative ne lit pas ses pointeurs de géométrie/requête inutilisés, comme l'original.

Chaque résultat accepté exécute le callback original. Le pont restaure le CPU autour de ces appels et conserve les registres non volatils, la pile et le contrôle flottant attendus par les trois appelants. Les registres volatils et conditions x87 privés ne sont pas un contrat général pour d'autres appelants.

## Cas indéfini découvert dans l'original

Les callbacks de contact proche peuvent ignorer le refus de `0x41eac0` et lire sa sortie locale non initialisée. Des polygones synthétiques non plans ou coplanaires rendent donc le résultat dépendant du contenu de la pile. Le module ne tente pas de corriger cette règle du jeu : il refuse transactionnellement les cas concernés, puis laisse l'original les traiter dans son contexte. Il conserve aussi le chemin original pour le diagnostic de winding inversé. Ce constat n'établit pas la cause des défauts de textures rapportés par les testeurs.

## Vérification

`port/test/segment_chain_test.py` extrait temporairement les références locales générées et compare au **x86 GOG exécuté sous Unicorn**, au noyau natif et au pont de production. Aucun code généré, exécutable ou polygone du jeu n'est ajouté au dépôt.

- 100 000 requêtes : 28 042 collectes non vides, 83 333 requêtes modifiées et 1 087 collecteurs saturés ; 93 742 acceptations natives et 6 258 refus transactionnels. Les trois callbacks sont exercés.
- 20 000 autres comparaisons couvrent notamment les mots de contrôle flottant `0x23f`, `0x27f`, `0x33f`, `0x37f`. Le `0x23f` réellement utilisé par le jeu est accepté sans modifier le mot du CPU ; le bit réservé 6 n'est normalisé que pour le contrôle d'éligibilité.
- Neuf familles d'entrées invalides/non prises en charge refusent sans écriture partielle ; un test du pont couvre les listes vides/négatives avec pointeurs inutilisés invalides.
- 10 000 comparaisons du noyau **ARM réellement compilé** avec le noyau hôte qualifié, dont 9 375 acceptations, passent avec les modes FPSCR normaux et FTZ/DN. L'ELF du linker et ses symboles sont archivés ensemble.

`SH4_SEGMENT_VERIFY=N` exécute aussi l'original en jeu, compare le résultat et garde son état pendant la vérification. Toute différence désactive la voie native. Les parcours v105 donnent au moins **590 000 vérifications PC et 630 000 Vita, sans différence ni refus** dans les trois pièces mesurées. La première tentative PC rejetait tous les appels à cause du mot de contrôle `0x23f` : elle n'est pas comptée comme preuve.

Les six captures PC et les six captures Vita v105 sont identiques à leurs références ; les images Vita ont été examinées. La régression PC avec la voie native donne 23 images identiques. Ces preuves couvrent ces scénarios, pas tous les états du jeu.

## Mesures provisoires

Avec les mêmes optimisations de modèles activées, sans interpolation et hors fenêtres de capture :

| Pièce, v105 | Original segments | Chaîne native |
|---|---:|---:|
| Prison 4:11 | 39,332 ms | 38,239 ms |
| Past 8:24 | 33,856 ms | 33,655 ms |
| Métro 2:14 | 33,951 ms | 34,055 ms |

Le gain est limité à la prison dans cette comparaison. Il ne démontre pas 30 fps constants. Le profil détaillé v103 sert à identifier les fonctions, pas à convertir directement leur pourcentage en millisecondes : l'instrumentation de chaque appel et les options de compilation perturbent cette charge très riche en petits appels.

v106 compare, dans le même binaire natif, les recherches répétées de fonctions à leur validation unique et au retour immédiat sur liste vide : **38,753 → 37,322 ms** dans la prison. Les deux captures sont identiques. Les compteurs correspondent entre les essais ; sur 200 000 listes acceptées, 13 483 sont vides, sans refus. Ce second écart ne doit pas être additionné aux moyennes d'une autre série pour annoncer un gain général.

## Déplacements et cinématiques (v107)

La comparaison suivante utilise les sticks dans la voie contrôleur habituelle. Les douze positions/rotations relevées et les six captures correspondent exactement. Les images ont été examinées ; la prison montre notamment Henry au contact d'un ennemi en train de l'attaquer dans les deux variantes. Le maintien des PV est une option explicite du banc, identique des deux côtés ; ce parcours ne remplace pas une qualification de toute la progression.

| Parcours avec déplacement | Segments originaux | Segments natifs |
|---|---:|---:|
| Prison 4:11 | 41,139 ms | 37,946 ms |
| Past 8:24 | 33,380 ms | 33,397 ms |
| Métro 2:14 | 34,710 ms | 33,512 ms |

Il y a un gain mesuré dans la prison et le métro de ce parcours, sans gain dans Past. Les vues changent avec le mouvement : ces écarts ne s'additionnent pas à ceux des vues fixes. La cadence globale à 30 Hz reste non acquise.

Les cinématiques 1 puis 8 se terminent avec **16 captures PC et 16 captures Vita identiques** à leurs références, examinées. Les vues de Henry présentes ont des cheveux, un visage et des vêtements normaux ; le contexte à fond noir du clip 8 est conservé sur les deux plateformes et reste narrativement non qualifié. Aucun nouvel échec de rendu n'est déduit de ce fond.

Avec interpolation dans les vues fixes, les moyennes sont 39,921 / 38,673 / 34,282 ms dans prison / Past / métro. Past dépasse encore les 1 536 Kio du tampon de rejeu. Ce lot ne résout pas cette limite et ne démontre pas 60 images affichées constantes.

Preuves locales ignorées : `build/room-audit/native-segments-v105/`, `segment-dispatch-v106/`, puis `segment-qualified-v107/`. Les sauvegardes, binaires et configurations normaux sont restaurés et relus après les séries. Aucun changement de VPK public, ZIP des testeurs ou branche principale n'est déduit de ces essais.
