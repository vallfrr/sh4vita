# Adaptation Vita : registre avant/après et retours arrière

Ouvert le 28 octobre 2026 à la demande de Valentin. Complète le
[suivi des preuves](CONSOLE-ADAPTATION-PROGRESS.md).
Chaque prochaine modification de réglage doit être ajoutée ici **avant** son
activation par défaut : valeur précédente, valeur candidate, portée, preuve,
commit et procédure de retour. Une hypothèse reste une hypothèse.

## Référence joueur conservée

- Binaire de référence : `build/room-audit/stutter-v146/eboot-window-observe.bin`.
  SHA-256 : `2fb943a4a190e8bcc956c93bd3c78ec67b73707791944afe7c03f53bbd1c6b8a`.
- ZIP testeur conservé : `build/release/sh4vita-test-2026-10-28.zip`.
  SHA-256 : `96c36c71409509f178aa18bb5ad56466fde02ad2abf8c8b90a512a15930b3cf5`.
- Sauvegarde originale de cette campagne : 694288 octets,
  SHA-256 `f39b8e485113b0907332d809e37b8b8c6b9ead4f3bc5e2010debffe835f9fa20`.
  Copies vérifiées dans les sous-dossiers `original/` des essais
  `build/room-audit/console-files-20261028/`.
- Dernière restauration vérifiée : `pad-sample-vita/restored.json`, puis
  `inventory-info-vita/restored.json`. Binaire et sauvegarde ci-dessus,
  configuration normale, aucune entrée scriptée persistante.

Le binaire joueur contient aussi des correctifs antérieurs présents dans le
répertoire de travail : un simple ancien commit Git n'est **pas** son équivalent
garanti. Les artefacts ci-dessus sont les références exécutables. Ne pas écraser
ces fichiers avec un nouveau build. Ne pas employer `git reset --hard` pour un
retour : cela détruirait du travail indépendant.

## Changements de cette campagne

« Source » signifie intégré aux sources locales, pas automatiquement installé
comme nouveau VPK joueur ni envoyé aux testeurs.

| Point | Avant | Après / portée | État, preuve et retour |
|---|---|---|---|
| Sauvegarde GOG | CREATE_ALWAYS sur la destination, puis écriture ; retour de fermeture ignoré par le jeu | Écriture/vidage/fermeture du voisin `.sh4stage`, puis remplacement, sans suppression préalable de la destination | Prototype OFF dans `17124e8`, ON par défaut dans `40fe6af`. Format/MD5 identiques, appels GOG options et slots 0/15/29 comparés ; vrais menus PC/Vita qualifiés. `SH4_SAFE_SAVE=0` retrouve le seul transport original à titre diagnostic. Pour un retour complet, revenir au binaire joueur conservé. |
| Format et emplacements des sauvegardes | `save/sh4.sav`, 694288 octets, signature 0x7ffff005, MD5 sur 694272 octets | **Inchangés** ; aucune autosauvegarde, aucun nouveau slot | La protection du transport ne garantit ni coupure électrique ni récupération d'un fichier déjà logiquement corrompu. Elle ne change pas la logique de réparation GOG au chargement. |
| Sensibilité persistée | `camera.cfg`, défaut 3, crans 0..10 | **Valeurs inchangées** ; transport en deux étapes et échec visible | `17124e8` / `40fe6af`. Ne devient valeur sauvegardée que si l'écriture réussit. Pas de transaction atomique entre ce fichier et sh4.sav. |
| Erreurs Win32 | Succès parfois annoncé après erreur partielle, de vidage ou de fermeture ; erreurs OVERLAPPED masquées | Erreur réelle et nombre d'octets exact, métadonnées allouées avant ouverture susceptible de tronquer | `17124e8`, `5af5f96`. Tests de panne. Retour logiciel ciblé de ces commits, avec revue des dépendances ; pas un réglage de gameplay. |
| Messages système | Journal seulement sur Vita, réponse OK supposée | Boîte Vita MB_OK, CP1252 converti en UTF-8, résultat réel ou 0 | `9d94cf1`. Auto-fermeture à 120 swaps uniquement avec `SH4_MESSAGE_TEST` ; fermeture complète observée à 174 swaps dans la sonde. Aucun auto-OK en mode joueur. Bouton physique à qualifier. |
| Échec d'enregistrement des options | Résultat GOG ignoré par le menu | Message localisé, une fois par tentative ; la caméra privée ne se valide pas après l'échec | `40fe6af`. Panne ENOSPC simulée avant ouverture, sauvegarde originale identique, reprise du rendu jusqu'à 2623 présentations. Le jeu peut garder des options appliquées en RAM malgré l'échec de persistance signalé. |
| Retour de boîte système | Entrée tenue susceptible d'être relue au retour | Chaque périphérique attend son propre repos ; axes neutres et POV 0xffffffff ; seuil de repos pad ±6000 sur les quatre axes | `40fe6af`. Le seuil ±6000 ne s'applique **qu'à cette attente**, pas à la zone morte normale. Tests indépendance clavier/pad, maintien, buffers et absence de relecture SDL supplémentaire en jeu normal. |
| Lecture physique pad | Axes et boutons relus à chaque GetDeviceState, croix relue séparément | Échantillon axes/boutons commun à un périphérique et une présentation ; invalidation après boîte système | `40fe6af`. Six captures Vita et 23 captures PC identiques. Retour ciblé à l'ancien adaptateur à reconstruire si la sensation doit être comparée ; conserver les correctifs fenêtre/FPS antérieurs. |
| Paire caméra TPS — essai non retenu | Stick droit fourni sur Z/Rz (8/20), Rx/Ry neutres en TPS | Essai SH4_TPS_AXIS_PAIR=1 avec miroir Rx/Ry (12/16), puis **retiré du runtime** | Deux comparaisons Vita (sans puis avec L) : la seconde paire du jeu change bien, mais les 12 captures comparées restent identiques dans 4:11. Aucun bénéfice caméra visible attesté ici ; ne pas généraliser à tous les contextes. Les valeurs et le code précédents sont rétablis, aucune variable à régler pour revenir en arrière. Preuves tps-axis-routing-vita / tps-axis-with-l-vita conservées. |
| Injection de boutons bruts pour les essais | Aucun script avant le mapping | SH4_PAD_RAW=frame:masque_hex:durée, seulement dans un build SH4_TEST_OPTIMIZATIONS | a074f96 ; absent du build de production sans cette option, OFF sans variable ; retirer la variable restaure la lecture physique seule. Aucun changement de valeur de gameplay. |
| Détecteur de pointeur | Hystérésis avancée à chaque appel | Une avance par image échantillonnée | `ec82697`. Seuils ON 6 / OFF 1 / saturation 10 **inchangés**. L'identification par coordonnées reste provisoire. |
| Info et esquive | Boutons DI 9 et 10 sans correspondance physique | Prototype L+carré → DI9 ; R+rond → DI10 avec visée maintenue ; partenaires consommés jusqu'au relâchement | `ec82697` puis `40fe6af`. `SH4_PAD_COMBOS` reste **OFF par défaut** ; enlever la variable ou mettre 0 retrouve le mapping précédent. Masques exhaustifs et consommateurs GOG testés ; vraie animation/contexte inventaire-combat pas encore qualifiés. |
| Cycle de vie | Politique anti-veille existante, sans observateur explicite | Observateur opt-in, file fixe de 8 événements, dispatch au plus une fois par présentation | `eba7226`. `SH4_LIFECYCLE_TRACE` **OFF par défaut**. Aucun nouveau thread ; anti-veille et temporisations inchangées. Pas de suspension/reprise physique validée. |
| Allocation descriptions/objets DirectInput | Résultat nul susceptible d'être déréférencé ou écrit comme adresse invitée 0 | E_OUTOFMEMORY, sortie objet nulle ; aucun callback vers description nulle | `49710db`. Quatre chemins testés par injection, ABI des cas réussis conservée, builds PC/Vita OK. Pas une cause prouvée des crashs testeurs. |
| Texte d'options | Allocation non contrôlée | Erreur explicite si épuisement invité, sans écriture à zéro | `40fe6af`. Échec/reprise/cache vérifiés ; ne résout pas l'épuisement lui-même. |
| Intégrité du transfert — chantier en cours | sh4prep 0.2 écrit seulement nombre/taille agrégés ; le démarrage contrôle la présence de l'exe et du dossier data | Prototype d'index files.idx : chemins/tailles, total et FNV-1a ; contrôle des tailles avant lancement du moteur si annoncé dans le JSON | **Pas encore livré.** Anciennes données sans index restent prises en charge. Ne lit pas les Go de contenu, ne contrôle pas les corruptions de même taille et ne touche pas aux sauvegardes. Candidat Windows 0.3 préparé réellement (741 fichiers), neuf tests Rust et tests C passés. Six cas matériels passent : ancien dossier, index valide, absent, tronqué, taille incorrecte, valide à nouveau. Contrôle groupé : 175,943 puis 170,519 ms, 264 Kio temporaires libérés avant allocation du jeu ; cache système chaud, coût à froid non qualifié. Retour : ancien préparateur/dossier ou retrait du champ optionnel file_index, index orphelin ignoré. Linux/macOS non reconstruits. |
| Lecture de fichiers | ReadFile renvoie aussi succès après une erreur de lecture ou de déplacement ; le chemin Vita direct confond read < 0 et fin de fichier | Erreurs réelles signalées, comptes partiels préservés et padding zéro à EOF conservé ; code diagnostic [io-error] | Tests de panne stdio/direct Vita simulé passés, 23 captures PC et six captures Vita identiques. Retour ciblé : anciens corps read_direct/ReadFile dans a074f96, sans annuler les autres corrections du fichier. Non installé comme nouveau VPK joueur ; pas une cause prouvée des textures testeurs. |

## Valeurs de contrôle conservées

Aucun réglage ci-dessous n'a été retouché dans cette campagne :

| Paramètre existant | Valeur de référence conservée |
|---|---|
| Échelle de sensibilité | `0.4 + 0.2 × cran` ; cran 3 → 1.0 ; extrêmes 0.4 et 2.4 |
| Seuil de shape | 0.10 par axe ; ce n'est pas une zone morte radiale |
| Courbe shape | `a=(abs(v)-0.10)/0.90`, puis `a*(0.35+0.65*a)*k`, saturation à 1 |
| Seuil axis | Au moins 1500/10000 après shape, donc filtre supplémentaire |
| Caméra TPS | Facteur 0.9 × sensibilité, Z/Rz |
| FPS via DirectInput | X facteur 1.0 ; Ry facteur 0.55, avec correctifs natifs antérieurs conservés |
| Corrections FPS natives précédentes | Y : `sqrt(0.5 × sensibilité)` au consommateur ; yaw : multiplication au consommateur ; gardes de contexte conservées |
| Menu / pointeur | Navigation et courbes héritées conservées |
| Compteurs et mémoire de construction | Build invité demandé 200 Mio ; allocation effective journalisée séparément ; aucun nouveau budget global mémoire activé ici |
| Archive audio | Prototype de cache 512 Kio **à confirmer** ; les 22,61 Mio résidents n'ont pas été supprimés |

Mesure sans changement de courbe : `stick_response_audit.py` balaie les 32768
codes positifs et leur opposé. Avec k=1, le premier axe DirectInput non nul est
à 35,34 % du débattement ; avec k=0.55, à 48,91 %. Avec k=2.16, l'axe sature à
21456/32767. Ces nombres décrivent la sortie shape→axis, **pas** toute la vitesse
finale de caméra : le GOG a ses propres conversions et consommateurs.
Résultats détaillés : `build/room-audit/console-files-20261028/stick-response.json`.
La faible asymétrie de normalisation SDL peut être amplifiée exactement au
seuil dur sur un code d'entrée ; aucune correction de sensation n'est activée.

## Essais diagnostiques non retenus comme validation de gameplay

- Une sélection forcée de Continue avec la sauvegarde de référence n'a pas
  donné un contexte narratif exploitable : vues sombres, pas d'inventaire
  qualifié. Ce n'est pas un défaut reproduit via le menu normal. Le lanceur
  diagnostique est désormais limité à Nouvelle partie / Options pour éviter
  de contourner une ligne désactivée. `menu_destination_test.py` vérifie ce
  contrat ; son comportement normal reste inactif sans SH4_TITLE_SELECT.
- Les comparaisons de captures non synchronisées et les échecs du script
  d'enregistrement local restent mentionnés dans le suivi, sans les présenter
  comme des crashs du jeu ni comme des preuves de correction.

Pour un retour demandé par Valentin : identifier d'abord le comportement précis,
revenir au réglage précédent si un commutateur existe, sinon préparer un
changement Git ciblé et un nouveau binaire. Comparer à l'artefact joueur avant
de le remettre sur la console. Toujours sauvegarder l'état joueur courant avant
le test ; les anciennes empreintes de cette page ne remplacent pas une nouvelle
sauvegarde si Valentin a rejoué entre-temps.

## Remise joueur du candidat — 28 octobre 2026

À la demande de Valentin, le candidat `159b735984b0567073c7e8f15603e6564a11e4a39b72367b3aba5fa92e88ebbd`
est maintenant installé et lancé pour essai manuel. Sauvegarde courante copiée
et vérifiée avant fermeture : `f39b8e485113b0907332d809e37b8b8c6b9ead4f3bc5e2010debffe835f9fa20`,
inchangée après installation ; camera.cfg conservé. Backup complet et ancien
binaire 2fb943… dans `build/room-audit/console-files-20261028/player-candidate-install/original`.
JSON conserve French ; index ajouté et validé sur console (239,632 ms).
Pas de env.txt, ctl.txt, key.txt, shotnow ou tour persistant. Combos non qualifiés
et trace de cycle de vie restent désactivés par défaut. Aucun essai automatique
en cours ; console rendue au joueur. Ne plus restaurer l'ancienne sauvegarde
après une nouvelle progression : refaire une copie de l'état courant.

### Aide des commandes — candidat en cours
Avant : Config. Boutons ouvrait le sous-menu de remappage PC. Candidat : sur
Vita, la même ligne garde la détection de confirmation et le son GOG, puis
ouvre une aide système localisée, issue des affectations vivantes des actions.
Les trois préréglages sont conservés ; aucun bouton ni sensibilité n'est modifié.
SH4_CONTROLS_HELP=0 restaure le sous-menu PC ; sur PC, défaut inchangé (désactivé).
Info/esquive apparaissent seulement si le prototype SH4_PAD_COMBOS=1 est activé,
ce dernier reste OFF par défaut. Une fermeture neutralise les entrées jusqu'au
relâchement comme les autres dialogues. Pas de travail supplémentaire en jeu
normal, hors la ligne sélectionnée. Validation matérielle encore en cours.

| Reprise des commandes | Aucun traitement du retour système | SH4_RESUME_GUARD=1 neutralise à la reprise et attend relâchement, OFF par défaut | Tests simulés passés ; veille physique/audio/GPU non qualifiés. Retrait de la variable rétablit l'observateur seul. Aucun changement anti-veille/fréquences. |


### Contexte d�options explicite
Avant : menu = g_options_seen non nul et g_frame <= g_options_seen + 3.
Apr�s : �tat ouvert tenu par le wrapper GOG 0x526be0, effac� � son retour final.
Vita : dernier menu=1 � 2322, premier menu=0 � 2323, sans reliquat apr�s sortie.
Tests retours 0/1/2/r�entr�e et ABI pass�s ; fen�tres et sensibilit� inchang�es.
Retour cibl� : ancienne variable/affectation nat_optframe et calcul menu dinput
au commit 97973eb. Ne pas annuler les autres corrections de ces deux fichiers.


## Écran de commandes intégré — 28 octobre 2026 (candidat en qualification)

Avant : Config. Boutons et aide système / prototype de remappage PC.
Après : Voir les commandes, une seule page noire avec silhouette Vita blanche,
libellés courts dans la police du jeu et vrais assets Kenney sombres (CC0).
Silhouette Tokyoship, CC BY 3.0, attribution embarquée dans le VPK.
Aucun changement de données préparées : nouveaux visuels dans app0:ui/controls.tga.
Aucun mode A/B/C, échange de sticks ou remappage dans cette page.
Rond revient aux options ; les anciennes sauvegardes et input.ini ne sont pas écrits.

L/R utilisent les flèches de page existantes ; pas de seconde paire.
Le rendu des glyphes de cette seule page est ajusté à partir de leurs quads,
avec UV et arguments originaux conservés. Le cache de glyphes et le vidage
normal du jeu sont conservés (un essai de vidage précoce était incorrect et rejeté).
Atlas 1024×512 BGRA, 2 Mio invités plus copie GPU, libéré à la sortie des options ;
aucun atlas chargé par ces fonctions en jeu normal, sauf ouverture du menu.

Raccourcis Vita : L+carré vers Info, R+rond vers Esquive avec visée.
Avant OFF par défaut ; candidat ON sur Vita, OFF sur PC, SH4_PAD_COMBOS=0
rétablit le mapping précédent. Masques physiques exhaustifs et consommateurs
GOG réels vérifiés ; qualification des animations/contextes de jeu distincte.
Zones mortes, sensibilité, inversion et échelles de caméra inchangées.

Preuves : build/ui-mockups/controls/. Premier essai Vita : entrée/sortie,
trois pages, sauvegarde/camera.cfg/input.ini/JSON identiques. Régression PC :
23 captures, zéro différence ; arrêt diagnostic attendu à 250 secondes.
Dernier rendu aligné et symboles + : contrôle matériel final encore en cours.
Retour : SH4_CONSOLE_BINDINGS=0 retrouve l’éditeur d’origine, ou réinstaller
le binaire joueur sauvegardé avant l’essai. Retirer l’atlas seul ne restaure pas
l’ancienne interface. Ne jamais restaurer une ancienne sauvegarde après progression.


### Lisibilité et finition de l’aide commandes — 28 octobre 2026
La page est désormais dessinée en 960×544, indépendamment de la résolution 3D.
Les glyphes originaux sont capturés sans modifier leurs arguments puis regroupés
par lots de 64 pour ce rendu ; le cache et le vidage du jeu restent inchangés.
Une cible GPU privée est libérée avec le menu. Le rendu normal reste identique
sur les 23 références PC contrôlées après ce changement.

Anciennes positions des textes du pied : Info/Esquive y=476, Retour y=479.
Nouvelles positions : 471 et 473, pictogrammes inchangés. L/R, auparavant fixes
x=95/827, suivent maintenant le décalage signé du jeu 0x109222c, multiplié par
960/512. Les flèches existantes sont conservées, sans animation indépendante.
Captures Vita à 1705/1710/1715 : défilement conjoint observé ; capture de l’aide
au repos : textes et combinaisons alignés. Les cinq langues ont été rendues
et inspectées sur PC à la résolution finale : pas de débordement ni chevauchement,
accents présents. Tests controls_help_test et console_ui_state_test réussis.
Preuves locales : build/ui-mockups/controls/pc-language-* et vita-aligned-idle.


### Fondus synchronisés des nouveaux visuels — 28 octobre 2026
Avant : les badges L/R et le schéma natif restaient à pleine opacité pendant
le fondu du jeu. Après : lecture seule de la valeur originale 0x137f98c,
0..128, conversion saturée en alpha 0..255. Aucun compteur, durée ou état du
jeu modifié. La page complète reçoit le voile noir après ses glyphes, traits
et pictogrammes ; L/R reçoivent l’alpha complémentaire. Le hook 0x526490
limite aussi les badges aux images où les flèches originales sont dessinées,
pour ne pas les faire apparaître seuls pendant la préparation du menu.
Retour : retirer ces deux applications du fondu et le garde page_frame ;
les valeurs originales du jeu n’ont jamais été écrites par ce correctif.
Tests ciblés : valeurs 0,8,..128 ; masquage hors rendu des flèches ; alpha
intermédiaire des deux badges ; restauration de l’état D3D. Captures PC
entrée et sortie inspectées : schéma, traits, glyphes et boutons ensemble,
plus de badges isolés lors du retour. Preuves : pc-fade-final. Qualification
Vita en cours dans vita-fade-final (sauvegarde originale vérifiée avant lancement).

Qualification finale : PC, captures successives du fondu complet entrée/sortie
inspectées. Vita : ouvertures/retours de l’aide et sortie des options observés,
sauvegarde et fichiers de réglages identiques. Des entrées physiques pendant
la série ont dévié le parcours : les captures Vita prévues pour le schéma
montrent surtout l’écran de luminosité et le titre ; ne pas les présenter
comme une validation visuelle complète de ce fondu sur matériel. Le code
de composition est partagé avec PC. Binaire final et atlas installés puis
vérifiés octet pour octet ; lancement normal sans env/ctl/entrées automatiques.
Sauvegarde originale sauvegardée et vérifiée. VPK local mis à jour :
build/ui-mockups/controls/sh4vita-controls.vpk ; aucune publication.


### Menu principal : suppression de Quitter
Sur Vita, avant la construction de la liste originale par 0x528a50, la dernière
entrée de 16 octets à 0x655178 est remplacée par le terminateur nul. Valeurs
originales {48,7,0,0} : libellé Quitter et type de destination. Les quatre
entrées précédentes, leurs indices et disponibilités restent inchangés.
La même table définit dessin et navigation : pas de ligne vide sélectionnable.
Le PC de référence conserve sa sortie ; SH4_CONSOLE_TITLE=1 permet de tester
le comportement Vita sur PC, =0 retrouve le menu original sur Vita.
Test PC réel : quatre lignes, quatre appuis bas reviennent en haut, haut
sélectionne Options et valider ouvre les options. Captures pc-title-console.
Qualification matérielle en cours ; sauvegarde originale vérifiée avant test.

Vita : capture du titre à quatre choix relue (1450/1690), entrée des options
observée dans le journal, sauvegarde et fichiers de réglages identiques à
la fin de l’essai. Les touches physiques ont aussi été utilisées pendant ce
parcours ; la boucle de navigation complète est qualifiée par le test PC.
Binaire et atlas joueur installés avec vérification des octets, sauvegarde
fraîche doublement lue et conservée ; configuration normale restaurée.
Preuves : vita-title-console/result.json, player-installed.json et captures.


### Nuit du29octobre : qualité et moteur, essais isolés

Les réglages joueur de référence restent640x362, TEXMAX256, seuil alpha256.
Tous les essais conservent une copie du binaireC5 et de la sauvegarde fraîche,
restaurés et relus après chaque série. Rapport : QUALITY-2026-10-29.md.

| Réglage/traitement | Avant | Essai | État/repli |
|---|---|---|---|
| Taille des mipmaps |256|512|Plus net dans certaines surfaces, coût jusqu’à16Mio ; défaut non modifié|
| Résolution3D |640x362|704x398,768x436,960x544|Plus net mais perte d’interpolation métro/hôpital ; défaut non modifié|
| Précision alpha |RGBA8 si dimensions≤256|SH4_ALPHA32_MAX512|Préserve cette précision en512,9–13Mio supplémentaires ; défaut256|
| Formats globaux |16bits selon texture|SH4_TEX32=1|Rejeté comme défaut : VRAM libre9,8Mio seulement dans l’hôpital testé|
| DXT1 CPU |RGBA8 temporaire puisRGB565|SH4_DXT16_NATIVE=1, tamponRGB565 direct|Identité différentielle10000cas et capturesPC/Vita ; étape CPU31%plus rapide ; défautOFF en qualification|
| Déclaration de sommets |Parcours à chaque dessin|Cache possédé par VShader|Aucun gain utile ; prototype entièrement retiré, seul échec calloc géré|
| Bornes d’indices |Deux parcours producteur/consommateur|SH4_PREP_BOUNDS=1, paire min/max jointe|Tests25000cas+1536records etPC identiques ; mesureVita en cours, défautOFF|
| Prélecture non apprise des archives |Taille demandée, chevauchement prévu4096|SH4_PREFETCH_GUARD=1, marge finale≤4096|Première paireVita réduit6pauses musicales ; répétition requise, défautOFF, mêmes plafonds mémoire|
| Banc de mesure |Deux captures par destination|SH4_AUDIT_NO_SHOTS=1 ou END_SHOT=1|Seulement diagnostic : évite de biaiser la cadence après readback ; défaut inchangé|

PREP_SCALAR et INTERP_SKIP_LARGE préexistants ont aussi été mesurés séparément
du profil normal : environ4,1ms gagnés ensemble dans le Passé, pas de30/60global
démontré. Sources/binaire des essais rejetés archivés ; aucune réduction de
qualité nouvelle introduite pour obtenir ces chiffres. Audio512Kio reste à
confirmer par le joueur, pas de libération générale des22Mio cette nuit.


## Nuit du29octobre — complément qualité, état vers05h43

| Élément | Avant | Candidat / retour arrière | Qualification |
|---|---|---|---|
| DXT1 logiciel | décodageRGBA8 puisRGB565 | SH4_DXT16_NATIVE=1 par défautVita ; =0 rétablit le chemin | 10000casCPU, imagesPC/Vita exactes, gain sur conversion uniquement |
| PrévisionBGM | marge finale0 | SH4_PREFETCH_GUARD=1 par défautVita, +4096au plus dans plafond existant ; =0 retour | deux pairesVita ordre inversé,6lectures bloquantes corrigées par intervalle, boucle lente restante |
| Déclaration / bornesindices | chemin ancien | prototypes retirés, ancien chemin conservé | aucun gain utile après tests exacts |
| Mipmaps / alpha | TEXMAX256 / ALPHA32_MAX256 |512/512 seulement essais ;256/256 retour | détail supérieur, coût mémoire mesuré, pas encore nouveau défautjoueur |
| StockageDXT | logiciel, anciennebibliothèqueGPUcorrompue | nouvellebibliothèqueisolée+HWDXTprésent ; supprimervariable pourOFF | chambre corrigée,7destinations+2cinématiques ; pas toute progression |
| Limite1024 |512essai |1024essai, nonretenu | mêmes niveaux chargés que512 dans7destinations, aucun détail ajouté |
| Résolution3D |640x362 |704x400/768x432/960x544 essais ;640retour |704/768perdent interpolation dans zones mesurées ; pas de promotionglobale |
| Budgetprélecture |2*1310720octets | mêmeplafond, réservationatomique |80000réservationsconcurrentes, aucun dépassement |
| PréfixeBGM | pasdecopiepréfixe |SH4_AUDIO_HEAD_CACHE=1 opt-in, défautOFF ; =0 retour |256plagesPC,172hits,8/8capturesPCexactes ; VitaOFF/ON àfaire |
| Niveau texturevide | allocation nonvérifiée | abandon uploadsiNULL | injectionéchec, noir etniveaupeuplé contrôlés |
| CroissancemipDXT | copiedesecoursmêmesiNULL | anciennesressourcesconservéessidoubleéchec |800casproductionASan/UBSan ; nonpreuvecrashsv83 |

Bibliothèque SDK et CG normale inchangées. Correctifisolé reproductible dansport/patches/vitagl-sh4-dxt-blocks.patch, àappliqueraprèscompiled-cache. Nouveau candidat complet distincteboot-quality-full, pasencoreinstalléjoueur. Sourceetvaleursavant/après dansdocs/QUALITY-2026-10-29.md. Sauvegardejoueurf39 etancienbinairec5 protégés. NePASconfondre512Kiod'anciencacheUV ettailletexturedimension512.


## 29 octobre, crash après la vidéo du train : accumulateur audio

Avant : calloc de 8192 octets à chaque callback normal de 1024 trames stéréo, sans contrôle de NULL, puis free. Dump du testeur : thread SDLAudioP2, lecture vldr [r5] avec r5=0 ; même défaut encore présent dans la source courante avant cette modification.

Après : scratch statique de 8192 octets pour l'unique périphérique audio SDL, remis à zéro à chaque bloc ; aucun malloc/free de cet accumulateur sur le chemin normal. Blocs plus grands : allocation conservée et vérifiée, silence borné si refus, sans avancer les tampons ni notifier une lecture inexistante. DSP48kHz, interpolation, gains, positions, saturation et notifications conservés. Repli de code : remettre le couple calloc/free de mix ; ce repli réintroduit le défaut connu, ne pas l'utiliser sans au minimum garder la vérification de NULL.

Vérifications :160comparaisons PCM/états/notifications identiques au mélangeur du paquet distribué,1000callbacks normaux sans allocation même sous échec forcé, échec oversized et reprise contrôlés ; création/lifetime audio existants passent ; PC23/23images exactes ; buildsPC/Vita OK. Non installé ni rejoué dans la scène exacte Vita. Candidat audio séparé aab38e3e... avec bibliothèqueCG normale ; les captures/endurance du candidat4092 ne qualifient pas ce changement postérieur. Ne pas distribuer comme candidat qualité combiné sans intégration/qualification.

Voir docs/TESTER-CRASH-2026-10-29.md. Pas de preuve que tous les crashs précédents ou une fuite globale sont résolus.


## 29 octobre, 19 h 01 — durée de vie des ressources vidéo

| Élément | Avant27d4 | Aprèsb8d0 | Preuve / repli |
|---|---|---|---|
| CPU sortie vidéo | pile1Mio etTIB4Kio retenus à chaque lecture | propriété hôte horslayoutCPU, libération après troisjoins | 2000cycles hôte,8films Vita ; repli27d4 conservé |
| CinqslotsYUV | retenus aprèsStop jusqu'au graphe suivant | rendus aprèsStop ; Pause conserve | 1000cycles hôte ;8capturesfilm+15Cynthia vues |
| Ordonnanceur laboratoire | un film par lancement | liste de films sans réentrance | 8retours dans unprocessusPC etVita, diagnostic inactif normalement |
| Qualité et cadence |512,640×362, lecteurperd certaines images | qualité identique, pertesFPS non corrigées | seul eboot change parmi511entréesVPK |

MesuresVita après retours1→8 : invité109667→109675Kio (+8), hôte13753→13785Kio (+32). RétentionCOM résiduelle non traitée. Binaireb8d0 installé normalement surVita1 ; VPK6ff9892a… copié/relu surI:, pas encore installé surVita2. Aucun préparateur ni donnée de jeu modifié. Repli : eboot27d4ff04… etVPKdb2f5a15… archivés. Rapport MOVIE-LIFETIME-2026-10-29.md.
