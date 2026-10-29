# Reconstruction native du jeu — cible et migration

Décision de périmètre du 26 octobre 2026. Travail expérimental sur `codex/60fps-engine` ; la version qualifiée et le ZIP des testeurs restent la référence. Voir la [feuille de route](ROADMAP-PORTAGE-NATIF.md) pour les mesures et la qualification.

## Résultat recherché

Construire une base native C adaptée à la Vita à partir du GOG v5 : ressources du joueur, règles, scènes et événements du jeu original. La cible dépasse des raccourcis dans le C recompilé. Les modules finaux doivent manipuler des modèles, acteurs, collisions, matériaux et événements explicites, avec leurs propres durées de vie, sans transporter un `CPU *` dans leurs boucles internes.

La décompilation fournit une description à vérifier, pas les sources originales récupérées automatiquement. Le C généré et le binaire restent les oracles pendant la migration. On remplace des chaînes complètes avant de supprimer leurs anciens chemins. Commencer un exécutable vide et perdre le jeu jouable ne facilite ni l'équivalence ni la mesure.

Les gains de quelques boucles ne plafonnent pas les gains possibles de cette architecture. Inversement, un module écrit en C natif peut rester lent s'il répète les mêmes calculs, copies et soumissions. Le travail doit porter sur l'organisation et les volumes traités, puis sur les instructions. Aucun gain global ou 30 fps constants n'est encore démontré.

## Frontières visées

| Module | Propriété native attendue | Référence conservée pendant la migration |
|---|---|---|
| Ressources et scènes | Identités avec génération, vues de formats GOG vérifiées, chargement/libération explicites, éventuelles données dérivées versionnées. | Chargeurs d'origine et fichiers préparés du joueur. |
| Collision | Scène spatiale, requêtes, contacts et ordre de parcours explicites ; mathématiques et seuils vérifiés. | BSP et tests du binaire, corpus de contacts et trajectoires. |
| Modèles et animation | Descripteurs décodés, palettes, poses, déformations et tampons avec une durée de vie connue. | Chaîne `0x4287f0`, animations et instant des événements. |
| Rendu | Matériaux et commandes natifs, ordre des passes, transitions de cibles et synchronisation explicites. | États D3D8 observés, images PC/Vita et effets du jeu. |
| Simulation | Acteurs, progression, inventaire, caméra, événements, hasard et cadence de 30 Hz explicites. | Fonctions et tables d'origine, parcours narratifs et sauvegardes. |
| Plateforme | SDL2, fichiers, audio, vidéo, allocation et fils sans contrats Win32 invités internes. | Services du port actuel tant que des appelants x86 subsistent. |

Chaque module peut d'abord recevoir un adaptateur aux adresses invitées. Cet adaptateur est une frontière temporaire, à un nombre limité de points d'entrée. Déplacer des `R32/W32` dans un nouveau fichier en gardant tout l'état CPU ne remplit pas la cible finale.

## Stratégie d'intégration

1. Établir le contrat du système et son propriétaire de données : création, mutation, réutilisation et libération. Séparer les faits vérifiés des hypothèses.
2. Capturer des entrées/sorties de référence, y compris appels et effets observables. Préserver les cas rares et les changements d'état, pas seulement les images d'une pièce immobile.
3. Construire le module natif avec des types et des tests indépendants du runtime x86. Garder les adaptateurs et les replis hors du cœur du module.
4. Remplacer une chaîne cohérente dans le jeu expérimental. Comparer la référence et le nouveau module dans les mêmes scènes, puis mesurer sur Vita. Un noyau non raccordé n'est pas une optimisation du jeu.
5. Migrer les consommateurs et retirer les anciens intermédiaires. Une copie native ajoutée à côté de toutes les copies invitées peut aggraver le budget mémoire ; la suppression des doublons fait partie du travail.
6. Retirer l'ancien chemin uniquement après sa qualification. Documenter les fonctions et données qui obligent encore à conserver le runtime invité.

Le coût du diagnostic reste séparé de celui du jeu. Aucun changement de qualité, d'ennemis, de précision ou de comportement ne compte comme gain équivalent. La sauvegarde et la mise à jour du seul VPK restent compatibles.

## Premier lot : chaîne de collision sphère/polygones

La reconnaissance initiale donne ce chemin dans le GOG v5 :

| Adresse | Rôle observé | Points à préserver |
|---|---|---|
| `0x561a20` | Installation d'une ressource de collision. | En-tête, polygones et racine BSP ; le correctif de racine périmée sur ressource nulle doit subsister. |
| `0x41d670` | Parcours récursif des nœuds pour une sphère. | Ordre : contenu du nœud, puis enfants dans l'ordre de la table ; nombre d'enfants relu après les appels. |
| `0x420c10` | Passage du contenu d'un nœud au filtrage des polygones. | Nombre et offset relatif du contenu ; installation du callback `0x420a70` avant filtrage. |
| `0x41f560` | Choix du callback global. | Écriture à `0xed7500` ; possibilité de modification par des appels imbriqués. |
| `0x420a70` | Collecte des identifiants de polygones candidats. | Tableau à `0xed7538`, compteur à `0xed7938`, limite de 256 entrées ; conserver l'ordre et la saturation. |
| `0x41f570` | Bornes des polygones, distance au plan et tests d'arêtes. | Ordre des candidats et callbacks, écritures de la sphère, scratch global et arrondis. |
| `0x41ac30` | Normalisation utilisée dans le filtrage. | Contrat exact encore à caractériser avec ses cas dégénérés. |

Ces rôles ont été lus dans la décompilation, l'assembleur et le C recompilé locaux ; ce tableau n'est pas encore un remplacement natif qualifié. La liste collectée contient des candidats, pas les contacts finaux : ses consommateurs doivent aussi être caractérisés. `0x420cf0` remet le compteur à zéro ; `0x420d00` et `0x420d10` exposent le nombre et une entrée. L'ancien essai natif de boîte/rayon n'avait pas montré de gain Vita et n'est pas une preuve en faveur d'une nouvelle réécriture identique.

### Formats et état déjà observés

- Nœud de `0x41d670` : **maximum** xyz à `+0x00`, nombre d'enfants entier signé à `+0x0c`, **minimum** xyz à `+0x10`, contenu à `+0x20`, table d'offsets d'enfants relatifs au nœud à `+0x28`. L'ordre maximum/minimum se vérifie par les tests `rayon + maximum - centre` et `centre - (minimum - rayon)` du binaire. Le contenu comporte un nombre à `+0x20` et un offset relatif au contenu à `+0x24`. Leur validité et leur durée de vie doivent être établies au chargement.
- Requête sphère : centre xyz à `+0x00`, composante w à `+0x0c`, rayon à `+0x10`. Le filtrage peut écrire w ; elle ne doit pas devenir une entrée supposée immuable.
- Polygone : sommets de 16 octets, nombre physique d'enregistrements dans l'octet `+0x0c` du premier sommet ; le bit 0 de l'octet `+0x0d` impose trois sommets au test. La progression vers le polygone suivant utilise toujours le nombre physique. Ne pas confondre les deux comptes.
- Zone globale `0xed7460..0xed7507` : bornes de la requête et du candidat, dernier sommet, callback et drapeaux. Les appels sortants peuvent la lire ou la modifier. Une copie en variables locales exige des points de synchronisation et des tests de réentrance.
- Les tests de nœud ne sont pas tous arrondis de manière symétrique : certaines valeurs intermédiaires passent par un stockage float avant comparaison, d'autres restent en précision double dans la recompilation. Un test vectoriel générique n'est pas encore démontré équivalent.
- Le parcours segment/BSP `0x41da00` contient du découpage de segment par des plans et ses propres tolérances. Il ne peut pas être remplacé automatiquement par le parcours de boîtes de la sphère.

### Travail avant le raccordement

Caractériser les écritures sur ces ressources et la durée de vie des données de pièce ; un pointeur réutilisable ne suffit pas comme identité. Établir le contrat des callbacks et des requêtes imbriquées. Enregistrer des requêtes réelles pour eau, métro et appartement, puis comparer les contacts, leur ordre et les trajectoires, en plus des images. Le premier livrable de performance devra couvrir parcours, filtrage et remontée des contacts ; un autre petit test arithmétique isolé ne le remplace pas.

Le banc `port/test/collision_chain_test.py` exécute déjà la chaîne de collecte complète du binaire GOG sous Unicorn face aux références C générées, compilées temporairement. Il compare nombre, ordre et identités des candidats, écritures de la requête et état du callback ; il vérifie aussi l'absence d'écriture des géométries. Le corpus synthétique couvre des arbres à plusieurs niveaux, rejets de branches, triangles/quadrilatères, drapeau imposant trois sommets, plans dégénérés et saturation à 256 candidats. Il n'est ni une mesure de performance ni une preuve concernant les contacts finaux, tous les registres/arrondis intermédiaires ou les états d'une partie réelle. Le futur module natif devra être ajouté comme troisième implémentation au même banc et confronté aux requêtes réelles.

## Critères de fin de reconstruction

Le port peut atteindre 30 fps avant la fin de cette migration. À l'inverse, la reconstruction n'est complète que lorsque la simulation et les modules du jeu n'ont plus besoin du `CPU`, de la pile x86, des appels par adresses et du tas invité pour fonctionner. Les chargeurs doivent encore consommer les ressources légitimes du GOG supporté, et le port doit rejouer la progression, les sauvegardes, les cinématiques et les effets attendus. Un inventaire des dépendances restantes doit justifier chaque retrait.

État actuel : les noyaux de modèles et leur réutilisation par ressource sont décrits dans [NATIVE-MODEL.md](NATIVE-MODEL.md). La [collecte sphère/BSP et les listes directes](NATIVE-COLLISION.md), ainsi que le [filtrage de segments](NATIVE-SEGMENTS.md), disposent de services natifs transactionnels et de comparaisons au GOG, sur PC et sur Vita. Les contacts finaux, les autres parcours BSP, les modèles entiers et la simulation entière restent à reconstruire. Aucun pourcentage global de réécriture n'est établi. Les options expérimentales restent désactivées par défaut et le paquet envoyé aux testeurs n'est pas modifié.
