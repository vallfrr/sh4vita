# Préparation des licences et de la distribution

Audit initial du 28 octobre 2026. **Préparation effectuée, conformité complète non validée.** Aucun changement de licence générale, publication, soumission VitaDB, paquet testeur ou binaire installé n'est effectué par cet audit.

## Demande et constat

Valentin souhaite préparer dès maintenant les éléments nécessaires à une distribution propre. Le [commentaire de Rinnegatamante](https://github.com/Rinnegatamante/VitaDB-contributions/issues/129#issuecomment-6314472673) rappelle les obligations liées à vitaGL/vitaShaRK, et non un formulaire imposant une licence unique à toutes les applications de VitaDB.

Les [termes LGPLv3, section 4](../LICENSES/LGPL-3.0.txt) demandent notamment les notices, les textes de licence et un moyen approprié de modifier/remplacer la bibliothèque dans l'application combinée. Une voie consiste à fournir les sources et les matériaux de reconstruction ; une autre peut utiliser des fichiers objets permettant une nouvelle édition de liens. Un ELF/VPK final n'est pas en lui-même ce paquet de reliaison. L'accès public au dépôt sans termes de réutilisation ni correspondance exacte avec le binaire ne constitue pas une preuve suffisante.

**Point supplémentaire :** `SceShaccCgExt` possède une [licence GPLv3](https://github.com/Rinnegatamante/SceShaccCgExt/blob/102192366c25fe7b8fec073d665494b6172605c3/LICENSE), sans exception repérée dans les fichiers examinés. La ligne de lien locale inclut `-lSceShaccCgExt`. `arm-vita-eabi-nm` confirme les symboles `sceShaccCgExtEnableExtensions`, `DisableExtensions`, `SetInternalSourceFile` et les fonctions d'allocation de ce composant dans l'exécutable local. Il ne s'agit donc pas seulement d'une bibliothèque mentionnée mais inutilisée. Les conséquences de cette combinaison doivent être résolues avant de certifier la distribution ; le seul mécanisme LGPL de reliaison ne règle pas automatiquement une dépendance GPL.

L'audit n'attribue pas à Valentin les droits sur le code original de Konami et ne propose pas de placer le C dérivé de l'exécutable sous notre propre licence. Le fait de demander au joueur son GOG ne démontre pas à lui seul que toutes les conditions de redistribution du binaire combiné sont remplies. Ce point nécessite une décision de distribution documentée ; ne pas publier de code/objets du jeu pour combler mécaniquement une liste de fichiers manquants.

## Éléments préparés

- `LICENSES/` : textes GPL/LGPL de vitaGL local, licence de vitaShaRK, licence de SceShaccCgExt, notices SDL2, TLSF, math-neon, vita-headers et texte MIT.
- `THIRD_PARTY_NOTICES.md` : attribution par composant, modifications du décodeur et des bibliothèques indiquées, distinction des données du jeu.
- Inventaire local de 31 archives de lien avec SHA-256, version du compilateur, révisions SDK, trois états de source vitaGL et patches exacts : `build/license-audit-20261028/inventory.json`. Les preuves sont ignorées par Git et ne sont pas un paquet de publication.
- Copie locale de la ligne de lien et des différences des checkouts vitaGL, sans modifier ces checkouts ni les bibliothèques installées.
- Provenance des textes explicitée dans `LICENSES/README.md` ; aucune licence globale appliquée par simple ajout de ce dossier.

## Correspondance avec les versions distribuées

L'inventaire concerne le build local `sh4vita-guestmem` du jour, pas une certification du VPK v83 ni du paquet testeur du 28 octobre. La référence testeur actuelle est le ZIP SHA-256 `96c36c71409509f178aa18bb5ad56466fde02ad2abf8c8b90a512a15930b3cf5`, avec eboot `2fb943a4a190e8bcc956c93bd3c78ec67b73707791944afe7c03f53bbd1c6b8a`. Ses sources effectives ont été archivées localement dans `build/room-audit/baseline-20261028/` ; tous les changements de cette référence ne sont pas encore représentés par un unique commit publié.

Le build courant référence réellement `vitaGL-sh4-cgcache/libvitaGL.a`, et non le checkout de profondeur seul. Sa base est `2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8`, avec sept fichiers modifiés. Un nom de dossier ou le SHA de base ne suffit donc pas à identifier les sources compilées. Les deux patches publics `port/patches/vitagl-sh4-cache.patch` et `vitagl-shared-depth.patch` ne doivent pas être empilés arbitrairement : leurs instructions précisent le cas d'utilisation.

La révision exacte des archives installées vitaShaRK, SceShaccCgExt et math-neon n'a pas été retrouvée dans cet inventaire. Les textes récupérés à des commits upstream identifiés sont des références, pas une preuve de provenance de ces archives. Le SDK indique GCC 15.2.0 et vita-headers `ebc8f4f7ac8313fe4f74ca95724116b224bad184`. SDL Vita annonce `SDL-release-2.32.8-0-g98d1f3a45` ; le SDL desktop est distinct.

## Travaux à terminer avant la soumission

| Élément | État et critère de clôture |
|---|---|
| Licence de nos propres fichiers | À décider avec Valentin après inventaire de provenance. La mention MIT du Cargo préparateur ne constitue pas une licence globale. Aucun changement automatique. |
| Dépendance GPL SceShaccCgExt | À résoudre : confirmer les termes de la version réellement liée, puis examiner une solution compatible pour le binaire distribué. Une éventuelle autre autorisation requiert l'accord du détenteur, pas une supposition de notre part. |
| Sources exactes des bibliothèques | Reconstituer/pinner les versions réellement utilisées et conserver les modifications et scripts de compilation. Ne pas remplacer un binaire de référence par une nouvelle bibliothèque sans qualification. |
| Reconstruction ou reliaison | Établir la voie choisie et la tester depuis un dossier propre. Les données propriétaires et le C généré ne sont pas ajoutés à Git ; la question des matériaux redistribuables doit être résolue explicitement. |
| Génération locale du jeu | Vérifier que l'export Ghidra, les scripts et les métadonnées nécessaires peuvent être reproduits depuis le GOG pris en charge, sans `re/out` personnel. La génération locale seule ne tranche pas la conformité juridique du binaire distribué. |
| Dépendances transitives | Terminer zlib, taiHEN/stubs, runtime GCC/newlib/libstdc++ et composants inclus dans vitaGL/vitaShaRK, selon ce qui est réellement embarqué. Les dépendances du préparateur Rust ont un lot distinct. |
| Visuels LiveArea | Les nouveaux visuels viennent d'un testeur. Conserver son attribution souhaitée et confirmer son autorisation de redistribution et les éléments incorporés. Ne pas inventer une licence pour ses images. |
| Notices dans les paquets | Incorporer notices et textes dans le VPK et les ZIP finaux, puis inspecter les fichiers réellement livrés. Préparation documentaire seulement pour l'instant. |
| Source correspondant à chaque release | Relier tag/commit propre, options, données techniques de build, patches et empreintes à chaque VPK. Un lien flottant vers `main` n'identifie pas exactement une ancienne version. |
| VitaDB | Préparer la fiche descriptive, liens, besoins et déclaration IA fidèle au mode de développement ; ne rien envoyer sans demande de Valentin. L'acceptation appartient aux mainteneurs. |

## Ordre proposé

1. Finir la provenance et trancher le cas SceShaccCgExt avant de choisir une licence générale pour le port.
2. Proposer à Valentin un choix concret de licence pour les fichiers dont il peut déterminer les termes. Les textes tiers restent inchangés.
3. Figer un candidat final et ses dépendances, puis vérifier la reconstruction/reliaison et le contenu du paquet dans un environnement propre.
4. Publier seulement les matériaux autorisés et nécessaires avec les notices ; conserver le GOG et les données du joueur hors distribution. Soumettre ensuite à VitaDB à la demande de Valentin.

Cette préparation n'a aucun coût pendant le jeu : aucun nouveau traitement CPU, cache ou accès disque n'a été ajouté au runtime. La console n'a pas été utilisée. Les obstacles identifiés restent explicitement ouverts ; ce rapport n'est pas une validation juridique ni une garantie d'acceptation VitaDB.
