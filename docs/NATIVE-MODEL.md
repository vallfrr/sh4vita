# Modèles GOG v5 : premier remplacement structurel

**Historique technique des essais successifs.** Les anciens statuts « expérimental/désactivé/en attente » décrivent leur version de l'époque. Pour l'état courant, les défauts effectivement retenus et la reprise sur console, commencer par [HANDOFF.md](HANDOFF.md), puis le rapport du29octobre lié dans ce fichier.

Travail expérimental du 26 octobre 2026, sur `codex/60fps-engine`. Voir la [roadmap](ROADMAP-PORTAGE-NATIF.md) pour les objectifs et les portes de qualification.

## Origine vérifiée

REA 6.7.0 et Ghidra 12.1.4 relisent le binaire local GOG v5, SHA-256 `cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0`. La décompilation de `0x4287f0` a pour identifiant d'évidence `ev_98d425cb2ab92c92f4df3ea4cc5c36e71f05127b1ffe780bfa785fdf8e45a2d6`. Les instructions et la décompilation restent sous `re/out/` et `build/`, ignorés par Git. Le pseudocode contient des variables non résolues ; les contrats ci-dessous ont été vérifiés dans les instructions du binaire et le C généré, sans recopier aveuglément ce pseudocode.

## Contrat de la palette de matrices

Dans la boucle des groupes de modèle, après la sélection du shader, `0x428e64..0x428e9d` parcourt vingt indices 32 bits. La table vient de `groupe + R32(groupe + 0x58)`. Chaque indice autre que `0xffffffff` désigne trois vecteurs de quatre flottants, soit 48 octets, dans le tampon invité `0xf629c0 + indice * 64`. La destination commence à la constante de sommet 34 ; chaque entrée avance de trois constantes, jusqu'à la constante 93 incluse. Une entrée `-1` conserve les constantes précédentes.

Il n'y a entre ces vingt appels ni dessin, ni lecture des constantes, ni changement d'état différent. Leur état graphique final peut donc être préparé en un bloc, en conservant les constantes correspondant aux indices absents. La couche de préparation doit copier ce bloc dans sa propre file avant le retour : aucun pointeur sur un tableau de pile ne survit à l'appel.

Le candidat `d3d_model_palette` de `port/src/d3d8/device.c` est activé uniquement par `SH4_NATIVE_PALETTE=1`. Le générateur ajoute un point d'entrée avant la boucle originale et conserve cette boucle comme repli. Le candidat vérifie que le périphérique est celui du port et que la méthode indirecte est toujours l'implémentation attendue de `SetVertexShaderConstant`. Un périphérique ou une méthode différente garde la voie originale. Le comportement par défaut reste inchangé pendant l'évaluation.

L'état de sortie invité à conserver comprend aussi : `EDI=94`, `EDX=table+80`, le pointeur de table sur la pile, les registres `EAX/ECX` issus du dernier passage, et les vingt octets d'arguments/retour laissés sous `ESP` par le dernier appel effectif. Les comparaisons finales de la boucle donnent l'égalité de 94 avec 94 ; le générateur rétablit ces drapeaux. Le remplacement ne change pas la pile x87.

## Vérification indépendante du chemin graphique

`port/test/model_palette_test.py` compile les fonctions de production concernées dans une petite bibliothèque de test. Unicorn exécute les **instructions x86 originales** du GOG v5 sur les mêmes matrices, indices et registres. Seul l'appel de périphérique est raccordé à une copie des constantes ; l'oracle n'est pas une seconde implémentation C de la boucle.

La série de 20 000 comparaisons vérifie les registres généraux, les octets laissés sur la pile et les 96 constantes du périphérique. Elle couvre toutes les entrées absentes, toutes présentes, chaque emplacement isolé, chaque trou isolé, des masques et données binaires aléatoires, puis une répétition inchangée de chaque cas. La répétition ne doit émettre aucun enregistrement. Une méthode indirecte différente doit refuser le chemin natif sans modifier le CPU. Résultat : 199 616 appels originaux pour 9 999 enregistrements groupés ; aucune différence d'état dans ce corpus. Ce ratio décrit le corpus, pas les performances du jeu.

Commande, avec Unicorn installé pour le Python utilisé et un compilateur natif :

```text
python port/test/model_palette_test.py --cc gcc --cases 10000
```

Les builds PC et Vita réussissent. La régression PC existante conserve ses 23 images de référence à l'identique avec le candidat activé. Une paire PC sur prison 4:11, Past 8:24 et métro 2:14 conserve également les six captures octet par octet. Les six vues ont été examinées : Henry et le décor sont présents dans l'eau, Henry et un chien sont visibles dans le métro ; la caméra de diagnostic de Past est contre une paroi, ce qui limite sa valeur visuelle. Ce parcours ne remplace ni un combat, ni une cinématique, ni une partie normale.

Dans une fenêtre PC de Past, les enregistrements de préparation passent de 9 827 à 4 139 par image. Les fenêtres hôte ne constituent pas une mesure de cadence Vita. La première tentative Vita a perdu la connexion avant la première pièce mesurée ; elle a été arrêtée et restaurée avant une reprise autorisée par l'utilisateur.

## Résultats Vita et limites

Même binaire avec l'option à 0 puis 1, interpolation coupée, 180 images de stabilisation et 450 de mesure demandées. Les chiffres ci-dessous utilisent les fenêtres statistiques entières situées dans l'intervalle établi et excluent les captures. Ils ne donnent pas une distribution p95/p99 par image.

| Scène | Référence, ms | Palette native, ms | Écart | Images mesurées référence/native |
|---|---:|---:|---:|---:|
| Prison 4:11 immobile | 42,372 | 41,171 | -1,200 | 381 / 392 |
| Past 8:24 immobile | 44,121 | 41,142 | -2,979 | 411 / 396 |
| Métro 2:14 immobile | 35,853 | 35,123 | -0,729 | 338 / 345 |
| Past 8:24 déplacement + caméra | 40,939 | 39,062 | -1,877 | 349 / 414 |

Les huit captures Vita des candidats sont identiques octet par octet à leurs références appariées et ont été examinées. Les six vues de la référence stationnaire correspondent aussi aux six images de l'ancien candidat à boucles bornes/UV natives. La seconde capture mobile de Past montre Henry près de la porte 302 ; l'autre reste contre une paroi. Cela vérifie un parcours scripté limité, pas toute la pièce en partie normale. Les événements narratifs et le cycle mort/rechargement des testeurs ne sont pas couverts.

Le gain est **modeste** et le budget de 33,33 ms est encore dépassé dans les quatre cas. L'option reste expérimentale et désactivée par défaut ; le ZIP des testeurs n'est pas remplacé. La sauvegarde originale (`04315d28a0639804cf37c1c9336278a8ae46a300133ae1daf58c4c99dcccca38`), le binaire qualifié (`a00ef94dbab228565dedbb188a802ae12ef5c21f9e487b65d4c1e995b3789527`) et la configuration normale ont été relus après chaque paire. Le lancement normal final a produit un nouveau journal de démarrage.

## Coûts restants identifiés

Un diagnostic séparé mesure des intervalles sans chevauchement dans la fonction de modèles. Des compteurs détectent les entrées répétées avant fermeture ou les intervalles non fermés ; seule la version corrigée, avec zéro erreur sur les fenêtres utilisées, sert au classement. Ce diagnostic ajoute des lectures d'horloge et ne remplace pas les mesures de cadence ci-dessus.

Dans Past sur Vita avec la palette native, les fenêtres de 60 images montrent environ 1,3 ms de collecte, 4,0–4,1 ms de palette, 1,2 ms de mise à jour de positions, **4,6 ms de bornes** et **8,0–8,1 ms de conversion UV**. La soumission de dessin varie autour de 5,3 ms et monte à 15,5 ms dans une fenêtre : elle inclut les appels et attentes du fil de jeu, pas seulement le temps GPU. Les boucles bornes/UV restent insérées dans la grosse fonction recompilée et son unité compilée pour la taille. Le prochain essai consiste à les isoler dans des fonctions natives compilées normalement, puis à réduire les accès mémoire répétés avec des gardes d'alias et une comparaison différentielle. Les horloges de diagnostic ont ensuite été retirées ; la section `.text` du binaire PC reconstruit correspond exactement au candidat déjà validé.

Les preuves locales sont sous `build/room-audit/native-model-v85/` : résultat REA, builds, comparaisons x86, journaux, captures et restauration. Aucun fichier préparé, modèle, texture ou shader du joueur n'est modifié par ce prototype.

## Fonctions de sommets isolées (v86)

`port/src/rt/model.c` isole les deux passages déjà natifs, auparavant intégrés dans la fonction générée compilée pour la taille. Le fichier natif utilise les options normales du port (`-O2`). Le calcul des bornes accumule les quatre extrema dans des variables locales en conservant leurs mots binaires d'origine. La conversion UV garde le même ordre des opérations en double, les mêmes branches et le même statut de comparaison x87 ; elle utilise une copie intégrée lorsque les plages sont disjointes et ne réécrit qu'une fois les derniers arguments laissés sur la pile invitée.

Les plages mémoire invalides, les chevauchements entre source/destination/pile et les destinations qui recouvrent les constantes prennent la voie conservée. L'option reste désactivée par défaut : `SH4_NATIVE_VERTICES=1` active les deux passages, `bounds` ou `uv` permet de mesurer un seul passage. `SH4_NATIVE_PALETTE=1` est maintenu dans les mesures suivantes.

Le test `port/test/model_vertices_test.py` compare les fonctions isolées aux **passages natifs conservés**, et non à un nouvel oracle x86 complet. Les 20 000 comparaisons passent sur l'état CPU entier, la pile, tous les octets des sommets, les constantes, les zéros signés, infinis, NaN et leurs payloads, les seuils et les passes vides. Les replis d'alias et de plage sont exercés. Les six captures PC des trois pièces correspondent au candidat v85 ; la régression PC de 23 images est également identique. Un compteur PC temporaire confirme 72 000 passages réussis pour chaque fonction dans la séquence observée, sans repli ; ce compteur a été retiré.

| Past 8:24, interpolation coupée | ms/image | Images mesurées | Écart à la référence |
|---|---:|---:|---:|
| Passages conservés | 41,498 | 390 | — |
| Bornes isolées seules | 40,890 | 395 | -0,608 ms |
| UV isolées seules | 40,042 | 403 | -1,457 ms |
| Deux passages isolés | 39,757 | 405 | -1,742 ms |

Chaque candidat conserve ses deux captures Vita à l'identique de la référence. Aucun fatal ou message d'erreur GL/GXM n'est relevé dans ces journaux. La sauvegarde, le binaire qualifié et la configuration normale sont relus après chaque série. Les modes ont été testés successivement, avec 180 images de stabilisation et 450 demandées par cas ; des répétitions supplémentaires seraient nécessaires pour départager les petites différences entre modes. Les événements narratifs, les autres pièces et un parcours mobile Vita avec v86 restent hors de cette qualification.

Le résultat est une amélioration limitée et une séparation du code utile aux prochaines réécritures ; **il n'établit pas 30 fps** et ne justifie pas une promotion dans le paquet des testeurs. Ces fonctions et leurs replis restent expérimentaux. Les preuves et symboles sont conservés dans `build/room-audit/native-vertices-v86/`.

## Contrat à établir pour remplacer la préparation entière d'un modèle

Les adresses sont des observations du GOG v5, pas une interface publique stable. Les noms ci-dessous décrivent les lectures vérifiées ; les champs non compris doivent rester opaques.

| Structure | Champ | Utilisation observée |
|---|---:|---|
| Modèle | `+0x08` | Offset relatif vers l'en-tête contenant le nombre de groupes et leur table. |
| Modèle | `+0x1c`, `+0x20` | Nombre et offset relatif d'indices 16 bits vers le premier ensemble de blocs de 64 octets. |
| Modèle | `+0x24`, `+0x28` | Nombre et offset relatif pour le second ensemble de blocs. |
| Modèle | `+0x14`, `+0x18` | Nombre et offset de séquences de mises à jour de positions ; ne pas les traiter comme de la géométrie immuable. |
| Modèle | `+0x38` | Offset vers l'identifiant 16 bits utilisé pour choisir une entrée de la table UV du paquet courant. |
| Modèle | `+0x40` (octet) | Sélection parmi quatre chemins de matériau ; les chemins rares doivent conserver leur repli. |
| Groupe (pas `0x5c`) | `+0x1c`, `+0x3c` | Emplacements invités des handles de tampon d'indices et de sommets, créés au premier besoin. |
| Groupe | `+0x40`, `+0x44`, `+0x48` | Nombre d'indices, nombre de sommets et taille de sommet. Les passages étudiés utilisent 64 octets. |
| Groupe | `+0x4c` | Bits participant au choix du shader avec le mode de matériau et les drapeaux globaux. |
| Groupe | `+0x50`, `+0x54` | Offsets relatifs vers les données servant à initialiser les tampons. |
| Groupe | `+0x58` | Offset relatif de la palette des vingt indices de matrices étudiée en v85. |

Le début de `0x4287f0` collecte les blocs depuis deux régions du paquet courant désigné par `0xf8d094`, à `+0x6000` et `+0x8000`, vers `0xf629c0`. L'ordre et les indices proviennent du modèle. Chaque groupe initialise ses tampons si nécessaire, applique les séquences de positions, effectue éventuellement la conversion UV et émet le dessin dans l'ordre original. Plusieurs appels pour un même descripteur dans une image peuvent être de véritables passes distinctes ; leur identité ne permet pas de supprimer un dessin.

Le prochain remplacement complet doit avoir une interface native entre le descripteur extrait du GOG, les tampons et l'état D3D8, avec un contrat pour chaque mode de matériau. Il doit couvrir les créations/libérations et les identités de ressources (génération + offset), pas seulement un instant où tous les tampons sont déjà chauds. Une adresse d'objet recyclable ne suffit jamais comme clé. Les séquences d'appels observables, les déformations, les changements de pièce et les effets rares devront être comparés avant de rendre cette voie active par défaut. Cette fonction complète n'est pas encore réécrite.

## Correction de l'oracle UV et contrôle original (v89)

L'audit des instructions GOG a trouvé une erreur commune aux deux passages UV expérimentaux : le `FLD [ESP+0x30]` à `0x429164` précède le retrait des 12 octets d'arguments de copie. Il lit donc la **largeur UV** au déplacement normal `+0x24`, et non le minimum X à `+0x30`. Le test v86 comparait deux implémentations partageant cette erreur. Les captures antérieures n'avaient pas révélé de différence dans leurs scènes échantillonnées ; elles ne suffisaient pas à établir la correction générale.

Les deux passages utilisent désormais la largeur. `port/test/model_uv_original_test.py` compare directement les instructions de l'exécutable GOG sous Unicorn : 40 000 comparaisons UV passent (octets des sommets, pile, huit registres et conditions/TOP x87), ainsi que 20 000 comparaisons des bornes sur mémoire et registres vivants. Les 20 000 comparaisons entre passages conservés et isolés passent également. Le premier cas du nouvel oracle reproduisait l'erreur avant correction. Cette erreur expérimentale n'est **pas reliée** aux corruptions des anciens VPK signalées par les testeurs.

`SH4_ORIGINAL_MODEL_UV=1` conserve un chemin de référence exécutant le bloc d'instructions original, et désactive le remplacement UV isolé. Sans cette option, le passage natif corrigé reste utilisé. La régression PC finale donne 23 images identiques ; six captures PC et six captures Vita comparant directement l'original et le natif sont identiques. Les captures Vita ont été examinées : Henry texturé dans Water et Subway ; la vue de Past reste partiellement obstruée par le mur à cette entrée.

| Vita, mêmes options natives palette/sommets/collision | Original UV | UV corrigées | Interpolation activée |
|---|---:|---:|---:|
| Water 4:11 | 39,220 ms | 39,170 ms | 42,057 ms |
| Past 8:24 | 47,553 ms | 39,512 ms | 43,665 ms |
| Subway 2:14 | 33,799 ms | 33,905 ms | 35,694 ms |

Il s'agit de séries successives, sans réduction de précision ou de qualité. L'interpolation produit 381 images intermédiaires pour 385 images logiques dans Water, 335/339 dans Subway, mais aucune dans Past : la capture de la trame dépasse le budget de rejeu de 1 536 Kio. Ces mesures ne démontrent donc ni 30 images logiques constantes ni 60 images affichées constantes. Augmenter simplement ce budget a déjà été défavorable dans un essai précédent ; il faut réduire le coût et le volume du travail enregistré.

Preuves, journaux, binaires et symboles appariés : `build/room-audit/native-uv-v89/`. La sauvegarde, le binaire normal et l'absence d'environnement de test ont été restaurés et vérifiés avant de relancer normalement la console. Aucune promotion dans le paquet des testeurs.

## Propriété des UV par ressource (v96/v97, expérimental)

Le bloc original situé entre le retour de verrouillage `0x4290ac` et le déverrouillage `0x42928f` lit un tampon de sommets entier pour ses bornes et ses UV, puis écrit dans le tampon de travail distinct à `0xf529b0`. Le nouveau pont reconnaît uniquement ces deux sites, sur le CPU principal, avec offset/taille/drapeaux de verrouillage nuls. Les verrous génériques restent des écritures potentielles : **tous** invalident les métadonnées UV, y compris les mises à jour de positions. Aucune exception supposant qu'un autre verrou ne change que XYZ n'a été ajoutée.

Les métadonnées appartiennent à l'objet `Buffer`, pas à une table d'adresses réutilisables. Le nombre de sommets, les quatre extrema initiaux, la ressource, le CPU propriétaire et la version du verrou doivent correspondre. Les plages hors tampon ou mémoire invitée, les chevauchements avec la pile, les sorties ou les constantes reprennent le passage conservé. Les numéros de version existants pour les uploads GPU et les indices continuent d'être renouvelés sur les verrous/déverrouillages ; leur contrat n'est pas affaibli.

- `SH4_NATIVE_MODEL_UV_CACHE=1` conserve seulement les bornes du tampon jusqu'à une écriture potentielle.
- `SH4_NATIVE_MODEL_UV_CACHE=2` conserve aussi la préparation UV indépendante des offsets. Le noyau typé `port/src/rt/model_uv.c` ne manipule ni CPU invité ni adresses globales. Il prépare les calculs en double puis applique les offsets actuels et copie les **autres canaux actuels** à chaque utilisation. L'ordre d'addition du cas spécial Y est conservé.
- Sans l'option, ces réutilisations restent désactivées. Elles exigent les passages natifs correspondants ; `SH4_ORIGINAL_MODEL_UV=1` garde toujours son rôle d'oracle.
- `SH4_MODEL_UV_CACHE_VERIFY=N` exécute le passage conservé pour chaque réutilisation vérifiée, compare le résultat attendu et conserve les sorties originales. Une différence désactive la réutilisation. Ce mode sert à la correction, pas à la mesure de vitesse.

Le cache de préparation est plafonné à **512 Kio** au total, possède une allocation par ressource et libère son budget avec celle-ci. Les paramètres complets participent à la clé, sauf les offsets appliqués à chaque dessin. Les entrées ou paramètres non finis, les allocations refusées et les tampons dépassant le budget reprennent le calcul conservé. Les trois pièces consomment 213 168 octets dans le contrôle observé.

Le noyau préparé passe 60 000 comparaisons UV avec le x86 GOG original, plus 20 000 comparaisons des bornes. Le pont réel de verrouillage/cache passe 250 002 comparaisons avec les passages natifs conservés sur CPU entier, pile et octets des sommets, avec écritures génériques, changements de paramètres/nombre de sommets, réutilisation d'adresse, échec de vérification et libération du budget. Les vérifications réelles atteignent 90 000 passages sans différence dans chaque famille sur PC et Vita ; les six captures appariées PC et Vita sont identiques.

| Même binaire, interpolation coupée | Référence sans réutilisation | Bornes seules v96 | Bornes + préparation v97 |
|---|---:|---:|---:|
| Water 4:11 | 39,282 ms (v96/v97) | 39,234 ms | 39,269 ms |
| Past 8:24 | 39,729 ms (v96), 39,600 ms (v97) | 39,190 ms | 39,309 ms |
| Subway 2:14 | 33,961 ms (v96), 34,014 ms (v97) | 34,168 ms | 34,107 ms |

Les gains sur la cadence finale sont faibles ; **cette migration ne résout pas les 30 fps**. L'interpolation de v97 donne 42,182 / 43,754 / 35,762 ms dans les trois pièces, avec respectivement 379/383, 0/418 et 391/395 images intermédiaires par image logique mesurée. Past dépasse encore le budget de rejeu. Ces chiffres ne qualifient pas 60 images affichées constantes.

Le profil détaillé v95 trouve encore `0x4287f0` en tête (39,4 % des 23 476 échantillons, 600 images, environ 25 049 appels invités par image). Un diagnostic de phases v99, avec compteur de défauts de délimitation nul, observe environ 4,3 ms dans les bornes et 6,9 ms dans les UV de Past. Ce sont des durées internes instrumentées et non une économie de cadence démontrée ; les coûts des copies, des soumissions et des attentes doivent être départagés avant de décider de la suite. La fonction complète de préparation/rendu reste à migrer.

Les sauvegardes, binaires et environnements normaux ont été restaurés après les séries v96/v97. Preuves : `build/room-audit/model-ownership-v96/`, `model-uv-prepared-v97/`, `native-hot-v95/` et `model-phases-v99/`. Les transformations et les performances des autres états narratifs restent à qualifier ; aucun paquet public n'est remplacé.


## Données préparées et trafic mémoire (v98 à v101)

La seconde série de phases v99 explique le faible gain de v97 : les bornes de Past passent d'environ **4,35 à 0,24 ms**, mais l'application des UV préparées monte de **6,90 à 10,53 ms**. Le scan évité est presque compensé par le nouveau passage mémoire. Ces durées sont instrumentées ; elles ne doivent pas être présentées comme un gain sur la cadence finale.

Le candidat v98 filtrait les attributs absents du shader traduit et regroupait leurs octets utiles dans les enregistrements de rendu. Il gardait les valeurs et les indices exacts. Les tests de copie/déclaration, 6 000 enregistrements réels décodés en lectures d'attributs, les 23 images PC et six captures PC en mouvement passent. Sur Vita dans Past, il change pourtant **39,705 en 39,729 ms**, ne regroupe que six dessins par image et réduit leur charge d'environ 29 à 22 Kio. Avec interpolation, la moyenne atteint **44,071 ms sans image intermédiaire** : la trame reste trop grande. Cet essai est **retiré du code**, ses preuves et sources restant sous `build/room-audit/vertex-streams-v98/`.

v100 ajoute un préchargement explicite borné aux entrées valides, huit sommets à l'avance, dans le passage UV préparé. Les valeurs, la précision et les copies restent les mêmes. `SH4_MODEL_UV_PREFETCH=1`, avec `SH4_NATIVE_MODEL_UV_CACHE=2`, donne **35,827 ms contre 39,705 ms** sans cache dans la première paire Vita Past ; les deux captures sont identiques et examinées. Ce résultat concerne cette vue statique et demande encore répétition et qualification mobile. Il ne démontre pas 30 fps constants.

v101 compare ce passage à une copie en bloc de tous les canaux courants, suivie de l'application des UV préparées (`SH4_MODEL_UV_PREFETCH=2`). Le pont a déjà vérifié les plages complètes et disjointes ; aucune valeur de position n'est conservée d'une image à l'autre. Cette variante passe 60 000 comparaisons UV contre le x86 GOG et 20 000 comparaisons de bornes. Sa qualification est complétée ci-dessous ; les tests de calcul seuls ne déterminent pas le gain Vita. Toutes ces voies restent désactivées par défaut.


La première paire v101, après retrait de v98 du binaire, mesure **35,973 ms** pour la répétition du préchargement et **33,706 ms** pour la copie en bloc, dans la même vue Past. Les deux captures Vita restent identiques. Le temps CPU estimé du fil principal passe de 33,708 à 32,688 ms par image dans les fenêtres retenues ; les temps de préparation/rendu sont concurrents et ne s'additionnent pas. La moyenne murale demeure au-dessus de 33,3 ms : aucun constat de 30 fps constants.

La variante en bloc passe également les **23 images de régression PC**, les **six captures PC avec déplacements/caméra mobile**, et les **16 captures PC des cinématiques 1 puis 8**, toutes identiques aux références correspondantes. Les images des cinématiques ont été examinées : cheveux, visage et vêtements normaux dans les vues d'objets/acteurs présentes. Le fond noir de la séquence 8 existe aussi dans la référence : son contexte narratif demeure non qualifié. Les 16 captures Vita des cinématiques sont elles aussi identiques à la référence v90 et examinées. La validation du mouvement par sticks est précisée ci-dessous. Preuves : `build/room-audit/model-prefetch-v100/`, `model-bulk-v101/`, puis `model-qualified-v102/`.


### Limite découverte dans le scénario de déplacement Vita

Les options `SH4_AUDIT_MOVE` et `SH4_AUDIT_SWEEP` écrivaient uniquement des touches clavier. La voie contrôleur Vita ne recevait pas ces entrées. Les captures v102, malgré leurs noms `motion`, ne suffisent donc pas à qualifier un parcours par sticks ; elles montrent bien des scènes animées et six images identiques entre référence et candidat, mais le mouvement du joueur doit être confirmé séparément. Les preuves des anciens essais doivent être lues selon les positions réellement visibles, pas selon le seul nom du scénario.

La paire v102 donne prison **41,494 / 41,466 ms**, Past **37,777 / 34,091 ms** et métro **33,752 / 33,876 ms**. Ces mesures restent exploitables comme comparaison de ces vues ; elles ne démontrent pas un gain général. Les six images appariées ont été examinées. L'interpolation du candidat donne **42,372 / 38,389 / 35,973 ms** dans les mêmes trois pièces ; le détail des images intermédiaires doit être conservé avec les fenêtres, sans assimiler ce nombre à des horodatages d'affichage physique.

Le diagnostic v104 ajoute l'injection des sticks dans la voie contrôleur, uniquement pendant un tour explicitement activé et sa fenêtre de mouvement. Il utilise la réponse et le mappage habituels, et journalise quelques positions/rotations réelles du joueur. Sans audit, ou hors fenêtre, il ne modifie aucune entrée. Cette correction du banc n'est pas une optimisation du moteur et ne change pas les commandes de la version normale. La paire Vita dans Past est terminée : les deux captures sont identiques et les quatre positions/rotations relevées correspondent exactement entre référence et candidat. Henry passe de (-483, 0, -20) à (249,637, 0, -246,267). Les vues confirment le déplacement et Henry reste correctement texturé. Les fenêtres mesurées donnent 33,973 ms sans réutilisation contre 33,300 ms avec copie en bloc ; le changement de vue rend ce parcours plus léger que la vue fixe initiale. Les fenêtres exclues autour des captures diffèrent entre variantes : cet écart limité ne qualifie pas une économie générale. La régression PC finale après la correction du banc reste à 23 images identiques. Les 250 002 comparaisons du pont sont également relancées avec le mode en bloc réellement activé, sans différence. Preuves dans `build/room-audit/pad-motion-v104/`.

## Calcul UV NEON v123 : écarté après mesure

Un diagnostic PC des trois pièces trouve 70 385 lots admissibles sur 90 000 appels (5,61 millions de sommets sur 7,28 millions). Le candidat conservait les UV préparées en double et ajoutait, pour ces seuls lots, deux floats exacts par sommet. Les offsets et coordonnées devaient être finis, non négatifs, normaux ou nuls, représentables sans perte en float ; l'arrondi matériel devait être au plus proche. Les autres cas revenaient au calcul double. Les deux additions float étaient exécutées avec NEON sur Vita ; les autres canaux étaient copiés depuis les sommets courants.

Les 20 000 cas comparés aux instructions x86 GOG passent, dont 1 745 lots utilisant effectivement le nouveau calcul. Les 4 000 lots du binaire ARM compilé correspondent bit pour bit au calcul double qualifié, y compris avec FTZ/DN ; les refus d'offset négatif et d'arrondi non admissible ne touchent pas la sortie. Le test de propriété des tampons passe 250 002 comparaisons dans chaque mode. La régression PC reste à 23 images exactes ; 70 000 utilisations sont observées dans le parcours PC des trois pièces et ses six captures correspondent à la référence. Sur Vita, le nouveau chemin est aussi exécuté plus de 70 000 fois ; les six captures appariées sont identiques et examinées.

| Vita, même binaire, interpolation active | Double en bloc | Floats NEON |
|---|---:|---:|
| Water 4:11 | 38,297 ms | 38,254 ms |
| Past 8:24 | 38,115 ms | 38,233 ms |
| Subway 2:14 | 33,776 ms | 33,844 ms |

Le changement varie de −0,043 à +0,118 ms : **aucun gain utile**. Le tableau float ajoute environ 100 Kio dans ce parcours et concurrence le plafond de 512 Kio du cache. Le candidat a été retiré du code et les builds normaux PC/Vita reconstruits ; ses scripts, binaires et mesures restent dans `build/room-audit/model-uv-packed-v123/`. Les sauvegarde, binaire et configuration originaux ont été vérifiés restaurés sur Vita. Cette expérience indique que les additions UV ne dominent plus le coût final dans ces scènes ; une nouvelle piste doit mesurer les copies, les soumissions et les attentes restantes avant de modifier le moteur.

## Balayage des acteurs v124 : écarté après mesure

Le profil Past v111 attribuait 8,2 % des échantillons propres à `F_00553850`, répartiteur des phases des acteurs. La fonction balaie 300 emplacements de 0x430 octets plusieurs fois par image. Un remplacement C facultatif a conservé l'ordre et la relecture des emplacements, car un rappel peut activer l'acteur suivant. Comparé aux instructions de l'exécutable GOG, il reproduit exactement les registres, la pile, les 300 emplacements et l'ordre des rappels dans 400 cas aléatoires, y compris cette mutation. Le parcours PC Past l'a effectivement exécuté plus de 14 000 fois ; la régression PC donne 23 images identiques.

La paire Vita utilise **le même binaire**, avec le remplacement désactivé puis activé, l'interpolation et les autres optimisations retenues. Les trois pièces sont atteintes, les six captures sont identiques entre les deux passes et à la référence v123.

| Vita | x86 recompilé | Balayage C |
|---|---:|---:|
| Water 4:11 | 38,383 ms | 38,369 ms |
| Past 8:24 | 38,147 ms | 38,173 ms |
| Subway 2:14 | 33,847 ms | 33,878 ms |

Les écarts de −0,014 à +0,031 ms ne montrent **aucun gain matériel**. L'échantillonnage de fonctions avait surestimé l'intérêt de cette boucle. Le remplacement a été retiré, le code généré et les builds PC/Vita ont été restaurés. Les scripts et preuves restent dans `build/room-audit/actor-dispatch-v124/`. La Vita a retrouvé sa sauvegarde, son binaire et sa configuration d'origine, empreintes vérifiées ; le lancement normal a été envoyé. La suite doit cibler le coût mesuré de la préparation et soumission des modèles, sans supposer qu'un appel fréquent est coûteux.


## Piste de réduction des transferts — recherche du29octobre, non implémentée

Le passage UV retenu copie encore64octets par sommet puis réécrit8octets d’UV. Le pont d3d_model_uv dispose déjà d’un propriétaire Buffer, d’une version de lecture, de plages complètes disjointes et d’une invalidation au Lock écrit. Ce contrat ouvre une piste différente du passage floatNEON déjà rejeté : conserver les canaux immuables dans un bufferGPU et transférer séparément les8octets d’UV courantes, calculées avec l’arithmétique double puis float d’origine. Ne pas remplacer ce calcul par une addition de shader moins précise pour annoncer un gain sans perte.

Avant prototype : vérifier au véritable DrawUP que seuls les mots24/28 diffèrent de la source pour les lectures concernées ; rechercher toute modification intermédiaire de position/couleur/normale ; conserver les autres DrawUP intacts. Un tag d’adresse temporaire seul ne suffit pas : version, propriétaire, génération, taille et durée de vie pendant la file doivent être définis. Si l’objet source est libéré avant consommation, le record doit garder sa propre référence. Les lecteurs CPU du bloc destination doivent être recensés avant d’en supprimer la copie.

Première variante possible : garder la copieCPU de référence, mais spécialiser uniquement le transport du record et les attributsGPU en séparant UV dynamiques/canaux statiques. Mesurer d’abord octets copiés et temps de soumission ; ne retirer le chemin complet qu’après captures différentielles, changement de matériaux/poses, invalidation, libération et cinématiques. Cela ne constitue ni un gain mesuré ni une réécriture déjà réalisée. Le gros orchestrateur0x4287f0 contient994instructions ;0x4297c0 est seulement un relais vers0x429590, son coût de profil est inclusif. Réécrire le relais ne supprimera pas le travail de ses descendants.
