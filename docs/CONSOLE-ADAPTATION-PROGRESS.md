# Adaptation console — suivi d'implémentation

28 octobre 2026. Suit [le programme d'adaptation Vita](ADAPTATION-CONSOLE-VITA.md).
Valentin a autorisé l'ensemble du chantier, sans validation intermédiaire.
Chaque lot reste qualifié séparément ; ce document ne déclare pas le programme terminé.

Les valeurs d'origine, les valeurs candidates et les moyens de retour sont
consignés dans le [registre avant/après](CONSOLE-CHANGE-REGISTER.md).

## Lot 1 — écritures et sauvegardes

Complément : `CreateFileA` vérifie l'allocation de ses métadonnées **avant**
l'ouverture susceptible de tronquer un fichier. En cas de manque de mémoire,
il ne déréférence plus le résultat nul et renvoie une erreur propre sans ouvrir
le fichier. `file_open_errors_test.py` vérifie le contenu précédent, l'absence
d'ouverture après OOM, les permissions, le stockage plein et OPEN_ALWAYS.
Ce défaut de code est reproduit par injection ; il n'est pas identifié comme
la cause des crashs longue durée des testeurs.

### Frontière des fichiers

`kernel32.c` signale maintenant les erreurs d'écriture partielle, de déplacement
avant écriture, de vidage stdio et de fermeture. Le nombre d'octets écrits est
initialisé à zéro et reste exact en cas d'échec. Les handles invalides et les
écritures sur un fichier ouvert en lecture seule sont refusés. Les erreurs sont
capturées avant journalisation/nettoyage. Une fermeture ratée libère quand même
les ressources hôtes ; le handle ne reste pas réutilisable.

La complétion OVERLAPPED simulée conserve un statut d'erreur que
`GetOverlappedResult` ne transforme plus systématiquement en succès. Le backend
reste synchrone ; ce changement n'implémente pas une nouvelle file d'I/O asynchrones.
Les offsets d'écriture non représentables par le backend FILE sont refusés au
lieu de revenir au début du fichier. La synchronisation physique du stockage
n'est **pas** qualifiée : `FlushFileBuffers` vide encore les tampons C seulement.

Contrats consultés : [WriteFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile),
[FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers),
[CloseHandle](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-closehandle).

### Transport de sauvegarde en deux étapes

Analyse du GOG : `0x408030` écrit 694288 octets (`0xa9810`), avec signature
`0x7ffff005` puis MD5 des 694272 premiers octets. Il contrôle `WriteFile`, mais
ignore le résultat de `CloseHandle`. Son ouverture via `0x407df0` demande
CREATE_ALWAYS : la destination peut être tronquée avant le début de l'écriture.
Corriger les seuls retours de la couche Win32 ne protège donc pas la dernière
sauvegarde complète contre une panne.

Le wrapper `nat_savewrite` conserve les fonctions GOG de construction de chemin
et de MD5 (`0x407c10`, `0x415300`, `0x415bf0`, `0x415d30`). Le transport natif écrit
un fichier voisin `.sh4stage`, contrôle écriture/vidage/fermeture, puis remplace
la destination. Il ne supprime jamais d'abord l'ancienne sauvegarde. Sous Windows,
le remplacement utilise MoveFileEx ; sous Vita, rename. Le stage n'est ni `.new`,
ni `.bak`, ni `.prev`, et ne devient pas une priorité de chargement. Un stage
interrompu est ignoré puis réécrit au prochain essai. Un contexte d'appel inattendu
est refusé explicitement, sans repli vers l'écriture destructive.

**Activé par défaut après qualification ci-dessous.** `SH4_SAFE_SAVE=0` conserve
le transport GOG uniquement pour comparaison diagnostique. Aucun réglage ni nouvelle
préparation des données n'est nécessaire pour le joueur. Le format, les emplacements et les points de sauvegarde du
jeu sont conservés. Cette opération n'est pas une autosauvegarde, ni un système
de restauration de versions, ni une garantie contre une coupure électrique en
plein traitement des métadonnées de la carte.

Le réglage privé `camera.cfg` utilise le même transport. Cela supprime notamment
la suppression préalable de sa destination sous Windows. Les changements non
acceptés restent annulés ; une écriture refusée ne devient pas la nouvelle valeur
sauvegardée.

### Preuves et limites

- `python port/test/file_write_errors_test.py` : fonctions de production,
  fichier temporaire réel, erreurs injectées (écriture partielle avec/sans errno,
  stockage plein, seek/flush/close), comptage, invalidité, lecture seule,
  complétion, append et nettoyage.
- `python port/test/staged_file_test.py` : fichier synthétique de 694288 octets,
  cinq pannes open/write/flush/close/replace ; destination précédente intacte,
  première création, remplacement réussi et stage interrompu réessayé.
- `python port/test/save_write_test.py` : douze entrées synthétiques comparées à
  la vraie fonction GOG recompilée, registre/stack d'appel conservés, MD5 aussi
  vérifié indépendamment par Python ; vrais appelants GOG de modification des
  options et des emplacements 0/15/29 comparés également ; refus de
  contexte/adresses et panne stockage.
  Le test lit uniquement l'exécutable GOG local, jamais la sauvegarde du joueur.
- `options_test.c` : bornes, annulation, acceptation, rechargement et cinq langues
  passent après raccordement du nouveau transport de `camera.cfg`.
- Builds PC et Vita (`SH4_GUEST_MB=200`, VGL RAM 32, profil désactivé) réussis.
- Régression PC de la frontière de fichiers : prison `4:11`, Past `8:24`, métro
  `2:14`, six captures identiques à la référence précédente. Ne qualifie pas tous
  les appels de fichiers de toutes les scènes.
- Sonde autonome sur la vraie Vita : huit créations/remplacements de fichiers
  synthétiques, avec relecture exacte. Aucun fichier `sh4.sav` n'est ouvert par
  cette sonde. Binaire et sauvegarde d'origine restaurés et vérifiés ensuite.
- Démarrages PC et Vita réels avec `SH4_SAFE_SAVE=1` : deux écritures natives
  chacun, fichier rechargé par le lecteur GOG ; taille et MD5 de la sauvegarde
  créée sur Vita vérifiés après transfert. Binaire, configuration et sauvegarde
  de référence de la Vita restaurés, puis lancement normal effectué.
- Les tentatives de navigation PC automatisée n'ont pas atteint les options :
  captures relues, chemins Load/New Game observés. Elles ne qualifient donc pas
  l'enregistrement depuis ce menu. Les vrais appelants sont couverts par le test
  différentiel ci-dessus, pas encore par ce parcours utilisateur.

Complément de qualification : `SH4_TITLE_SELECT=1200:3` permet désormais d'entrer
dans les options par le vrai dispatcher/fondu du titre, après mise à jour normale
du menu. Le test a modifié le volume BGM puis accepté le dialogue de sauvegarde,
sur PC et sur Vita. Une écriture native réelle a été observée. Sur Vita, le fichier
relu depuis le stockage possède une taille de 694288 octets et un MD5 valide ;
les données des trente emplacements (`0x660..0xa9800`) sont identiques à la copie
précédente. Le bloc commun de paramètres et le digest changent selon le chemin GOG.
Sauvegarde et configuration originales restaurées ensuite. Ce résultat complète
les premiers essais de navigation non concluants ; il ne les rend pas validants.

Contrôle cumulé : 23/23 captures PC de `ref_head` identiques, puis six captures Vita
identiques sur prison/Past/métro avec les options expérimentales de ce lot activées
et un délai d'arrivée identique à la référence (180 images). La première tentative
à 120 images n'était pas alignée et n'est pas utilisée pour conclure. Planche Vita
relue : personnages, ennemis, décors et sous-titres visibles dans les états capturés.
Cela ne qualifie pas toute la campagne ni les crashs de longue durée.

Preuves locales ignorées : `build/room-audit/console-files-20261028/`.
La sauvegarde de Valentin a été copiée deux fois et vérifiée avant toute intervention
console : SHA256 `f39b8e485113b0907332d809e37b8b8c6b9ead4f3bc5e2010debffe835f9fa20`.

Restent dans le lot : traitement utilisateur des erreurs, éventuel échec de création
du dossier, qualification interruption/reprise et contrôle des options réelles.
L'intégrité des douze fichiers synthétiques et huit fichiers Vita n'est pas la
preuve d'absence de régression pour tous les états narratifs.

## Lot 1 — messages visibles

Les deux appels directs GOG à `MessageBoxA` identifiés (`0x414b6d`, `0x414cb4`)
sont des erreurs de démarrage avec un unique bouton OK, sans demande de confirmation.
La Vita affiche désormais une boîte système dans ces cas, après synchronisation
du rendu. Le résultat est remonté à USER32 ; une création, lecture du résultat ou
terminaison ratée ne produit pas de réponse OK implicite. Les ensembles de boutons
non implémentés sont refusés explicitement. Ce n'est pas une qualification de
messages modaux à n'importe quel point du rendu ou pendant une FMV.

Les chaînes ANSI Western du GOG sont converties en UTF-8 pour la boîte Vita et
SDL sur PC. `message_box_test.py` compare les 255 octets non nuls CP1252 à la
conversion Unicode indépendante de Python, avec 801 tailles de tampon, sans
découper un caractère UTF-8. Il exerce les fonctions de production avec un SDK
simulé : OK, échec d'ouverture, échec de résultat/terminaison, abandon, bouton
non pris en charge et fermeture du diagnostic.

Sonde réelle `SH4_MESSAGE_TEST=1` : boîte ouverte puis fermée automatiquement,
résultat lu et terminaison réussis ; l'ID bouton nul observé n'est pas transformé
en OK. Le jeu reprend ensuite son démarrage et dépasse 1250 présentations sans
FATAL relevé. Le bouton physique OK n'a pas été pressé par cette sonde. Sauvegarde,
binaire de référence et configuration normale restaurés après l'essai. Preuves
dans `console-files-20261028/dialog-vita/`. Ce diagnostic n'est pas actif dans le
paquet joueur et n'ajoute pas de tâche périodique au jeu normal.

## Lot 2 — commandes : infrastructure et prototype

La traduction des boutons SDL vers les douze boutons DirectInput du jeu est
isolée dans `pad_mapping.h`, sans nouvelle allocation. Les ensembles possibles
de boutons physiques sont comparés exhaustivement au contrat précédent : mêmes
résultats par défaut, notamment pour L/R dans les options, Start, Select,
confirmation et inventaire. Le diagnostic utilise cet échantillon au lieu de
relire tous les boutons une seconde fois pour afficher leur état.

**Prototype `SH4_PAD_COMBOS=1`, désactivé par défaut :** L + carré produit les
infos d'objet (DI 9), R + rond produit l'esquive (DI 10), en conservant R pour la
visée. Les actions carte/caméra arrière et course/annulation des boutons consommés
ne sont pas émises simultanément. La combinaison ambiguë des quatre boutons est
neutralisée. Ces raccourcis ne remplacent pas L/R dans les options et n'exigent
pas le tactile arrière. Leur pertinence en inventaire/combat, leur confort et les
indications à afficher restent à qualifier en jeu ; ce n'est pas encore le
préréglage publié.

L'hystérésis qui reconnaît un écran à pointeur compte désormais une fois par
image échantillonnée plutôt qu'à chaque appel de GetDeviceState. Cent relectures
dans la même image ne doivent pas basculer entre caméra et curseur. Les seuils
restent 6/1, avec saturation à 10 ; le détecteur de position reste provisoire.
Cela ne remplace pas encore l'ensemble de l'entrée par un snapshot logique unique.

`pad_mapping_test.py` couvre l'équivalence exhaustive par défaut, les combinaisons,
les répétitions dans une image et le débordement du compteur. Tests hôte de
fenêtre avec le véritable consommateur GOG, inversion brusque des axes, sensibilité
FPS et yaw : tous passent. Builds PC/Vita réussis. Les nouvelles combinaisons
physiques et les interactions au curseur restent à qualifier sur la console.

## Lots suivants — état courant

### Veille/reprise : observer avant de changer la politique

`SH4_LIFECYCLE_TRACE=1` active un observateur des callbacks d'alimentation.
Il utilise le thread principal existant, une file fixe de huit événements et les
horloges processus/système. Le callback ne fait ni I/O, ni rendu, ni modification
de la mémoire du jeu ; les messages sont écrits ensuite depuis la boucle normale.
Les débordements sont comptés, les inscriptions ratées nettoyées et le dispatch
explicite limité à une fois par présentation. Désactivé, il n'appelle pas ces
services noyau et ne crée pas de thread. La politique anti-veille actuelle est
conservée pendant la qualification.

Tests simulés : désactivation, création/inscription ratées, nettoyage, callback
sans journalisation, file pleine et appels répétés dans la même image. Le build
Vita réussit. Premier événement matériel observé : `flags=00001000` après
inscription réussie. Aucune suspension/reprise physique n'est encore qualifiée.

Le [timer Vita de SDL 2.32.8](https://github.com/libsdl-org/SDL/blob/release-2.32.8/src/timer/vita/SDL_systimer.c)
utilise l'horloge du processus pour ses compteurs et `sceKernelDelayThreadCB`
pour les attentes. Le jeu corrige déjà une échéance dépassée dans sa boucle
`0x414a50` : ajouter aveuglément un « rattrapage » de frames serait incorrect.
Il faut mesurer les deux horloges et le comportement audio/vidéo à la reprise.
L'examen de cette version SDL ne prouve pas à lui seul tous les détails de
l'archive installée et de ses éventuels correctifs.

Commandes physiques complètes et aides, courbes des sticks, veille/reprise, lisibilité,
ressources/préparation, chaîne native/rendu/interpolation : autorisés, non considérés
comme livrés par les changements de sauvegarde. Les essais audio antérieurs restent
à confirmer, sans retrait des 22,61 Mio résidents. Les licences restent reportées.

### Erreurs visibles et retour des dialogues

Les options GOG ignorent le retour de leur écriture : le transport protégé ne
suffisait donc pas à prévenir le joueur d'un échec. Le wrapper du dispatcher
conserve désormais la cause et un compteur d'échecs ; le menu affiche une boîte
système dans la langue du jeu, une seule fois par tentative. Le cas « carte
pleine » est distingué. Un échec ne valide pas le réglage privé de caméra, même
si le menu termine son dialogue à l'image suivante. Les deux fichiers ne
constituent pas une transaction unique ; des options peuvent être appliquées
en RAM malgré un échec de persistance, signalé au joueur.

Après une boîte système, chaque périphérique attend indépendamment son retour
au repos. Une manette encore maintenue ne redevient pas active parce que le
clavier est neutre. Les axes sont centrés et les POV valent 0xffffffff pendant
cette attente. Les entrées caméra brutes sont également neutralisées. En jeu
normal, cette garde ne relit pas les boutons SDL.

Qualification matérielle : panne ENOSPC injectée **avant** toute ouverture du
stage dans les vraies options, dialogue français init/result/term réussis,
reprise du rendu jusqu'à 2623 présentations et écran-titre inspecté sans anomalie.
Le fichier de sauvegarde reste identique octet pour octet à l'original f39b8e…
(694288 octets, MD5 valide). Le script attendait inutilement 2800 présentations
et a atteint sa limite temporelle ; la qualification ci-dessus utilise les
preuves enregistrées, pas ce critère non atteint. Un premier enregistrement
local a échoué sur l'encodage du journal ; ce n'était pas un crash Vita.
Le bouton OK physique n'est toujours pas qualifié : fermeture automatique de
la boîte diagnostic, qui renvoie correctement « aucune réponse », pas un faux OK.

Tests hôte : erreurs internes/externes, ABI, fin de dialogue différée, nouvelle
tentative, échec de camera.cfg avec errno préservé avant journalisation, langues,
relâchements indépendants et limites des buffers. Le manque de mémoire lors de
la création du libellé caméra déclenche une erreur explicite au lieu d'écrire
à l'adresse invitée zéro ; cela ne résout pas à lui seul l'épuisement mémoire.

### Échantillon physique cohérent

Axes, croix et actions utilisent un échantillon physique commun, conservé dans
chaque périphérique pour une même image. Les injections diagnostiques restent
appliquées ensuite. Le cache est invalidé à la sortie d'une boîte système.
Les tests couvrent les lectures répétées, deux périphériques, le débordement du
compteur et l'invalidation. Cela ne remplace pas encore tous les lecteurs du
jeu par une API d'actions et ne justifie aucune promesse de gain de FPS.

Le prototype de combinaisons conserve les partenaires consommés jusqu'au
relâchement des deux touches, quel que soit l'ordre. Une combinaison ambiguë
reste neutralisée ; un passage aux options ne transforme pas un relâchement en
changement de page. Les écrans à pointeur ne démarrent pas de nouvelle combinaison.
Ces comportements sont testés sur les masques d'entrée ; l'action réelle Info /
Esquive reste à qualifier en inventaire/combat. SH4_PAD_COMBOS reste désactivé
par défaut.

## Dernière comparaison et protection DirectInput

Le candidat avec échantillon physique partagé a terminé les trois pièces Vita
(prison 4:11, passé 8:24, métro 2:14), en mouvement et balayage : six TGA
décompressées identiques à la référence. Le parcours PC retrouve 23/23 BMP
identiques, sans FATAL. Le transport protégé était actif par défaut, sans
SH4_SAFE_SAVE. Console, binaire et sauvegarde d'origine restaurés ensuite.
Cette sélection ne couvre pas tous les états narratifs ni les nouvelles actions.

Complément de robustesse : quatre points d'entrée DirectInput refusent maintenant
proprement une allocation ratée (objets hôtes et descriptions invitées), avec
E_OUTOFMEMORY et sortie objet nulle. Les tests injectés vérifient l'absence de
callback vers une structure nulle, de modification de l'adresse invitée zéro,
et le maintien de l'ABI dans les cas réussis. Cela ne démontre pas que les crashs
longue durée des testeurs avaient cette cause.

### Commandes : lien jusqu'aux consommateurs GOG

Le test pad_actions_test.py emploie les véritables fonctions GOG 0x406c50,
0x4f84a0 et 0x554250 avec les tables du binaire vérifié. Pour les trois
préréglages du jeu, L+carré active l'action Info 4 sans Carte/Annuler ; R+rond
active l'action Esquive 12 en conservant Visée 10, sans son partenaire Annuler.
La levée de R avant rond reste neutralisée. Le verrouillage narratif du jeu est
neutralisé dans cette seule unité de test ; elle ne prouve donc pas les animations.

Le build de test possède un injecteur SH4_PAD_RAW avant la traduction des boutons,
compilé uniquement avec SH4_TEST_OPTIMIZATIONS. Ce chemin évite de qualifier une
combinaison en injectant directement le résultat DI attendu. En mode joueur,
sans variable, aucune combinaison n'est injectée ; le build de production sans
l'option ignore complètement cette instrumentation.

Sur Vita, une véritable partie initialisée via le parcours habituel puis une
pièce 4:11 : 1/1 cas fini en 236,7 s. Les masques bruts 0x204 puis 0x402 donnent
respectivement DI9 et DI5+10. Captures inspectées : l'information répond
« Vous ne tenez rien », se ferme et rend la main normalement. Henry n'étant pas
armé, aucune animation d'esquive n'est attestée par ces captures. Les combinaisons
restent OFF par défaut jusqu'à qualification avec une arme et un objet tenu.
Binaire, sauvegarde et configuration de référence restaurés après cette série.

### Frontière de lecture : qualification ciblée terminée

ReadFile ne distinguait pas une erreur de stockage d'une fin de fichier. Le
candidat contrôle les déplacements et les erreurs stdio / read() direct Vita,
préserve le nombre réellement transféré, les codes d'erreur et la complétion
OVERLAPPED simulée. EINTR est réessayé sur le chemin direct. Le remplissage zéro
au-delà des octets disponibles est maintenu pour le contrat de padding du lecteur
de ressources GOG. Le backend reste synchrone : ce n'est pas une implémentation
complète des règles Windows asynchrones ni de tous les comportements EOF.

Référence : [contrat ReadFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile).
Les tests injectés stdio et chemin direct Vita simulé couvrent EOF, octets
partiels suivis d'erreur, erreur sans errno, EINTR, handle, bornes, offset haut,
seek, prélecture et événement de complétion. Les tests d'écriture précédents
passent aussi. Builds PC/Vita réussis ; 23/23 captures PC et six captures Vita
décompressées identiques, sans [io-error] observée dans les trois pièces. Console,
sauvegarde et configuration de référence restaurées. Ce défaut
n'est pas attribué aux textures des testeurs sans reproduction correspondante.

## Contrôles TPS : expérience de routage non retenue

Le test pad_axis_contract_test.py suit les fonctions GOG 0x406f50, 0x406c50,
0x4067c0, 0x407480 et 0x553d20 : cette chaîne lit les slots Ry/Rx et produit une
paire quantifiée après le seuil ±300. Z/Rz seuls n'atteignent pas cette conversion.
Le miroir testé sur Vita fait effectivement apparaître ±1 dans la seconde paire,
au lieu de zéro, sans rotation du joueur. Cependant les six captures comparées
sans L puis les six avec L restent identiques dans la pièce 4:11. Le recentrage
par L fonctionne dans les deux versions. La transmission est prouvée, pas un
bénéfice caméra visible ni un gain de performance. Cela ne décrit pas tous les
contextes caméra du jeu. L'essai de miroir a été **retiré du runtime** : mapping
précédent rétabli, courbes inchangées. Les preuves restent sous tps-axis-routing-vita
et tps-axis-with-l-vita ; les deux séries ont restauré la console ensuite.

## Transferts de données : candidat en qualification

Le nouveau préparateur candidat 0.3 produit files.idx (chemins et tailles,
compteur/total et FNV-1a du manifeste) et l'annonce par un champ optionnel dans
sh4vita.json, toujours au format 1. La sauvegarde, camera.cfg, les entrées de
test et les caches ne sont ni copiés ni inventoriés. Une préparation réelle
complète a produit 741 entrées / 3632595346 octets et un index de 18564 octets.
Le vérificateur C du port valide intégralement cet index Rust sur PC, puis les
tailles de ces 741 fichiers. Cela vérifie la correspondance de métadonnées,
pas les octets du contenu des fichiers.

Le candidat Vita vérifie un index seulement s'il est annoncé dans le JSON.
Les anciennes données sans ce champ restent utilisables ; un index orphelin
n'impose pas une nouvelle préparation. En cas d'index absent/tronqué/invalide ou
de fichier manquant/de taille incorrecte, le démarrage affiche une erreur lisible
avant allocation invitée et avant le moteur. Aucun fichier de jeu n'est réparé
ou supprimé. Aucune vérification périodique n'est ajoutée en cours de jeu.
Les six cas matériels passent avec sauvegarde identique : ancien JSON sans
index, index valide, index annoncé absent, tronqué, taille incorrecte puis index
valide à nouveau. Le premier prototype faisait un stat par fichier : 14,039 s
au premier passage contre 0,789 s au second. Il a été remplacé par une lecture
groupée des métadonnées des quatre répertoires. Ce backend prend 175,943 puis
170,519 ms sur les deux passages valides, avec 264 Kio temporaires libérés avant
allocation de la mémoire invitée (166 Mio effectivement obtenus). Le cache
système était déjà chaud : ce ne sont pas des mesures froid/froid comparables.
Aucune charge périodique en jeu, aucune réduction de qualité.

Le CLI Windows candidat 0.3 compile ; les neuf tests Rust, dont ceux avec
l'exécutable GOG, passent. Les tests C couvrent l'interopérabilité du manifeste
réel, les chemins, troncatures, limites, erreurs de répertoire et d'allocation,
et la libération du cache. Linux/macOS restent à reconstruire avant distribution.
La console a retrouvé ses métadonnées originales et son ancien binaire après
la série. Ce contrôle prévient des transferts incomplets ; il ne détecte pas
une corruption de contenu de taille identique et ne prouve pas une cause des
textures ou crashs des testeurs.

La copie complète issue du CLI candidat a également été vérifiée octet par
octet via SHA-256 : 741/741 fichiers identiques à l’installation source
(rapport prep-index-copy-hashes.json). L’ancien binaire joueur démarre avec
le nouveau JSON/index et conserve la sauvegarde identique : compatibilité
dans ce sens vérifiée sur Vita.

Le tour matériel final data-index-rooms-vita termine les trois pièces 4:11,
8:24 et 2:14 : six TGAs décompressées strictement identiques à la référence,
sans FATAL/FAILED/memory exhausted. Index actif au démarrage : 172,496 ms.
Binaire, sauvegarde, JSON, absence originale de files.idx, camera.cfg et
configuration joueur restaurés et jeu relancé. Aucun gain de FPS attribué à ce lot.

## Entrées à la reprise — prototype B1

Ajout de SH4_RESUME_GUARD=1, OFF par défaut : le callback existant ne fait
qu'enfiler l'événement. Le thread principal neutralise les entrées lors des
événements SYSTEM_RESUME / APP_RESUME, puis chaque périphérique attend son
propre relâchement. Les notifications batterie/charge ne déclenchent rien.
Tests simulés : modes inactifs, enregistrement/échec, file bornée, reprise,
notification secteur et absence d'action dans le callback passent. Cela ne
qualifie ni la veille physique, ni la reprise audio/GPU ; anti-veille et
fréquences conservées. Aucune libération de ressources arbitraire à la reprise.

## Aide des commandes — qualification encore ouverte

Le premier affichage long et sa variante compacte arrivent bien depuis la
page Commandes, mais le résultat du dialogue est 0x80100001 (et non succès).
Le jeu reprend et la sauvegarde reste identique. Le premier harness ne
contrôlait que le retour de GetResult, pas le champ result : son résultat
positif ne constitue donc PAS une validation du dialogue. Le second harness
contrôle les deux et rejette correctement cette variante. Une sonde de texte
court dans le même contexte départage longueur du texte et contexte d'appel.
Ne pas livrer l'aide comme qualifiée sur la seule preuve de reprise du jeu.


### Aide : variante pagin�e retenue apr�s comparaison

La sonde tr�s courte passe dans le m�me contexte o� les textes longs et compacts
�chouent. Ce contraste atteste une diff�rence li�e au contenu ; la limite exacte
du syst�me n'est pas �tablie. Le candidat retenu r�partit les commandes en trois
pages courtes. Sur Vita, controls-help-pages-vita : d�marrage puis trois dialogues,
quatre r�sultats SDK result=0/term=0, retour aux options, plus de 2250 pr�sentations,
sauvegarde identique. Les quatre captures du menu et de sa reprise sont conserv�es ;
elles ne capturent pas le compositeur syst�me et ne prouvent pas la lisibilit� des
pages. L'auto-fermeture diagnostique n'est pas un test du bouton OK physique.

Les cinq langues, la s�lection seulement apr�s confirmation, les pr�r�glages
vivants, le contrat CPU et la neutralisation apr�s fermeture passent c�t� h�te.
Le PC garde son sous-menu original par d�faut. Le contr�le est activ� sur Vita ;
SH4_CONTROLS_HELP=0 retrouve le sous-menu pr�c�dent. Les nouvelles touches
exp�rimentales restent OFF ; leur animation arm�e n'est toujours pas qualifi�e.
Les pages suivent l'ordre d�placement/action/course, carte/objets/cam�ra/vis�e,
pause/album. Ce premier �cran syst�me n'est pas encore un sch�ma de pad dans la
police du jeu ; cet objectif du programme reste ouvert.


### Contexte options explicite � qualification sur Vita

Le wrapper de la v�ritable fonction d�options 0x526be0 maintient maintenant
un �tat ouvert pendant toute sa dur�e, y compris sa fermeture anim�e, puis
l�efface lors de son r�sultat de sortie. Le pad consulte cet �tat au lieu de
l�ancienne �ch�ance g_options_seen + 3 images. Aucun seuil/axe ni mapping de
jeu n�est chang�. controls-context-vita montre menu=1 � 2322 et menu=0 d�s
2323 apr�s le retour du menu, sans �chantillon menu ult�rieur ; aide compl�te,
quatre r�sultats syst�me normaux, sauvegarde identique. Les tests host couvrent
les retours 0/1/2 et la r�entr�e ; fen�tres, inversion, sensibilit�/yaw passent.
Builds PC/Vita r�ussis. La capture labelled-vita/after-help.png a �t� relue :
libell� � Commandes PS Vita � et aide de ligne corrects dans la police du jeu.


Le tour console-adaptation-vita finit ensuite 3/3 (4:11, 8:24, 2:14) : six
captures d�compress�es identiques � la r�f�rence, sauvegarde et binaire joueur
restaur�s. Les tests d�archives audio c�t� h�te repassent : 355 entr�es compar�es
avec et sans livraison native, 20000 copies g�n�r�es (repli, recouvrement et sens
inverse inclus). La s�rie mat�rielle audio-headers-vita est lanc�e s�par�ment
avec transfert/en-t�tes activ�s ; elle ne retire pas les 22,61 Mio r�sidents.

L�aide distingue maintenant Courir et Annuler : ils peuvent utiliser des touches
diff�rentes selon le pr�r�glage. La derni�re page montre explicitement Valider
et Annuler � partir des actions de menu vivantes. Les cinq langues et un autre
pr�r�glage sont v�rifi�s par le test h�te. Les raccourcis exp�rimentaux, lorsqu�ils
sont demand�s, utilisent une quatri�me page afin de ne pas allonger la troisi�me.
