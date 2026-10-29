# Qualité des textures — comparaison expérimentale

Branche expérimentale, 27 octobre 2026. Les options ci-dessous servent à mesurer une amélioration visuelle ; les valeurs par défaut du port ne changent pas pendant les essais.

## Réglages distincts

- `SH4_TEXMAX=256` est la limite actuelle des niveaux de mip envoyés au GPU. `512` conserve davantage de détail lorsqu'un niveau correspondant existe. Une texture à un seul niveau n'acquiert pas de nouveau détail avec ce réglage. Les ressources et fichiers GOG ne sont pas réécrits.
- Le stockage courant emploie RGB565 pour les textures opaques, RGBA4444 pour certaines textures avec alpha, et RGBA8 pour certaines textures avec alpha jusqu'à 256×256. Les conversions peuvent ajouter une trame destinée à atténuer les bandes de couleur.
- `SH4_TEX32=1`, nouveau réglage de laboratoire, envoie les texels décodés en RGBA8, sans réduction en 16 bits ni trame de conversion. Il reste désactivé par défaut. La décompression DXT logicielle reste en place : sa présence répond à des corruptions du décodage matériel observées précédemment, et ne constitue pas une réduction de résolution.

Les deux réglages doivent être séparés : relever la limite à 512 peut aussi modifier le format choisi pour une texture avec alpha. Une image plus détaillée n'est donc pas automatiquement meilleure sur tous ses canaux. Les cibles de rendu et la résolution de l'écran ne sont pas changées par ces essais.

La hausse concerne principalement le stockage GPU et les uploads. Les surfaces verrouillables conservent leur organisation dans la mémoire invitée ; ces options ne démontrent pas une correction de l'épuisement de ce tas. La mémoire libre, le temps de chargement et les pauses doivent être observés en plus des FPS.

## Mesures historiques

La matrice du 25 octobre employait un moteur plus ancien et les mêmes scènes dans chaque paire. Avec interpolation :

| Scène | Limite 256, ms/image logique | Limite 512, ms/image logique | Intermédiaires / images logiques, 256 → 512 |
|---|---:|---:|---:|
| Forêt 3:13 | 33,311 | 33,300 | 0,989 → 0,989 |
| Hôpital 7:32 | 33,520 | 33,507 | 0,010 → 0,375 |
| Eau 4:11 revisitée | 48,990 | 49,074 | 0,989 → 0,989 |

Les temps moyens sont pratiquement inchangés dans cet échantillon. La variation de l'interpolation à l'hôpital ne suffit pas à établir un gain : sa décision adaptative doit être contrôlée et répétée. Un intermédiaire soumis n'est pas une mesure de cadence physique de l'écran. Ces résultats ne couvrent ni les longues sessions, ni toutes les scènes.

## Campagne actuelle

v116 compare les limites 256 et 512 sur le **même binaire v113 archivé**, avec les services natifs qualifiés, l'interpolation activée, les mêmes scènes Eau 4:11 / Passé 8:24 / Métro 2:14 et deux vues par scène. Chaque pièce bénéficie de 180 images d'attente et 600 images d'observation. Les fenêtres de mesure touchant les captures sont exclues. Le binaire ne contient pas le nouveau traitement audio par lots.

La paire v116 est terminée : les six vues appariées ont été examinées dans `texture-quality-v116/review.png`. Les grilles et surfaces proches gagnent du détail ; aucun défaut évident de personnage ou de surface n'est apparu dans cet échantillon. La vue du passé reste proche d'un mur et montre peu Henry : elle mesure une charge lourde, sans qualifier à elle seule l'apparence des personnages de cette pièce.

| Scène | 256 → 512, ms/image logique | Intermédiaires / images logiques | VRAM libre minimale relevée, 256 → 512 |
|---|---:|---:|---:|
| Eau 4:11 | 38,188 → 38,136 | 0,989 → 0,989 | 62 220 → 58 465 Kio |
| Passé 8:24 | 37,973 → 37,953 | 0 → 0 | 56 286 → 50 654 Kio |
| Métro 2:14 | 33,377 → 33,337 | 0,875 → 0,862 | 60 118 → 55 169 Kio |

Les écarts moyens sont inférieurs à 0,1 ms et ne constituent pas un gain de vitesse. Le métro présente une faible variation de la décision adaptative d'interpolation ; le passé ne produit toujours aucun intermédiaire. Les plus longues images des fenêtres complètes restent comparables : 95,0/96,6 ms, 92,5/92,6 ms et 43,7/45,2 ms. La qualité supérieure ne règle donc pas les pauses. Les chargements mesurés restent proches, respectivement 2 879/2 851, 2 605/2 626 et 2 487/2 481 ms. Aucun incident fatal n'est observé.

Les mesures, captures et configurations sont dans `build/room-audit/texture-quality-v116/`. La sauvegarde d'origine SHA256 `04315d28a0639804cf37c1c9336278a8ae46a300133ae1daf58c4c99dcccca38`, le binaire d'origine et l'environnement normal ont été restaurés et contrôlés après la paire. **512 est un candidat favorable dans ces scènes, pas une validation universelle ni un nouveau réglage public.**

La paire de formats v117 est également terminée, à limite 512 dans les deux variantes. Les six vues appariées ont été examinées ; aucun défaut évident de surface, d'alpha ou de personnage n'est apparu. Les différences de couleur sont discrètes sur ces captures sombres : l'absence de conversion en 16 bits est établie par le chemin de code, sans prétendre à un changement visuel spectaculaire dans toutes les vues.

| Scène | Formats courants → RGBA8, ms/image logique | Intermédiaires / images logiques | VRAM libre minimale, formats courants → RGBA8 |
|---|---:|---:|---:|
| Eau 4:11 | 41,157 → 41,205 | 0,989 → 0,989 | 58 465 → 50 101 Kio |
| Passé 8:24 | 38,223 → 38,216 | 0 → 0 | 50 654 → 38 919 Kio |
| Métro 2:14 | 34,852 → 34,821 | 0,986 → 0,986 | 55 169 → 45 014 Kio |

Les écarts moyens complets restent inférieurs à 0,05 ms. Une lecture partielle de la première fenêtre avait donné environ +0,24 ms dans l'eau ; le tableau retient la fenêtre complète. Les chargements et les plus longues images sont comparables. Le coût principal observé est la consommation supplémentaire de 8 à 12 Mio de VRAM. Sauvegarde, binaire et environnement normaux sont restaurés et contrôlés après cette paire.

**Les temps absolus de v116 et v117 ne doivent pas être comparés entre eux.** Une dérive locale du garde de mot de contrôle x87, retrouvée entre les builds, faisait refuser le service natif de segments pour le vrai mot `0x23f` dans les deux variantes v117. Leur comparaison de formats utilise bien le même binaire, mais leur charge CPU diffère de v116. Le garde qualifié est restauré pour v118 ; les quatre mots de contrôle et 20 000 requêtes comparées au GOG passent de nouveau. Cette dérive n'est pas une conséquence du réglage de texture.

Le contrôle v118 compare **256/formats courants** à **512/RGBA8**, sur le même binaire avec les services natifs actifs. Hôpital 7:32 et Monde final 9:2 sont atteints dans les deux variantes. Le troisième saut demandé, vers Forêt 3:13, expire en restant dans le monde 9:2 dans les deux variantes : **la forêt est exclue**, même si le script a consommé ses trois demandes et écrit `finished`.

| Scène réellement atteinte | Standard → qualité, ms/image logique | Intermédiaires / images logiques | VRAM libre minimale relevée |
|---|---:|---:|---:|
| Hôpital 7:32 | 33,467 → 33,521 | 0,585 → 0,583 | 53 979 → 9 817 Kio |
| Monde final 9:2 | 34,759 → 34,567 | 0,015 → 0,012 | 45 044 → 15 005 Kio |

Les quatre vues appariées ont été examinées ; les surfaces et personnages visibles ne montrent pas de nouvelle corruption évidente. Le cadrage du monde final est proche de certains modèles et ne qualifie pas une traversée normale de la scène. Les chargements sont comparables (2 749/2 697 et 2 695/2 635 ms). Les plus longues images restent importantes et varient : 113,2/129,3 ms et 157,4/151,2 ms. Aucun nouveau fatal n'est observé, mais ce test ne démontre pas l'absence de pauses. Les empreintes de sauvegarde et binaire d'origine sont relues et l'environnement normal restauré après la série.

## Décision

La limite 512 est un candidat favorable : davantage de détail, sans coût moyen mesurable dans les trois scènes v116. Le RGBA8 ajoute lui aussi peu de coût moyen dans les cas comparés. **Son coût mémoire est toutefois réel** : il ne reste qu'environ 9,6 Mio de VRAM libre dans un relevé de l'hôpital. Sans une qualification plus large ou une politique de budget, il ne devient pas un réglage global.

L'option `SH4_TEX32=1` est donc conservée pour l'expérimentation ; les valeurs normales restent 256 et formats courants. Pour reproduire la qualité supérieure testée : `SH4_TEXMAX=512` et `SH4_TEX32=1`. Pour n'augmenter que le détail : `SH4_TEXMAX=512`. Aucun paquet public ni préparateur n'est remplacé. Le travail de performance reprend ensuite sur le traitement audio par lots puis les grands traitements du moteur.

Une amélioration n'est retenue qu'après revue des captures, absence de nouveaux défauts, marge mémoire et comparaison de la fluidité. Les tests de qualité ne remplacent pas la qualification des scénarios de jeu ni l'optimisation du moteur.
