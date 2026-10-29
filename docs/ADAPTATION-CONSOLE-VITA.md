# Adapter SH4 à une expérience console Vita

Audit et propositions du 28 octobre 2026, à la demande de Valentin. **Document de décision : aucune des nouvelles propositions ci-dessous n'est implémentée par cet audit.** Les correctifs et optimisations déjà présents restent la référence ; la console et le paquet testeur ne sont pas modifiés. Le chantier des licences est reporté à sa demande.

## Direction recommandée

Faire fonctionner le GOG sur ARM est déjà acquis dans le port jouable. La prochaine étape est de faire en sorte que l'utilisateur n'ait plus à composer avec une interface, des entrées et des comportements système hérités du PC : toutes les actions au pad, menus lisibles, erreurs compréhensibles, sauvegardes sûres, veille fiable, chargements et cadence prévisibles.

Cela complète la [reconstruction native](ARCHITECTURE-NATIVE.md) et la [feuille de route moteur](ROADMAP-PORTAGE-NATIF.md). Une nouvelle écriture de chaque fonction n'est pas nécessaire pour changer un libellé ou gérer une reprise. Les gros remplacements doivent cibler les contrats de ressources et les traitements réellement coûteux. Aucun changement de langage n'est requis : C compilé pour ARM peut convenir.

Trois niveaux sont distingués : **constat de code**, **preuve déjà mesurée**, **proposition à qualifier**. Les anciens STATUS et GAME contiennent des notes historiques : ils ne remplacent pas les rapports des essais récents. Une fonction de compatibilité partielle n'est un défaut à traiter que si un appel pertinent l'utilise et qu'un comportement attendu manque.

## A. Commandes complètes et cohérentes — priorité haute

### A1. Rendre toutes les actions accessibles

Constat : dans `port/src/dx/dinput.c`, la table `btn[12]` laisse les indices 9 et 10 à `-1`, correspondant aux informations d'objet et à l'esquive. Les boutons 6/7 sont produits par le déplacement latéral FPS ou L/R dans les menus. Le tactile arrière a été délibérément écarté après des mouvements involontaires.

Proposition : établir une carte de **toutes** les actions et de leurs contextes : exploration TPS/FPS, combat, inventaire, carte, album, menus, code/téléphone et cinématiques. Prévoir une commande physique pour les deux actions manquantes. Examiner d'abord une action contextuelle ou une combinaison dont les boutons n'ont pas d'effet conflictuel dans cet état ; ne pas choisir au hasard une combinaison L+R ou Select+croix qui ferait aussi viser, courir ou ouvrir l'album. Le tactile avant peut offrir un raccourci secondaire, jamais le seul accès à une action indispensable.

Livrable : schéma Vita visible depuis les options, puis un préréglage complet. Qualification : chaque action effectivement réalisée, maintien/appui distingués, absence de déclenchement double, combat et inventaire compris. Aucun changement arbitraire de la difficulté ou des règles de l'esquive.

### A2. Construire une vraie couche d'actions et de contexte

Constat : `GetDeviceState` combine lecture SDL, conversion DirectInput, modes du jeu, diagnostics et injections. Le mode curseur repose sur des coordonnées à `0x5ff27c/0x5ff280` et une hystérésis comptée en appels. Les options sont reconnues par `g_options_seen` et un délai en images. Ce sont des adaptateurs fonctionnels mais fragiles comme frontière permanente.

Proposition : échantillonner le pad une fois par image logique, produire un état immuable, puis traduire des actions selon un contexte explicite. Garder le pont DirectInput pour les consommateurs non migrés. Définir priorité et neutralisation lors d'un passage menu/jeu, FPS/TPS, observation et cinématique. Remplacer les heuristiques par les états réels après identification de leurs contrats.

Bénéfice attendu : remappage cohérent et suppression d'incohérences entre lecteurs d'entrées ; aucun gain de FPS présumé. Tests : inversion immédiate, diagonales, menu fermé alors qu'une touche reste tenue, interruption de cinématique et changement de caméra sans mouvement résiduel.

### A3. Zones mortes et sensibilité qui correspondent réellement aux réglages

Constat : `shape()` traite séparément chaque axe malgré le commentaire « radial ». `axis()` applique encore un seuil minimal de 15 %, alors que `shape()` commence à 10 %. Ces deux transformations peuvent créer une zone inactive plus grande que le réglage annoncé. La sensibilité TPS est appliquée avant saturation des axes ; l'effet peut donc plafonner. La correction FPS récente agit aussi au niveau des valeurs consommées par `0x52d710`/`0x52e0b0` et a déjà été mesurée. Le correctif d'inversion franche de direction existe : le préserver.

Proposition : formaliser une courbe à deux dimensions, séparer mouvement et caméra, puis appliquer la vitesse au bon consommateur plutôt que simplement écrêter l'entrée. Conserver le comportement actuel comme référence. Proposer zone morte et inversion explicites, sans exposer dix réglages avant d'avoir de bons défauts. Le cran 0 ne signifie actuellement pas caméra désactivée : l'échelle vaut 0,4 ; le libellé doit être cohérent.

Validation : vitesse mesurée aux petits, moyens et grands débattements, crans 0/3/10, FPS/TPS et observation ; contrôle du bruit au repos sur plusieurs pads. Une zone morte radiale ne doit pas modifier involontairement la vitesse diagonale ou les déplacements traditionnels.

### A4. Prompts Vita et configuration utilisable

Proposition : remplacer les indications clavier/manette PC pertinentes par croix, rond, carré, triangle, L/R, Start/Select. Adapter l'écran « Config. Boutons » au préréglage Vita réel et aux actions disponibles ; ne pas conserver un écran qui laisserait régler des boutons inexistants. Vérifier confirmation/annulation selon le réglage console ou un choix explicite, avec cohérence entre menus originaux et boîtes système.

Les symboles doivent correspondre à la commande réellement active et à la langue, pas être collés dans une texture supposée universelle. Préserver la police et le style du jeu ; les chaînes utilisent un codage de glyphes spécifique, déjà traité pour le slider.

## B. Veille, reprise et vie sur batterie — priorité haute

### B1. Gérer un cycle de vie Vita explicite

Constat : `watchdog_thread` dans `src/vita/main.c` appelle toutes les cinq secondes les power ticks désactivant veille automatique, extinction OLED et diminution de luminosité. `plat_pump_events()` retourne toujours zéro côté Vita. Aucun traitement explicite de suspension/reprise n'a été retrouvé dans nos sources. Cela ne prouve pas que toute reprise échoue, mais le port ne possède pas encore de contrat de reprise qualifié.

Proposition : identifier les événements réellement délivrés par les services système/SDL de notre build ; sur perte d'activité ou suspension, neutraliser les entrées et arrêter la progression à une frontière sûre, coordonner audio, vidéo, lectures et files de rendu. À la reprise, remettre les horloges de présentation à jour, rétablir les services nécessaires et attendre une nouvelle entrée avant de continuer si nécessaire. Ne pas libérer à l'aveugle toutes les ressources GPU lors d'une suspension.

Cas de qualification : bouton Power en jeu, menu et FMV, retour Home/application, plusieurs cycles, reprise après longue pause, écouteurs et batterie faible. Ni son jouant en avance, ni grande avance de temps, ni touche maintenue fantôme. Le jeu à 30 Hz n'autorise pas une reprise qui rattraperait toutes les images absentes.

### B2. Remplacer l'interdiction permanente de veille par une politique adaptée

Proposition : garder les protections nécessaires pendant un chargement ou une écriture courte, puis autoriser l'économie d'énergie dans les états appropriés après qualification de B1. Réduire le travail en pause et sur écran statique. Mesurer avant de proposer des profils de fréquences : les valeurs actuelles 444/222/222/166 sont déjà fixées dans `main`, les « augmenter » n'est pas une solution nouvelle.

Ne pas diminuer les fréquences pendant une tâche audio/vidéo sans avoir vérifié son budget. Pas d'installation d'un plugin noyau, pas de modification de la configuration système pour ce chantier.

## C. Sauvegardes et erreurs traitées comme sur console — priorité haute

### C1. Rendre les erreurs d'écriture réelles et visibles

Constat : `K(WriteFile)` renvoie le succès même si `fwrite` écrit moins d'octets que demandé. `FlushFileBuffers` ignore le résultat de `fflush` et renvoie le succès même sans fichier valide. `CloseHandle` ignore le résultat de fermeture. Un manque d'espace ou une erreur de stockage peut ainsi être mal signalé au jeu. `camera_save` utilise un fichier temporaire mais le contrat de durabilité et les erreurs restent à qualifier.

Proposition : reproduire les contrats Win32 d'erreur à la frontière, avec compte exact et erreur utilisable par l'appelant ; tracer ensuite le chemin original de sauvegarde avant toute modification de son format. Vérifier ce que le jeu fait réellement en cas de refus. Si une couche transactionnelle dédiée est nécessaire, l'appliquer au fichier de sauvegarde, pas à toutes les écritures du jeu indistinctement. Un flush C n'est pas à lui seul une garantie de résistance à une coupure électrique.

Qualification par injection : écriture partielle, espace épuisé, fermeture ratée, interruption avant validation, conservation de la dernière sauvegarde complète, options et chargement. Utiliser des copies de test, jamais risquer la sauvegarde de Valentin. Les variantes `.new/.bak/.prev` ont déjà des effets sur le chargement : ne pas ajouter un mécanisme concurrent sans connaître l'ordre original.

### C2. Menu de sauvegarde lisible et compatible

Proposition : conserver les points de sauvegarde et le format d'origine, améliorer sélection des emplacements, message d'écriture et erreur. Un backup tournant du fichier complet peut être envisagé après validation de C1, hors des noms que le jeu sélectionne automatiquement. Il protège d'un écrasement logique, pas d'un stockage physiquement défaillant.

Une sauvegarde rapide automatique n'est pas une adaptation neutre : progression, événements et tension du jeu peuvent changer. Elle reste une amélioration optionnelle future, pas un préalable au port console.

### C3. Boîtes de dialogue réelles

Constat : `MessageBoxA` appelle `plat_message_box`, puis retourne toujours 1 ; côté Vita, cette dernière ne fait qu'écrire un journal. Certains messages/demandes PC peuvent donc être invisibles ou recevoir une réponse implicite.

Proposition : inventorier les appels utiles et leurs boutons attendus, fournir un dialogue console lisible et retourner la bonne réponse. Distinguer information, erreur bloquante et confirmation. Afficher les données absentes, version non prise en charge ou manque d'espace avec une action compréhensible, sans chemin de développement ni formule technique incompréhensible.

## D. Menus et image pensés pour un écran de 5 pouces

### D1. Terminer les options console existantes

Fait : `rt_keep_res` retire déjà la résolution, vibration, orientation du regard, déplacements FPS et ombre temps réel des pages, conserve leurs valeurs forcées et ajoute la sensibilité. Les trois captures `build/live/options-page-{1,2,3}.png` ont été relues pour cet audit. Ce travail existe : il ne faut pas le présenter comme une nouvelle proposition.

Proposition : garder la direction artistique et les options du jeu pertinentes, clarifier « Retour »/« Quitter » selon l'effet réel, afficher l'aide L/R pour les pages et confirmer les changements de manière cohérente. Ne pas exposer `SH4_*`, les tailles de tas ou la compilation de shaders dans le parcours normal.

### D2. Lisibilité du texte, carte et HUD

Proposition : auditer sous-titres, notes, inventaire, chiffres de téléphone, codes, sauvegarde et aides à la taille réelle. Donner à l'interface une mise à l'échelle distincte de la résolution de la scène 3D lorsque la chaîne de rendu le permet. Préserver les effets de bruit du jeu par défaut ; un réglage de lisibilité/accessibilité peut être séparé.

Éviter un « tout en 960×544 » global qui augmenterait aussi les coûts des cibles 3D et post-effets. Une passe UI plus nette exige d'identifier ses cibles, coordonnées et masques ; elle ne doit pas déplacer le curseur ou découper les menus. Comparaisons nécessaires sur les cinq langues : longueur, accents, retours à la ligne et zones sûres.

### D3. Qualité des textures ciblée

Constat : le plafond par défaut visible dans `resource.c` reste 256, avec options 512 et 32 bits déjà examinées. Les essais antérieurs ne qualifient pas un passage global en 32 bits dans les zones à faible marge mémoire.

Proposition : budgéter par famille de ressources, prioriser éléments lisibles et personnages, conserver transparence et couleurs, puis libérer les doublons devenus inutiles. Une texture améliorée uniquement à un endroit ne justifie pas de charger une copie haute qualité de chaque ressource. Le mode de qualité final doit être choisi par mesures de mémoire, cadence et captures ; ne pas remplacer le rendu de référence à l'aveugle.

### D4. Interactions au curseur

Proposition : téléphone, digicodes et autres écrans à pointeur doivent fonctionner entièrement au stick gauche avec vitesse et accélération adaptées. Vérifier les deux axes, les bords et le clic. Le tactile avant peut devenir un accès direct facultatif, après transformation correcte des coordonnées de l'écran vers la surface d'interaction. Aucun besoin de souris ou clavier pour finir le jeu.

## E. Fluidité, ressources et chargements

### E1. Un budget de console par système

Proposition : enregistrer les allocations et libérations par propriétaire de scène/ressource, vérifier les retours au même état et distinguer tas invité, hôte, GPU et files de libération. Borner chaque cache avec invalidation par génération. Réduire les copies résidentes en double seulement lorsque tous les consommateurs ont migré. Les refus mémoire doivent se propager correctement sans continuer avec des objets invalides.

Les gardes COM/audio/vidéo et l'attente des libérations différées déjà ajoutées sont des protections, pas la preuve de disparition de toutes les fuites. Le lecteur audio à 512 Kio reste à confirmer, avec les 22,61 Mio toujours résidents pendant sa qualification ; Valentin testera lorsqu'il aura la console.

### E2. Préparation hors console des données stables

Proposition : prolonger les travaux de `sh4prep` vers des index et descriptions de modèles/collisions/archives identifiés par le fichier source et un schéma. Précalculer ce qui est immuable et effectivement reparsé en jeu. Exclure l'état vivant : poses, IA, scripts, éclairages dépendants d'événements et ressources mutables. Ne pas générer une représentation native avec pointeurs ou alignements d'un PC hôte.

Un fichier dérivé doit faire gagner du travail après raccordement et retrait du chemin redondant. Le stockage peut augmenter : mesurer aussi le transfert FTP, les lectures et la taille. Les mises à jour ordinaires doivent continuer à préserver les données du joueur et sa sauvegarde ; une régénération nécessaire doit être explicitement annoncée et ne pas écraser `save/`.

### E3. Charger sans créer de longues images imprévisibles

Proposition : exploiter les tables de destinations et de préchargement déjà connues pour préparer le prochain ensemble probable, avec budgets et annulation. Répartir les opérations de parsing/création à une frontière de chargement adaptée. Ne pas faire coexister sans plafond toutes les ressources de deux pièces ; un préchargement peut empirer la mémoire et les crashs.

Les données n'arrivent pas toutes directement du disque : distinguer lecture, conversion, création GPU, liaison shader et attentes des fils. Le cache GPU fourni évite des compilations dans les cas couverts ; il ne garantit pas qu'un chargement ou une liaison soit gratuit. Pas de préparation globale de trois minutes remise par défaut.

### E4. Priorité à une cadence logique stable, interpolation ensuite

Proposition : viser le budget de 33,3 ms sur les parcours réels difficiles, mesurer les longues images et leurs causes, puis n'insérer une image interpolée que lorsque le budget de rendu et la continuité l'autorisent. Les transitions caméra, menus et textes doivent rester exacts ; ne pas interpoler aveuglément un changement de plan.

La logique reste à 30 Hz. Le compteur d'images affichées doit être distinct d'une mesure logique pendant les diagnostics. Le correctif des accès fichiers périodiques existe déjà et doit être conservé. Le passage de 36 à 35 ms ne constitue pas un 30 fps constant ; ni ce document ni une réécriture ne promettent une cadence globale avant mesure.

### E5. Modules natifs et backend GPU : choix guidé par le profil

Proposition : poursuivre des chaînes cohérentes de modèles/animation, collision et ressources en C natif, en déplaçant leurs données et durées de vie avec leurs fonctions. Préserver les seuils, les contacts et l'ordre des événements par tests différentiels au GOG. L'organisation compte davantage que remplacer chaque instruction par une autre.

Un backend GXM direct peut être un chantier distinct si la traduction/soumission GL ou les changements de scène GPU plafonnent encore la cadence après profilage. Commencer par une passe représentative avec profondeur, alpha et rendu vers texture ; abandonner si l'équivalence ou le budget se dégrade. Cela ne résout pas automatiquement le coût CPU d'animation, d'IA ou de collision.

## F. Audio, cinématiques et exploitation du port

### F1. Cinématiques qualifiées dans leur contexte

Le catalogue et les lanceurs existent déjà : [NATIVE-CINEMATICS.md](NATIVE-CINEMATICS.md). Ils couvrent un sous-ensemble de pilotes compris, pas 182 séquences librement rejouables. Étendre seulement avec les ressources et états requis, puis valider cheveux/visages, sous-titres, skip, reprise du contrôle et libération mémoire. Une cinématique lancée hors contexte peut produire un défaut artificiel.

Distinguer FMV de fréquence propre et séquence du moteur. Réduire les tampons et copies ou déplacer des travaux selon profil, sans couper la bande-son ni forcer une FMV à 60 images. Tester aussi la veille et le retour au jeu au milieu d'un décodage.

### F2. Audio pour haut-parleurs et casque

Conserver les volumes et la stéréo déjà disponibles ; qualifier la balance, voix, ambiance, clipping, pause et reconnexion. Un preset de dynamique pour petits haut-parleurs serait une option future mesurable, pas une modification imposée de l'atmosphère. Les tampons actifs et les données sources doivent garder des propriétaires distincts, y compris lorsque des interfaces partagées sont libérées dans un autre ordre.

### F3. Séparer le candidat joueur du laboratoire

Proposition : regrouper les diagnostics lourds derrière une option de build, garder une télémétrie légère bornée et les vingt journaux déjà prévus. Éviter les scans mémoire, captures, polling de fichiers et dumps dans le jeu normal. Vérifier le résultat compilé : retirer du code inutilisé économise surtout de la taille, pas automatiquement du temps par image.

Un écran « À propos / aide » peut fournir version courte, commandes, emplacement des logs et marche à suivre, avec un mode diagnostic explicitement activé. Le menu de téléportation reste réservé aux testeurs ; il ne doit pas perturber une partie ou être requis pour installer le port.

## Ordre de réalisation proposé

| Lot | Périmètre | Ce qu'il apporte | Critère avant intégration |
|---|---|---|---|
| 1 | Écritures/erreurs de sauvegarde et messages invisibles | Évite de laisser croire qu'une opération ratée a réussi. | Injections d'échec, messages/réponses corrects et sauvegarde précédente intacte. |
| 2 | Carte complète des actions, esquive/info et prompts | Jeu entièrement utilisable sans périphérique PC. | Actions réelles et transitions d'entrées, sans conflit entre boutons. |
| 3 | Courbes/deadzones et sensibilité TPS/FPS/observation | Commandes prévisibles et réglages effectifs. | Mesures de vitesse, repos et inversions, régression des correctifs récents. |
| 4 | Veille/reprise, pause et gestion d'inactivité | Usage de portable fiable ; fin des protections permanentes inutiles. | Plusieurs cycles jeu/FMV/menus sans perte de données ni audio/temps désynchronisé. |
| 5 | Lisibilité UI/carte/notes et menus console | Meilleure expérience sur petit écran avec le style SH4. | Toutes les langues, contrôles et coordonnées cohérents, budget rendu respecté. |
| 6 | Ressources, mémoire, données préparées et chargements | Moins de risques d'épuisement et de pauses. | Gains en jeu mesurés, retours mémoire stables et aucune baisse de qualité. |
| 7 | Gros modules natifs, rendu et interpolation | Meilleure marge pour la qualité et la cadence. | Profil causal, équivalence et gains reproductibles sur zones difficiles. |
| 8 | Parcours narratifs et plusieurs consoles | Vérification de l'ensemble final. | Install neuve, update, sauvegarde, mort/reload, transitions, cinématiques et session longue. |

Les lots ne demandent pas tous d'attendre la fin du précédent pour leur analyse, mais une version rendue au joueur doit rester identifiable et cohérente. Pas de gigantesque fusion de tous les changements avant premier contrôle. Chaque candidat conserve le paquet précédent, les sources/options exactes et une voie de retour.

## Règles de préservation du jeu

Préserver la progression, les points de sauvegarde, les ennemis et leurs comportements, l'audio, les effets et les données du GOG. Une commande moderne peut être proposée, mais remplacer les déplacements traditionnels par une direction relative à la caméra change l'expérience : ce serait un preset optionnel avec qualification des collisions et changements de plan. Le gyroscope ou le tactile ne sont pas des prérequis.

Ne pas ajouter d'autosave, d'accélération de cinématique, de suppression d'effets ou de réduction de qualité en les présentant comme de simples optimisations équivalentes. Les effets volontairement étranges du jeu restent comparés à l'original avant correction.

Les choix de combinaison pour esquive/info, d'apparence d'interface et d'éventuels presets seront présentés concrètement avant de figer l'expérience. L'objectif immédiat est un port Vita complet et prévisible, avec une architecture qui prépare aussi les autres plateformes ; pas un changement de jeu.

## Périmètre de cet audit

Lecture ciblée des options, entrées, services de fichiers/dialogues, démarrage/watchdog, textures, interpolation, audio/vidéo, préparateur et rapports natifs. Les trois captures d'options ont été inspectées. Aucun nouveau parcours console, profil de performance ou test de panne n'a été exécuté. Les défauts de retour d'écriture et limitations de mapping sont des constats de code ; leur manifestation dans chaque scénario reste à reproduire avant correctif.

Point de départ technique recommandé : **C1/C3**, en test autonome PC sur les fonctions de production, sans intervention sur la Vita ni altération de sa sauvegarde. Ensuite, lot commandes A1/A3 puis lot système B. Ce choix vise d'abord les contrats manquants et l'expérience utilisateur ; il ne reporte pas l'étude des crashs de sessions longues.
