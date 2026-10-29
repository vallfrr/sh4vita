# Relais vers Claude — 29 octobre 2026, état après interruption demandée

## 29 octobre 2026, 21 h 30 — curseur du téléphone et menus (Claude, avec Valentin)

**Installé sur Vita1, validé par Valentin** : eboot `build/cursor-20261029/eboot-menu.bin` SHA256 **490f5f77693e55338196abd27484e062d263e0b8b2ad732d4f05d46920406689** (source = candidat5 c15c… + `dinput.c` + jeton `dump=N` de `draw.c`, build `~/build/sh4vita-cursor`, mêmes options que `sh4vita-guestmem`). Repli exact : `build/cursor-20261029/eboot-before.bin` (candidat5 c15c0e5a…), sauvegarde `sh4.sav.before` (e5074a44…). `env.txt`/`ctl.txt`/`gdump_*` retirés de la console. **Non commité** (dinput.c/draw.c contiennent aussi du WIP préexistant), **aucun nouveau VPK/ZIP** : le VPK `build/sh4.vpk` écrasé à 21:27 contient cet eboot.

- **Cause du curseur** : la position du pointeur (`0x5ff27c/0x5ff280`) est réécrite pendant la lecture de la manette elle-même, donc nulle à cet instant ; l'état « observation » (`0x10a1914`, `0x10a1c34==7`) est commun au téléphone ET à la vue par la fenêtre. Mode curseur = liste d'éléments cliquables `[0xfd1d50] != 0` et ≥2 de 6 indicateurs (`0x6e8ea0==0`, `0xfd1aa4==0`, `0x12f6dc4==0`, `0xfd5c28==1`, `0x10a0f44==1`, `0x10a1144==1`). Séparation exacte sur 7 dumps (jeu normal ×3, téléphone ×2, Start, fenêtre). Autres écrans à curseur (digicodes) **non mesurés**.
- **Menus** : titre (monde 0) et pause (`0x5ff2d8==0 && 0xec7664==0`) → stick gauche = d-pad (lX/lY), stick droit nul ; le stick droit n'est plus branché sur lX hors jeu en 1re personne.
- Outils : `ctl.txt` `dump=N` écrit `gdump_N.bin` (0x5c0000..0x1300000) ; `tools/dump_diff.py` compare des groupes d'états. **Depuis Codex, `ctl.txt` n'est lu que si `SH4_CTL=1` ou `SH4_LIVE=1` est dans `env.txt`.**
- Reste (voir échange avec Valentin) : licences/distribution, ZIP et Vita2 avec ce correctif + fluidité films, autres films, cinématiques floues, 30 fps constant.


Valentin a arrêté le travail pour changer de LLM. **Aucun pilote de test PC/Vita actif à la dernière vérification réelle.** Ne poursuivre les travaux qu'à sa reprise ; vérifier les processus avant tout accès. Les paragraphes plus anciens ci-dessous sont historiques.

## État exact du dernier correctif films

- **Candidat5 c15c0e5aecc7977a36f202aa13fd2a928819f76d194b3f74245e70bd5ebd037b installé sur Vita1 (192.168.1.175)**, démarrage normal confirmé, sauvegarde conservée et relueSHA **e5074a445206018dd05142157a8264b1a70ee5b10b815504a5b193e5d8238279**. Aucun env/ctl/key/tour ni audit lourd. La console est rendue au joueur : vérifier son usage avant de la fermer à nouveau.
- VPK figé : `build/room-audit/movie-fluidity-20261029/candidate-5/sh4vita.vpk`, SHA **0a90c20c3b233dca6e0cc822ddf74c3c134e29c6dd7a61e032a503c4caf048f4**. Eboot extrait de ceVPK et comparé au binaireWSL, symboles et sources archivés dans le même dossier. Repli b8d0 conservé. Seule entréeeboot modifiée parmi511entrées du VPK précédent ; qualité et préparateur inchangés.
- Réglages Vita par défaut : cacheMPEG **512Kio** partagé entre les deux lecteurs, toutesimages si le cache est disponible, décodage **core0 / prioritéSDLLOW0xbf**. Ancien : cacheOFF, sautB de rattrapage, core1 / priorité0xa0. Réservation explicite des slotsYUV pour ne pas écraser celui consommé. Refus d'allocation du cache : ancien chemin de lecture/rattrapage conservé. Replis de diagnostic : SH4_MOVIE_CACHE=0, SH4_MOVIE_ALL_FRAMES=0, SH4_MOVIE_CORE=1, SH4_MOVIE_LOW_PRIORITY=0 ; le binaireb8d0 reste le repli exact.
- **Deux films7/11 terminés avec les défauts du candidat5**, sans capture/audit lourd : moyennes de sortie hors fenêtre initiale **29,917 /29,962 images/s**, minima fenêtres **29,82 /29,84**, intervallesmaximum **61,91 /62,64ms**, aucun intervalle≥100ms relevé. Attente dans la file≤26,81ms. Différence compteurAudio−PTS vidéo **35,71 à64,99ms**, quantifiée par callbacks ; ce n'est pas une mesure de latence physique/DAC. Le premier rattrapage peut dépasser temporairement30/s (fenêtreinitiale30,77/30,78) : aucune interpolation ni accélération du film source.
- Deux captures séparées examinées sans corruption évidente. Sortie anticipée du film11 avant sa limite43,6s, retour normal sans blocage ; skip du film7 nonprouvé. `visual-skip/playback-assessment.json`, `review-assessment.json`, `review.png` et sourceoriginale1s gardent les limites. Ces captures sont exclues des mesures de cadence.
- PCfinal : **23/23images identiques**, binaire8e86f3af… ; sauvegardePC restaurée. Bancs hôte :632916paquets sur14films identiques,4985imagesYUV/PTS identiques,8000comparaisons concurrentes ducache,100000interleavingsFIFO,1000cyclesStop/Pause,10000comparaisonsPCM/états exactes.
- **Pas de test final des14films surVita** : Valentin l'a explicitement refusé au profit d'essais courts de2–3films. Pilote22548/full14 stoppé volontairement avant le premier film, originaux restaurés ; ne pas le relancer. STOP local de ce lot à conserver comme preuve.

## Ce qui reste à faire lors de « reprends »

1. Lire le rapportfilms mis à jour et les preuves `qualification.json`, `profile5-analysis.json`, `normal-final/complete.json`. Le gros défaut est amélioré dans ces deux essais ; **quelques irrégularités jusqu'à63ms subsistent**, pas de promesse de cadence parfaite sur tous les films, ni de tous les crashs résolus.
2. Finaliser le registre des changements et le paquet privé si demandé : **aucun nouveau ZIP fluidité n'a encore été créé**, aucunVPKcopié surI: dans ce dernier chantier. Le ZIPprivé vidéo-memory et le VPK de Vita2 restent **b8d0**, pasc15c. Le nouveauVPK est déjà prêt danscandidate-5 ; pas besoin de refaireSH4Prep/lesdonnées. Ne pas affirmer que Vita2 a été mise àjour.
3. Si poursuivre la régularité : profiler les pointes restantes avec ces deuxfilms, contrôler audio et temps entre nouvellesimages ; ajuster puis comparer àc15c. Ne pas retoucher les dimensions/couleurs, ne pas assimiler un compteur moteur60 à60images vidéo, ne pas relancer une vaste campagne sans risque concret.
4. RespecterAGENTS : français, pasde sous-agent/automatisme/publication/push pendant cette campagne ; commits éventuels exclusivement vallfrr <valentin.fortier51110@gmail.com>, auteur etcommitter. Beaucoupde WIP antérieur à préserver. Éviter la renormalisation globale de CONSOLE-CHANGE-REGISTER.md : octets historiques nonUTF8.

Preuves et pilotes dans **build/room-audit/movie-fluidity-20261029/**. Aucun travail ne doit être présenté comme encore actif à partir des PID historiques ci-dessous.

---

# Reprise courante — 29 octobre 2026, 19 h 01 (Paris)

## Réduction demandée par Valentin : DEUX films, pas14

Valentin demande explicitement de qualifier deux films, puis ajuster si nécessaire, sans lancer les14. Le pilote22548/full14 a été arrêté parSTOP **avant le premier film** (préparation~90s), originaux/ebootb8d0/save restaurés et relus (`full14/restored.json`). Ce n'est pas une panne et il ne faut PAS relancer le lot14. Ses preuves etSTOP restent archivés.

Nouveau pilote `movie-fluidity-20261029/profile5.py`, journalprofile5-controller.log, sortieprofile5/, owner.json : **deux films7/11, sans capture/audit lourd, candidat5 c15c0e5a… avec ses réglages par défaut** (core0, LOW, cache512Kio et toutesimages si allocationréussie). Il restaure b8d0 et les originaux à la fin. VérifierPIDréel avant tout accèsVita. PCfinal terminé **23/23 images identiques**, hash8e86f3af… ; sauvegardePC restaurée. Ne pas concurrencer le piloteVita.

Essai4prometteur, pas encore remplacé par une validation finale : débitlivré29,88/29,97fps ; sorties~30fps, minima des fenêtres29,90/29,83 ; écartsmaximum62,09/67,10ms ; queue≤27,2ms ; audio-minus-video41–71ms (horlogemélangeur quantifiée, pas latencephysique). Le premiercompteurfenêtre surestimait de1image (~0,5fps), corrigé danscandidat5 ; ne pas annoncer30,28comme source. Sourcepixels/audio/définitioninchangée. Après résultat5 : examiner lesdiagnostics, faire deuxcaptures séparées si utile, et remettre un VPKnormal seulementsi concluant ; aucun besoin de nouveaupréparateur ou de réextraction desdonnées.

## Historique : fluidité films — état courant du nouvel essai

**Propriétaire matériel unique 16116**, `movie-fluidity-20261029/profile4.py`, sessionoutil60687. Référenceb8d0 protégée/restaurée après chaque essai. Candidat4 **b0a41a37…**, VPK9ad9f907… ; eboot extrait duVPK et relu identique à celui du dossier de compilationWSL. Essai films7/11 à partirdu titre (frame900 préflightPC avec retour temporisé normal), cache512Kio, toutesimages, décodagecore0 **prioritéSDLLOW (0xbf dans la bibliothèqueSDK)**. Fichiersowner.json/profile4-controller.log/profile4/* ; pas de commandeVita concurrente.

Référence sansaudit : livraisonfilm7=25,55fps,11=25,40fps. Candidat2core1/cache+toutesimages : décodage~30 mais affichage irrégulier, nonretenu. Candidat3core0 priorité normale : livraison~29,97 mais sortie23,63/21,73fps, pointes100–106ms et avanceaudio~100ms ; nonretenu. AttentesSleep(2) parfois32–67ms pointent vers l’ordonnancement ; l’essai4abaisse la priorité du seuldécodeur. **Aucun gain de fluidité validé pour le joueur à ce point.** Ne pas confondre décodeur, upload et scanoutphysique.

Tests hôte : FIFO100000interleavings (slotoccupé préservé),lifetime1000cycles ; cache8000comparaisons concurrentes sur2films ; **632916paquets/type/PTS/durée/rewind sur14films identiques**,4985imagesYUV/PTS décodées identiques ; compteur audio10000comparaisonsPCM/états exactes. PC23/23référence pourcandidat1/2 ; compteur audio et branchesVita postérieurs, nouvelle validation finale àfaire si retenu.

Essais invalides conservés : candidat1installaitun ancien `build/eboot.bin` au lieu duVPK fraîchementcompilé ; STOP demandé, b8d0restauré/relu. Ne pas utiliser ce candidat ou sa mesure. Le préflightPC initial manquait libwinpthread dansPATH (sortie avant le jeu) ; reprisePATHcorrigée. Son exigenceEOF étaittropstricte : film7retour normal limité10s, dernierrelevé8s ; markercomplete explique cette réserve. Aucun deceséchecs n’est transformé en crashdujeu.

### Historique : relais films, candidat1 en essai

La référence8912 est terminée et ses originaux restaurés : sans audit/captures, film7=25,55fps livrées en moyenne, film11=25,40 (minimum23,1). **Propriétaire matériel20456**, `movie-fluidity-20261029/profile1.py`, sessionoutil11852 ; candidat expérimental **d6a70ae4…**, cache512Kio + toutesimages activés seulement dans env de test, FIFO avec vrais slots protégés. Films7/11 sans audit/captures ; repli b8d0 et originaux protégés/restaurés dansfinally. Consulterowner.json, profile1-controller.log et sousdossierprofile1/. Aucun autre accèsVita pendant ce pilote.

**PC36036/jeu4220** : régression23images en cours, `pc-regression.py`, session91573 ; sauvegarde protégée, ne pas concurrencer/rebuilderl'exe. Bancs hôteFIFO100000interleavings, lifetime1000cycles etcache8000comparaisons concurrentes sur7/11 passent. Préflight complet des paquetsMPEG des14films en cours, erreurs de compilation du banc corrigées sans changement du lecteur. Cache/toutesimages restentOFF par défaut dans ce candidat1. Ne pas le remettre au joueur comme correctif validé avant résultats matériels.

## Travail actif nouveau — fluidité des films

Valentin demande maintenant de traiter les films irréguliers, jusqu'à10fps ressentis. Il autorise explicitement l'utilisation de **Vita1** ; Vita2 reste au joueur. **Seul pilote matériel8912**, `build/room-audit/movie-fluidity-20261029/baseline.py`, sessionoutil95597. Référenceb8d0, films7/11, **aucun audit lourd ni capture** ; originaux/save/eboot sauvegardés et relus dans `original/`. Ne pas concurrencer ce pilote. Il restaure ses originaux à la fin. `owner.json`, `baseline-controller.log`, `baseline-error.txt`, marqueurscomplete/restored. Nouvelle source et nouveaux candidats séparés de ceux remis au joueur ; références figées sousbefore/.

Axes en préparation : partage borné des lectures audio/vidéo du mêmeMPEG, réservation réelle des slotsYUV, comparaison du sautB avec toutesimages, mesures de retard de sortie. Préserver qualité640×480, synchronisation et b8d0comme repli. Vérifier les processus avant toute nouvelle action ; STOP_ALL etSTOP local respectés.

## État à reprendre en priorité

- **Plus aucun pilote de test actif.** Les propriétaires 29232, 28348 et 22388 ont terminé. Vérifier les vrais processus avant toute nouvelle action. Aucun sous-agent, automatisme récurrent, commit ou push créé pendant cette reprise.
- **Vita1 (192.168.1.175)** : candidat mémoire vidéo **b8d0c09f… installé**, lancement normal relu, sans env/ctl/key/tour ni audit lourd. La sauvegarde effacée sur demande n'a pas été restaurée. Valentin peut reprendre la console : vérifier son usage avant toute fermeture future.
- **Vita2 USB I:** : données prêtes, **742 fichiers / 3 632 595 544 octets**, tous relus SHA256 précédemment. VPK maintenant déposé dans **`I:/data/sh4vita-test-2026-10-29-b8d0.vpk`**, SHA256 relu **6ff9892a2c4f3b6a71b6b0f3a634a286eed1b9c7aa09cc2c746ca1275bc78d1f**. **Pas installé sur cette Vita** : Valentin a choisi de l'ouvrir lui-même dans VitaShell après sortie du mode USB. Aucun autre dossier système modifié.
- **Nouveau ZIP privé** : `build/release/sh4vita-test-2026-10-29-q512-video-memory.zip`, SHA256 **25b8697bb8cff08246798d4bb98f623414d3c843d22eaf28970474eccde34d30**. VPK b8d0 + mêmes trois préparateurs + README bref + empreintes. Aucune publication. Ancien ZIP/eboot27d4 conservés pour repli.
- Profil conservé : textures512, rendu640×362, alpha32max512, DXT corrigé et réglages combinés déjà mesurés. Seul `eboot.bin` change parmi les 511 entrées du VPK précédent. Le préparateur et les données ne changent pas.

## Dernière qualification et limites

Le lot initial27d4 a terminé ses **six contextes ciblés / neuf IDs communs**, 91 captures examinées. Le clip60 reste presque noir et non qualifié visuellement. La vue blanche6:21 reste à confirmer dans une progression naturelle ; sa présence sur le portPC ne prouve pas le rendu voulu dans l'original.

**b8d0** libère le CPU secondaire (pile1Mio + TIB4Kio) et les cinq slotsYUV après les joins de Stop ; Pause les garde. Builds PC/Vita et23/23imagesPC exactes. Sur Vita, **8 films alternés7/8 dans un processus**, huit retours, huit images examinées, aucun nouveau dump ni diagnostic bloquant. Tas invité après retour109667→109675Kio (**+8Kio**), hôte13753→13785Kio (**+32Kio**). Une rétentionCOM résiduelle subsiste. Puis Cynthia2:24/ID6 terminée,15captures examinées sans corruption évidente. `normal-postcheck.json` confirme ensuite le lancement ordinaire et l'absence d'entrées de test. Ces preuves ne garantissent pas tous les états ni la résolution de tous les crashs des testeurs.

Preuves finales : `movie-lifetime-candidate/final/hardware-r2/`, `visual-review-complete.json`, `normal-postcheck.json`, `second-vita-package-ready.json`. Premier essai `hardware/` conservé : refus au démarrage du **paramètre** timeout30000 inférieur à settle90+dwell30000, pas un crash de lecture ; rollback27d4 vérifié. Reprise avec32000 terminée. Rapport : [mémoire vidéo et cadence](MOVIE-LIFETIME-2026-10-29.md).

## Prochaine étape demandée : fluidité des films

Valentin signale des films saccadés. Les14MPEG sont encodés à29,97fps ; les mesures de livraison donnent25,475–29,818fps en moyenne selon film, parfois23,3 sur fenêtre~2s. **Le VPK b8d0 ne corrige pas leur FPS.** Ne pas justifier ces pertes par une source24fps et ne pas confondre images décodées, uploads et affichage physique.

Analyse sans nouvelle modification retenue : double conversion YUV→BGRA et YUV→565 ; sautB hérité de l'ancien pipeline ; deux démultiplexages relisent le même fichier. Les compteursI/O partagés ne sont pas des coûts CPU exclusifs et ne s'additionnent pas aux temps imbriqués. Mesurer séparément audio/vidéo et sans captures ; tester les films7/11 dans un **nouveau candidat distinct**, avec synchroaudio et mémoire, sans réduire la qualité. Préserver le candidat remis et éviter de relancer la campagne386 sans nouveau risque.

Un banc hôte extrait de `hand_frame` prouve aussi qu'un producteur avançant de cinq images pendant que le consommateur est immobilisé réécrit son slot `obusy`. `probe-movie-fifo-busy.json`. Risque MPEG à traiter par réservation explicite dans un candidat distinct ; **aucune occurrence matérielle attribuée**, pas une explication prouvée des textures3D d'Henry. Aucun correctif de cette file ajouté à b8d0.

## Historique opérationnel conservé ci-dessous

Les propriétaires, étapes en cours et statuts antérieurs ci-dessous sont historiques. L'état ci-dessus prévaut.

# Reprise courante — 29 octobre 2026, vers 09 h 41 (Paris)

**Précision demandée par Valentin sur les murs blancs6:21 :** la mêmevue surPCduport ne prouve pas le rendu voulu danslejeuoriginal. Ce cas reste **à confirmer en contexte normal**, aucune modification de rendu spéculative. Le compteur386désigne les cas de ressources mesurés, pas386scènes intégralement validées visuellement. Conserver cette réserve dans le rapport final.

**Nouvelle demande : fluidité des films MPEG.** Les14headers annoncent29,97fps ; moyenneslivrées25,475à29,818,minfenêtre23,3. Ne pas les qualifier de sources24fps. Correctifb8d0=mémoiresvidéo, pas correctifFPS. Pistesdécodeur/doubleconversion/skipB/affinités documentées dansMOVIE-LIFETIME ; à mesurer sur candidatdistinct après remise secondeVita, sans retarder indéfiniment la copie du VPKqualifié.

## Reprise matérielle après garde de scénario — vers18h48Paris

28348 est terminé ; l’échec était **`FATAL: invalid SH4_AUDIT_TIMEOUT` avant toutelecture** : timeout30000 inférieur à settle90+dwell30000. Ancien27d4 restauré/ relu (`rollback.json`). Ce n’est pas un crash du lecteur. Preuves conservées dans `final/hardware/`; référence ancienfilm7 terminée. **Seul propriétaire maintenant22388**, `final/qualify-vita-r2.py`, sessionoutil3894, mêmeb8d0, timeout32000valide ; sortie `hardware-r2/`, journal `hardware-r2.log`, erreur `hardware-r2-error.txt`. Ne pas concurrencer. Il ne rejoue pas la référenceancienne. Huit lectures puis Cynthia et nettoyage, revue requise avant copieVPKI:.

Lot ciblé initial6contextes terminé,91captures examinées ; scène60 presque noire nonqualifiée visuellement, autres vues sans corruption évidente. `targeted-review-complete.json` détaille les limites.

## Relais effectif — dernière qualification mémoire vidéo

Le lot ciblé29232 est terminé : six contextes ciblés terminés, marqueur `placed-vita-complete.json`. **Seul propriétaire réel actuel28348**, `movie-lifetime-candidate/final/qualify-vita.py`, a acquis la premièreVita et commence la référence film7 sur ancien27d4. Aucun autre pilote ni commandeVita. Ensuite b8d0, huit films dans un mêmeprocessus, cinématiqueCynthia, nettoyage. Vérifier `hardware-owner.json`, `hardware.log`, `hardware-error.txt` ; revue visuelle et résultat mémoire requis avant VPK surI:.

## Relais matériel prêt et choix installation Vita2 (18h19)

Valentin a confirmé : **il lancera lui-même le VPK dans VitaShell** sur la secondeVita. Déposer uniquement leVPKqualifié surI: après les contrôles ; pas d’installation systèmeUSB à tenter, pas besoin d’IPsecondeVita.

Le préflightPC de8films dans unprocessus est terminé, savePC restaurée. Derniers compteurs précis absents après expiration du tour ; résumés à108Mio, ne pas prétendre huit mesuresprécises. **Nouveau relais28348**, `movie-lifetime-candidate/final/qualify-vita.py`, sessionoutil92516, attend29232 sans commandeVita. Voirhardware-owner.json/hardware.log/hardware-error.txt. Il n’acquiert qu’après finréelle du prédécesseur et marqueurs14films/5reprises/lotciblécomplet ; vérifie27d4installé. Puis film7ancien, installationb8d0,8films[7,8]×4dansunprocessus(timeouttour30000), captureparfilm, posttas, Cynthia2:24nouveau, nettoyage. Repli27d4suréchec. **Ne jamais concurrencer29232ni28348 aprèssonacquisition.** Pas tâcheplanifiée.

Voir [correctif, valeurs précédentes et protocole](MOVIE-LIFETIME-2026-10-29.md). Àl’écriture seul29232 contrôle encoreVita1 ; Cynthia2:24 terminée,15captures vuesavecbon décor, pas de corruptionévidente ; Forêt3:1 enpréparation. Films51captures examinées ;7/8imagesprisesaprèsretour donc rendufilmnonqualifiévisuellement : le nouveau test prendra des capturesprécoces.

## Mise à jour18h12Paris — candidat construit, tests encore actifs

**Vita1 : seul29232/media-targeted-finish.py** encore actif, au cinquièmecontexteisolé308 au dernier contrôle ; vérifierjournauxréels.14/14films terminés avec retour et reprise (9EOF,5arrêts cohérents avec limitesGOG) ; erreurcapture12initiale conservée, reprise12réussie. Prochaines cinématiques ciblées dans leurs bonnes salles. Toujours candidat27d4installé surVita1, pas sourcecorrigée.

**Nouveau candidatfinal compilé et figé**, `fresh-combined-20261029/movie-lifetime-candidate/final/` : eboot**b8d0c09f804ac2b0ff58a53c6516b313519985cbcd60f2188d6ac12d8151d7c0**, VPK**6ff9892a2c4f3b6a71b6b0f3a634a286eed1b9c7aa09cc2c746ca1275bc78d1f**, PC**e6b3b3a2bd3d0dcf0f662be29b8007c6689dbdb23cba64f7276e377688a0da08**.source.zip/symboles/SHA danscandidate.json. DeltaVPK : seul eboot parmi511entrées.23/23capturesPCréférence identiques, buildVita réussi. Les quatrechangements sont CPUownership+rollback, libérationCPU/YUVaprèsjoinStop, et ordonnanceurdiagnosticSH4_PLAY_MOVIE acceptant une liste sans réentrance ; le champCPU garde sonlayout. Les versionsintermédiaires448f/f0ae restent archivées **ne pas les installer**. **b8d0 non installé/qualifiéVita, pas surI:**.

Nouveau pilotePC **pc-movie-repeat.py**, sessionoutil34117, propriétaire dans `movie-lifetime-candidate/final/pc-repeat-owner.json` :8lectures[7,8]×4 dans un mêmeprocessus, retour+90images, allocations invitées relevées ; savePC protégée/restaurée. Ne pas concurrencer cePC. Le scénario utilise le nouvelordonnanceur sans modifierVita1.

DonnéessecondeVitaI: déjà terminées/vérifiées ; VPK en attente des résultats matériels. Après libération29232 et revue des capturesciblées, qualifierb8d0surVita1 par comparaisonancien/nouveaufilm court puis8lectures dansunprocessus. Vérifier mémoireaprèsStop, imagescinéma et diagnostic, repli27d4conservé. Ensuite seulement copierVPKsurI: et organiser installationVitaShell, sans toucheràapp/dbdusystèmeviaUSB. L’accèsUSB seul ne permet pas de confirmer une installationLiveArea.

## Priorité ajoutée — seconde Vita USB et candidat mémoire vidéo (29octobre,17h55Paris)

Valentin a autorisé la préparation de sa **seconde Vita, volumeUSB I:**, puis l’installation du **nouveau VPK après revue/corrections/qualification**. Ne pas installer prématurément le candidat source. La premièreVita192.168.1.175 reste réservée aux tests. Ne pas confondre les deux consoles.

**DonnéessecondeVita terminées** : `I:/data/sh4`,742fichiers/3632595544octets, tous relusSHA256. VolumeexFAT serial880050289. Preuve `second-vita-data-complete.json`, manifest742 dans `second-vita-data-manifest.json`. Aucun autre dossier modifié ; pasdesave/env/ctl/entréesde test. LangueJSONFrench. Les741donnéesGOG proviennent de la préparation neuve déjà vérifiée ; pas de modification d’assets du jeu. **Aucun VPK copié/ installé sur cette Vita à ce point.** L’installation d’un VPK exige VitaShell/AppMgr ; copier directement dansapp n’est pas une installation valide. Ne pas modifier la base du système viaUSB.

**PropriétaireVita réel nouveau29232**, `media-targeted-finish.py`, sessionoutil70028. Les18980 et14192 sont terminés : film12 avait EOF+retour, mais fermeture du test pendant l’écriture de shot5700 :196608octets au lieu de2088978, journal arrêté au débutframe5700 sans marqueurshotterminé. Preuve brute conservée dansmovie-12, pas un crash du jeu identifié. Le banc attend maintenant les écritures de captures et utilise4230/4350/4470. Nouveau lot : reprise12dans`movie-12-capture-retry`, film13 ; cinqcontextesisolés ; puis sixcontextescinéma ciblés issus des placementsPC (IDs6,9,26/groupe26–28,42,44,66/groupe60–66), seulement siPC terminé. **Pas relance générale**. `media-targeted-owner.json`, `media-targeted.log`, `media-targeted-error.txt`, campaign-owner.json. Toute nouvelle erreur hors refus de contexte arrête.

PréflightPC23contextes terminé,16terminés ; originauxPC restaurés, marqueur `cinematic-placement-pc-restored.json`. NouveaubinairePC en compilation/régression séparée, donc ne plus comparer sonSHA à l’ancien préflight sans le distinguer. Les deux anciens relaisVita ont quitté sans reprise.

**SOURCE PLUS RÉCENTE QUE LE VPK27d4 :** fuiteCPUvidéo caractériséehôte avec les fonctions de production :16cyclesStop/nettoyage du graphe suivant conservent16842752octets de piles/TIB. Correctif nouveau : métadonnéeshôte de propriété horsstructCPU, `rt_free_cpu` mainthreadaprèsjoin ; Stop et nettoyageanciengraphe libèrentCPU+5slotsYUV, Pause les garde. L’étatdécodeurposition/durée est conservé jusqu’au nettoyage du graphe.2000cyclesCPU avecrollbackallocation/ownership et1000cyclesPause/Stop/nettoyage passent. **Pas encore installé/qualifiéVita.** Les autres objetsCOM retenus et autres gardesOOM restent ouverts ; ne pas annoncer touslescrashes corrigés.

Candidat en préparation sous `fresh-combined-20261029/movie-lifetime-candidate/` ; ancienPC55e11… copié danspc-before.exe. BuildVita session5792, PCrégression session67180 : vérifier processus avant nouvelleaction. BibliothèqueDXT isoléeinchangée, GUEST200/qualité512/résolution640×362. Un premier build a été refusé par la garde car l’argument natifWindows séparait `.a` ; scriptbashlocal corrige le passage de chemin, **pas changement de bibliothèque**. Snapshot3fichiersàactualiser après build définitif (movie.c a reçu le dégagementYUV après le premiersnapshot). Figer nouveauxeboot/velf/VPK/SHA puis qualifier séparément avant remiseVita2. Pas depush/publication/sousagent.

## État prioritaire — reprise médias, 29 octobre (remplace les propriétaires ci-dessous)

Propriétaire réel vérifié **18980**, commande `python -u build/room-audit/fresh-combined-20261029/movies-and-context-resume.py`, sessionoutil48577. Seul piloteVita ; le préflightPC33920 est indépendant (voirci-dessous). Candidat27d4ff04 inchangé. Voir `resume-media-owner.json`, `movies-owner.json`, `campaign-owner.json`, `resume-media.log` et `resume-media-error.txt`. Ni STOP_ALL ni STOP local actif au lancement. Vérifier de nouveau les commandes réelles avant toute actionVita.

Les anciens18160 et le relais `isolated-vita-successor.py` ont terminé : film0 EOF+retour+90images ; film1 retour+90images sans EOF, test trop strict. Analyse de l'exécutableGOG : `0x412cf0` utilise la table des limites à `0x610078+index*0x104`, film1=60s ; arrêt observé après le relevé58,4s. `0xecb198` est **un état de fondu, pas l'ID du film** ; constante `0x5b7530`=0,01, pas60. Le résultat brut et l'erreur initiale restent conservés ; `movie-01/playback-assessment.json` consigne la compatibilité avec l'arrêt temporisé original, sans inventer un EOF. Test ajusté uniquement dans les scripts ignorésGit ; aucun code du jeu modifié.

Le propriétaire18980 reprend films2..13 puis les5contextesVita isolés (136,218,256,303,308). Chaque film exige demande originale, EOF ou arrêt cohérent avec sa limiteGOG, retour et90images après retour, aucun nouveau dump/diagnostic bloquant. Les cinq contextesPC ont **tous terminé**, originauxPC restaurés (`context-pc-results.json`, `context-pc-restored.json`). Arrêt au premier défaut ; aucun successeur parallèle. Après succès, nettoyage normal ; captures cinéma/films et nouvelles reprises à examiner. Les696fluxaudio candidats restent à qualifier, aucun succès implicite.

Couverture actuelle :386tentatives/381mesurées, **762/762captures examinées** ; endurance7222,43s/90visites, **90/90captures examinées**. Les20cinématiques communesVita sont terminées sans erreur bloquante ;130/130captures examinées, sans corruption évidente des acteurs mais décors absents/partiels horscontexte nonqualifiés. Nouveau testhôte696charges :97204346blocs/3110539072octetsPCM identiques à la référence généréeGOG ; lectureVita nonprouvée. Voiranalyse etmanifestcinéma. Ne pas répéter ces campagnes sans nouveau risque. Suivi conversation environ10minutes demandé par Valentin ; surveillance rapide des fautes conservée dans le pilote, **aucune tâche planifiée**.

Revue complémentaire pendant les médias : [allocations et durée de vie des graphes](ALLOCATION-AUDIT-2026-10-29.md). Chemins sans garde repérés par inspection (notamment listes de libérationsprep et construction COM/vidéo), **non corrigés et pas causes de crash mesurées**. Ne pas confondre avec le NULLaudio déjà attribué/corrigé. La fuite explicite de graphes vidéo nécessite un test répété dans un seul processus ; les14lancements séparés ne la qualifient pas.

### Nouveau préflight des vraies salles des cinématiques (indépendantPC)

**PropriétairePC33920**, `cinematic-placement-pc.py`, sessionoutil75806 ; premier enfantjeu31132 au lancement. Ne pas lancer un autre jeuPC. PropriétaireVita18980 inchangé, aucun accèsVita depuis le nouveau préflight. Consulter `cinematic-placement-pc-owner.json`, `cinematic-placement-pc-results.json`, `cinematic-placement-pc.log`, `cinematic-placement-pc-error.txt` et marqueur de restauration.

Découverte dans les instructionsGOG :0x52c4c0 interprète les flux de commandes des tables0x6553a4/0x6553d8, opcode0 indexe par salle ;0x4f0af1 passe bien0xfd5a94 à0x52ca00. Opcode24 lie le driver. `tools/cinematic_contexts.py` exporte45placements/28IDs communs, `cinematic-placements.json`. ExempleID6 en2:24, pas2:1 utilisé précédemment ; les fonds absents ne doivent pas être considérés normaux/corrects à valider.

`cinematic-placement-plan.json` prépare23contextes/26IDs, plus1/2 déjà joués dans leur vraie salle. Le préflightPC les tente séparément avec entrées du catalogue, originauxPC protégés ; arrêt sur crash/stall/fatal hors garde laboratoire«scene never ready», contexte refusé archivé et nonvalidé. Le succès d’une salle placée exige lecture complète et revue d’image ; les conditionsnarratives ne sont toujours pas reconstituées. **Ne pas lancer encore le lotVita de ces23contextes** : attendre résultatPC, sortie/restaurationPC et libérationduVita18980 ; ne tenter que lescontextes prouvés, sans rejouer les anciens contextes erronés. Les150cinémas àdriversspécifiques restent nonqualifiés.

### Relais ponctuel enregistré (pas une tâche récurrente)

**14192**, commande `placed-vita-successor.py`, sessionoutil99061, attend uniquement : sortie réelle18980 et33920, marqueursfilms14/14 et5reprisesisolées, puis succès/fin/restaurationdupréflightPC. Il **n’a aucun contrôleVita pendant cette attente**. `placed-vita-owner.json`/`placed-vita.log`/`placed-vita-error.txt`. Après tous ces prédicats seulement, il prend la Vita et tente les placements ayant réellement terminé surPC ; contexte explicite refusé nonqualifié, toute panne/erreurGPU/allocation/core arrête. Même candidat27d4ff04. Préserver les captures et résultats ; revue manuelle requise après lot. Aucun autre pilote ni commandeVita pendant18980/ce relais actif.

Dernier point communiqué :8films terminés, movie08(neuvième)enlecture ; préflightPC10/23contextes tentés dont5terminés, les autres nonqualifiés par contexte. Batterie récente78% (ne pas affirmer la charge sans relevé frais). Les compteurs restent dynamiques ; vérifier les processus et journaux.

## Historique opérationnel antérieur : campagne combinée

**Dernier propriétaire :18160, movies-qualification.py/session61429**, cinématiquescommunes17976 terminées20/20 sans erreur bloquante signalée, captures à examiner. Lecture14filmsMPEG démarrée, movie00 en cours ; suivre movies-owner.json/campaign-owner.json, pas l'ancien17976. PCcontextes : quatre des cinq ont terminé isolément, dernier308 en cours, pilote15268/session75149 ; consultercontext-pc-owner.json et résultat/restauration avant autrePC.

Relais ponctuel supplémentaire préparé : **isolated-vita-successor.py**, attend sortie réelle18160, movies-complete.json, fin/restauration et succès des5contextesPC. Aucun contrôleVita pendant l'attente ; propriétaire et erreurs dansisolated-vita-owner.json/isolated-vita-error.txt. Il tentera les5casVita séparément, même candidat27d4,90+120images,2captures chacun. Les échecs originaux restent conservés ; ne compter386mesurés que si les5reprises réussissent réellement. Arrêt au premier échec, pas de redémarrage masqué. Après ceslots, audio696candidats et revuecinéma/films restent àfaire. Pas de tâche récurrente ni autre pilote concurrent.

**Suite préparée sans nouveauVPK :** l'option existante `SH4_PLAY_MOVIE=4200:index` appelle le lecteur original0x412ef0. Nouveau relais ponctuel **movies-qualification.py**, sessionoutil61429, attend la sortie réelle de cinematic17976 ET `common-cinematics-complete.json` avant toute commandeVita. Il enchaînera les14filmsMPEG0..13 avec le mêmeeboot27d4 : fin de flux obligatoire, retour du lecteur puis90images de reprise, captures et erreurs/dumps archivés. Vérifier **movies-owner.json/movies-qualification.log/movies-error.txt**. Pendant l'attente, les deuxPython sont normaux, seul17976 a la console ; après relais, le PIDmovies-owner devient propriétaire. Aucun redémarrage automatique après un crash/échec. Les14films ne sont pas les182cinématiques du moteur ni les696fluxaudio. Au succès retour configuration normale ; autresfluxaudio et contextes cinéma spécifiques restent àfaire. Pas de nouvelle automatisation récurrente.

**Analyse en cours, nouvelle demande de cadence :** Valentin souhaite des points d'avancement espacés d'environ10minutes, sans gaspiller les tokens à seulement regarder les images. Le pilote garde sa surveillance rapide des fautes ; ne pas créer une tâche planifiée récurrente. Voir [QUALIFICATION-ANALYSIS-2026-10-29.md](QUALIFICATION-ANALYSIS-2026-10-29.md).90captures d'endurance et648des762captures de couverture ont été vues ; suivi par page dansvisual-review/manifest.json, pas de corruption évidente dans ces vues mais cadrages obstrués/origines limitantes. Pistepause :143/147images>100ms contiennent>=20msI/O ; lecturesSErépétées600Kio jusqu'à67ms, pas seulementBGM. Ne pas modifier le candidat pendant le lotcinéma. Inventaireaudio13archives :355PCM,696candidats audio,182cinémas,263vides. Nouveau test réel `dsound_real_bank_test.py` :355chargesPCM,25347callbacks comparés,103821312octets identiques, hôte seulement. Premiers bancs trop stricts corrigés pour les25data qui chevauchent l'entrée et la queue virtuelle2octets ; assets inchangés. CouvertureaudioréelleVita et696candidats restent ouverts.

**COUVERTURE386 TERMINÉE, nouveau lot cinéma lancé.** Pilote12828 terminé :386cas tentés,381mesurés et5nonqualifiés (3:21revisite,4:39revisite,5:15revisite,6:21,7:2), changements automatiques d'état/salle ; aucun nouveau core collecté. Captures non encore toutes examinées. `coverage-complete.json`/`coverage-progress.json` donnent les détails. Après vérification qu'aucunPython/jeuPC n'était actif, nouveau propriétaire unique **cinematics-resume.py**, sessionoutil à retrouver et PID danscampaign-owner.json ; journalcinematics-resume.log, erreurscinematics-error.txt. Candidat27d4ff04 inchangé. Le lot tente20cinématiques communes des5contextesPC déjà terminés, dans des lancements séparés ; ne couvre pas tous les182IDs. Les12autres contextesPC et pilotes spécifiques ne sont pas validés. Revue des90captures d'endurance et des captures de couverture, diagnostic des147images>100ms de l'endurance, cinqcas nonqualifiés et inventaire/lecture des sons restent à faire. Ni campagne globale ni sons ni toutes les cinématiques ne sont déclarés terminés.

**DERNIÈRE REPRISE :** le relais22032 est terminé, parcours varié arrêté après4cas sur351 :5:1 avait été atteint puis état réel5:18 avant fin de séjour, timeout, aucun core. Source du changement d'état pas encore déterminée (mouvement/exploration et contexte narratif possibles), **pas un crash démontré**. Nouveau propriétaire exclusif : `fresh-combined-20261029/coverage-resume.py`, sessionoutil1581 ; PID dans **coverage-owner.json** et campaign-owner.json. Il parcourt les386cas de ressources par lots20 **sans mouvement** ;90images de stabilisation puis120de séjour,2captures par cas. Les timeouts de contexte sont archivés/nonqualifiés puis la couverture reprend les cas non encore mesurés ; aucune erreur fatale/allocation/GPU/core n'est ainsi ignorée. Résultatscoverage-progress.json, résumécoverage-complete.json après toutes les tentatives. Un cas chargé n'est pas une image visuellement validée. Les preuves2h et l'échec351 restent conservés, pas transformés en succès. Lire coverage-resume.log,coverage-error.txt et vérifier les processus avant toute commandeVita. Cinématiques/sons restent des lots distincts à faire après couverture.

**État le plus récent : endurance2h terminée conformément à la demande.** `two-hour-endurance.json` :7222,43s sans redémarrage,90visites terminées et90captures, arrêt intentionnel. Ancien35216 terminé ; **seul propriétaireVita actuel22032, after-two-hours.py**, phase `broad-varied-resources` :4cas terminés,5e en cours au dernier contrôle. Batterie56% en charge, aucun nouveau dump collecté. Les erreurs brutes « minimum3h nonatteint /192visites incomplètes » de l'ancien runner décrivent cet arrêt demandé, pas un crash. Revue des90captures encore requise.

La sériePC d'interactions est **arrêtée**, pas en cours : premier tunnel1 non rejoint dans le contexte neuf,7100images atteintes sans fatal mais prédicat non prouvé. `gameplay-pc-owner.json` est marqué stopped-context-unverified, sauvegardesPC restaurées, aucun jeuPC actif au contrôle. Le pilote tunnel4 précédent a bien terminé sa traversée ; ne pas généraliser son succès aux11routes. Valentin a ensuite confirmé que la priorité est désormais **couverture des ressources, sons et cinématiques**, sans rechercher toutes les interactions ni reproduire une progression complète. Ne pas passer du temps sur le tunnel1 tant que ce n'est pas nécessaire au chargement de ressources manquantes.

**Mise à jour gameplayPC :** premier pilote tunnel4 terminé, entrée normale10:4 puis retour1:2,33captures regardées sans corruption évidente,136,18s, originauxPC restaurés. Les10autres routes sont maintenant lancées en séquence par `fresh-combined-20261029/gameplay-pc-batch.py` ; consulter **gameplay-pc-owner.json** et le processus/commande avant tout autre jeuPC. Journalgameplay-pc-batch.log et résultatsgameplay-pc-results.json. Le banc refuse un jeuPC concurrent et n'émet aucune commandeVita. Les anciens49006/jeuPC du tunnel4 sont terminés ; le nouveau pilote est le batch, pas l'ancienne session. Arrêt au premier événement non prouvé, preuves conservées.

**Extension demandée : automatiser du vrai gameplay, pas seulement les salles.** Voir [GAMEPLAY-AUTOMATION.md](GAMEPLAY-AUTOMATION.md). Nouveau bancPC `tools/gameplay_scenario.py`, pilote actuel `gameplay-pc-tunnel04`/session49006 (vérifier processus avant autrePC), binairePC courant, aucune commandeVita. Prépare un point source puis utilise Action/confirmation et exige entrée10:4 puis retour. SauvegardesPC protégées/restaurées. Inventaire `gameplay-plan/scenarios.json` :386cas ressources,578enregistrements d'entréeporte,182IDs cinéma,11routes normales ; ce sont1157enregistrements de couverture, **pas1157tests réussis**. Objets/énigmes et pilotes cinéma spécifiques restent à mapper avec prérequis et prédicats ; ne pas activer aveuglément tous les drapeaux. Les pilotesVita actuels35216 et relais22032 ne sont pas modifiés ni concurrencés.

**DERNIÈRE INSTRUCTION : minimum d'endurance ramené de3h à2h par Valentin.** Pilote35216 conserve exclusivement la Vita jusqu'à cet arrêt propre. Un relais **ponctuel, sans commandeVita avant la sortie du précédent**, `after-two-hours.py`, session94705, attend elapsed_seconds>=7200 danscontinuous-soak, écrit un STOP identifié, attend la sortie/nettoyage complet du pilote35216 et vérifie les résultats. Puis il retire uniquement son STOP et démarre `broad-varied-resources` :351cas salle/visite, mondes entrelacés,90images de stabilisation+300de séjour, déplacement/caméra et capture finale, sans redémarrage de jeu dans cette phase. Les cas narratifs connus sont séparés ; tout autre refus/timeout arrête et conserve les preuves, pas de saut caché. Vérifier **`handover-owner.json` et `handover-error.txt` en plus de campaign-owner.json**. Pendant l'attente, la présence de deux Python est normale : seul35216 pilote la console. Après relais, le propriétaire sera le PID d'after-two-hours.py.

Le minimum10800 et les192visites du premier script ne sont pas modifiés en mémoire : son résultat brut comportera volontairement operator-stop et durée3h nonatteinte. L'autorisation2h, le motif d'arrêt et la durée réelle seront consignés dans `two-hour-endurance.json` ; conserver les résultats bruts, ne pas les transformer en192/192 ni3h validées. Revue des captures toujours nécessaire. Ce relais remplace la fin cinéma/mondefinal du premier pilote, qu'il faudra donc reprendre **après** la couverture variée. Ne pas confondre route351 avec totalité des386variantes graphiques.

**Demande ajoutée : élargir les chargements au-delà des4salles.** Ne pas interrompre la référence continue actuelle. Après libération du pilote, préparer une endurance à ressources variées (mondes entrelacés), distincte de la couverture graphique386cas par lots. Manifestes préparés uniquement sous `fresh-combined-20261029/broad-coverage-plan/` : `plan.json`, `varied-room-visits.txt`, `narrative-isolated.txt`. Ils ne sont pas exécutés et ne prouvent pas que chaque contexte narratif est valide ; préflight et isolation des transitions obligatoires. Les cas connus5:15/9:2 sont séparés. But : comparer les ressources à des retours dans une même salle de référence après chargement de scènes différentes, et distinguer pic, mémoire relâchée et croissance persistante. Ne pas annoncer qu'une passe en lots avec redémarrages valide cette endurance variée.

**Nouveaux fichiers testeur11/12 analysés pendant l'endurance, sans toucher au pilote :** voir [TESTER-INCIDENTS-2026-10-29-11-12.md](TESTER-INCIDENTS-2026-10-29-11-12.md). Core complet1793259684 : même mixeuraudioNULL, offsetRX0x5a5396/R5=0 que le précédent, correctif déjà inclus dans27d4ff04. Core1793264328`.tmp`4608octets tronqué, faute non attribuable. Log11 contient15refus de tampons de texture (3×1024Kio,12×256Kio), tas hôte23Mio mais VRAM encore disponible ; problème de mémoire prouvé, cache de shaders pas mis en cause par ces preuves. NouveauDXT évite ce gros tampon pour les textures admissibles, sans garantie de résolution universelle. À ce contrôle l'endurance est toujours active,32visites/~45min, tas hôte pic9674Kio ; pas encore3h. Copies et empreintes sous `tester-incidents-11-12-20261029/`. Ne pas modifier le candidat pendant la qualification.

**Lire ce bloc avant les anciennes entrées ci-dessous.** Contrôle réel : seul `python -u build/room-audit/fresh-combined-20261029/resume-endurance.py`, **PID35216**, session outil81960. Aucun jeuPC/second pilote observé ; STOP_ALL et STOP local absents. Vérifier à nouveau commande, propriétaire et progression : un PID seul n'est pas une preuve. Ne pas envoyer de commandesVita ni lancer de deuxième test tant que ce pilote avance.

- Dernier jalon : `timing-switches-off`, image6317, destination2, une destination terminée,241,7s dans cette phase. Le fichier `campaign-owner.json` se met à jour environ toutes les5s ; c'est la référence dynamique. `resume-endurance.log` ne reçoit principalement que les fins de phases, son silence seul ne prouve pas un blocage.
- **Alimentation confirmée :52%, « en charge »**, relevé watchdog165s du lancement courant. L'ancien avertissement batterie sanscharge est historique. La console est accessible et le jeu tourne.
- **Installation neuve achevée et vérifiée**, pas à refaire :742fichiers distants relusSHA256, contenuVPK vérifié, preuve `install-result.json`. Ancienne sauvegarde volontairement effacée conformément à la demande ; copie de précautionPC conservée, **ne pas la restaurer**.
- Candidat installé et ZIP restent figés : eboot `27d4ff04…`, VPK `db2f5a15…`, ZIP `sh4vita-test-2026-10-29-q512.zip` (`3c8538bf…`). Empreintes complètes dans [QUALIFICATION-2026-10-29.md](QUALIFICATION-2026-10-29.md) et `candidate.json`. Textures512,640×362, bibliothèqueDXT corrigée et correctifaudio inclus. Aucun nouveau changement du jeu depuis le build.

### Ce qui a réellement été fait dans cette reprise

L'ancien pilote32236 est **terminé en échec de scénario**, pas encore actif : sept destinations mesurées,14captures valides, puis timeout du monde9:2 avec état réel1:2. Le bâtiment5:15 déclenche une cinématique qui renvoie à l'appartement, également constatée surPC ; ne pas rejouer le chaînage5:15→9:2 identique. Les preuves ont été conservées sous préfixe `narrative-smoke-` ; les images/logs restent dans `smoke/`.

Les14captures ont été regardées sur la planche `smoke/review.png` : pas de corruption de textures évidente dans ces vues. Certains cadrages sont proches du dos d'Henry ; ce n'est pas une validation de tous les états du personnage. Mesures de3122images de séjour : moyenne34,360ms, P9538,206ms, P9940,846ms, maximum99,915ms ;0image>100ms. Tas hôte pic9841Kio, VRAM minimale44615Kio. **Le smoke complet reste en échec**, et ces chiffres agrégés ne prouvent pas30fps constants.

Départ du scénario fiable confirmé : `SH4_TITLE_SELECT=1200:0` et ROUTEclavier historique ; **pas** le secours1800 ni les injectionsPADIN des tentatives abandonnées. Ne pas modifier le script en cours d'exécution en prétendant modifier son processus.

### Enchaînement exact du pilote35216

1. `timing-switches-off` : Eau4:11, Passé8:24, métro2:14, hôpital7:32 ;180images de stabilisation puis1800mesurées par salle, vrais axes déplacement/caméra, sanscaptures. Même candidat mais PREP_SCALAR/INTERP_SKIP_LARGE/AUDIO_HEAD_CACHE/PREFETCH_SMALL=0.
2. `timing-combined` : mêmes salles/parcours, défauts duVPK actifs. Comparaison de ces quatre sélecteurs uniquement, **pas comparaison complète avecmain**.
3. `continuous-soak` : ces quatre salles répétées48fois,192visites,180+1800images par séjour, une capture de fin par visite. **Un seul processusjeu sans redémarrage, minimum10800secondes réelles** requis ; limite18000s. Logique/interpolation, mémoire et diagnostics observés. Trois heures **pas encore réalisées** à ce point.
4. `isolated-final-world` :9:2 seul dans un lancement neuf, afin d'éviter le retour narratif du bâtiment. Le bâtiment dispose déjà de ses deux captures, pas de relance inutile.
5. `cinematics-subway` : pilotes1/8 ; puis20cinématiques communes dont les contextes ont terminé surPC, groupes des mondes1/2/3/4/7. Les12contextesPC non qualifiés des mondes5/6/8/9 restent consignés, pas assimilés à des crashsVita.
6. **Le script ne lance PAS les386cas du catalogue.** Après son succès, il retire le pilotage, lance normalement, puis marque `endurance-and-cinematics-finished-coverage-pending`. Il faudra examiner les nouvelles captures et organiser ensuite la couverture restante, en isolant les contextes narratifs. Ne pas annoncer la campagne totale terminée sur la seule base de `endurance-complete.json`.

### Reprendre sans dépendre de la conversation

Toutes les preuves sont sous `build/room-audit/fresh-combined-20261029/` (ignoréGit). Consulter dans cet ordre `campaign-owner.json`, processus réels et commande, `resume-endurance.log`, dernier `<phase>/sh4log.txt`, puis `<phase>/result.json`/`resource-timeline.json` et `results.json`. Le runner archive chaque phase et les nouveaux dumps, et s'arrête sur fatal, échecallocation, erreurGL/GXM, blocage, timeout, capture manquante ou batterie<10% sanscharge. En échec il fermeSH4, retire les entrées et ouvreVitaShell ; préserver les preuves avant toute reprise. Aucun redémarrage caché permettant de valider une phase échouée.

`RAPPORT.md` est généré à la sortie du pilote ; il peut être ancien pendant une phase. La revue visuelle faite aujourd'hui est aussi consignée séparément dans `smoke/visual-review.json` pour ne pas modifier la liste de résultats chargée en mémoire par le contrôleur. Ne pas éditer ses résultats actifs pendant son exécution. Les mesures sont instrumentées ; temps de chargement, captures et logique doivent rester distingués. Latence physique bouton-écran non mesurée. Source, candidat compilé, installation et paquettesteur restent distincts. Aucun push/publication, automatisme récurrent ou sous-agent autorisé pendant ce lot.

## Historique opérationnel de cette matinée — remplacé par l'état ci-dessus

**NOUVELLE MISSION PRIORITAIRE :** Valentin a ensuite autorisé une remise à zéro complète de SH4 sur sa Vita (ancienne sauvegarde comprise), une préparation neuve et une campagne intensive d'au moins3heures, textures512/résolution640×362 retenues. Voir [QUALIFICATION-2026-10-29.md](QUALIFICATION-2026-10-29.md). La sauvegarde précédente ne doit plus être restaurée automatiquement. Parent unique `build/room-audit/fresh-combined-20261029/run-all.py` PID18916, transfert enfant2448 au lancement, session64508 : vérifier les processus réels et `sequence-owner.json`/`owner.json`/`campaign-owner.json` avant toute commandeVita. Le parent enchaîne copie+SHA256+installation puis tests ; ne pas doubler le pilote.

**Nouveau ZIP créé :** `build/release/sh4vita-test-2026-10-29-q512.zip`, SHA3c8538bf… ; eboot combiné27d4ff04… avec correctif audio ET bibliothèqueDXT corrigée. Source et symboles figés dans le dossier de campagne. Ce candidat remplace les anciens4092/aab… pour cette qualification, mais n'est pas encore validé. Les anciennes mentions de consoleC5/savef39 restaurées décrivent l'état **avant** le nettoyage demandé. Application/données/save supprimées (881entrées), nouvelle copie en cours au dernier contrôle. Installation validée uniquement après `install-result.json` ; minimum3heures non encore accompli. Les préparateurs du ZIP sont les natifs compatibles0.2.0 déjà distribués, pas une recompilation multiplateforme du source0.3.0.

Le buildWSL `~/build/sh4vita-guestmem` est maintenant configuré **SH4_TEST_OPTIMIZATIONS=ON / SH4_TEST_QUALITY_512=ON / bibliothèqueDXT corrigée** ; ne pas suivre les anciennes phrases ci-dessous parlant du cacheCG normal restauré. Les blobs mesurés sont archivés et ne doivent pas être remplacés. PréflightPC supplémentaire unique `cinematic-preflight.py`/session5812, jeuPC22760 au premier contrôle : il vérifie les32événementscommuns par monde avec sauvegardesPC protégées ; vérifier le processus réel avant tout autre testPC. Les contextes qui échouent ne deviennent pas automatiquement des bugsduport.

**09h04 : installation neuve TERMINÉE et vérifiée**,742fichiers de données relusSHA256 et toutes les entréesVPK installées relues ; `install-result.json` l'atteste. Boot normal : français(2),640×362, cacheVPK utilisé, aucun audit actif. Parent18916/session64508 a lancé **campaign.py PID23996**, phase smoke ; lire `campaign-owner.json`/`campaign.log`. Les changements de scripts ne doivent plus être appliqués au fichier en prétendant qu'ils modifient ce processus déjà démarré.

**09h17, état plus récent :** premier parent18916 et campagne23996 **terminés en échec de scénario**, aucun crashVita : première demande de salle à3700 alors que le jeu restait en0:0 (menus), timeout7301 ; originaux de cet essai archivés avec suffixe`initial-startup-failed`, dont rapport/log/résultats/source. Ne pas les compter validés. Vérification`startup-check.py` terminée : le sélecteur normal du menuNewGame0 à1200 lance bien le jeu, six captures prises et planche revue (texte introductif français puis chambre). Les validations utilisent désormais aussi les boutonsDIpad de confirmation et un secoursTITLE_SELECT1800:0 ; candidat/VPK inchangés. **Contrôleur unique actuel : campaign.py PID14192/session93201**, smoke en cours ; anciensequence-owner.json « failed » décrit le parent précédent, pas ce nouveau propriétaire. Question charge toujours en attente au dernier contrôle (batterie57%, sanscharge). Reprise directe de la campagne, **ne pas réexécuter la suppression/réinstallation**.

**09h23, priorité propriétaire : campaign.py PID32236/session32989**, `campaign-owner.json` fait foi. Tentative pad+sélecteur1800 abandonnée proprement par STOP créé par l'agent (retiré après sortie), preuves`pad-startup-stopped` conservées ; cela restait dans les menus, pas un crash. Reprise utilise **exactement le sélecteur1200 validé par les captures**, sans injections de boutonspad au démarrage ; log confirme `[menu-test] title choice=0 frame=1200` puis `[pad] world1`. Smoke en cours avant la demande de salle3700. Ne pas relancer les anciens parents/PID. SourceVPK et données inchangés ; batteries55% sanscharge, question en attente. Trois heures pas encore faites.

**Attention alimentation :** premier log normal donne60% **sans « en charge »**. Question asynchrone envoyée à Valentin pour brancher le chargeur. La campagne continue tant que c'est raisonnable, mais s'arrête sous10% sans charge ; ne pas prétendre3hvalidées si elle s'arrête avant. La boucle continue est finalement192visites des **quatre** destinations d'endurance éprouvées, pas huit (5:15 déclenche une transition narrative vers1 surPC). Les autres sont dans smoke/couverture. PréflightPC terminé, sauvegardesPC restaurées :20cinématiques communes complètes dans mondes1/2/3/4/7 ;12 non qualifiées dans5/6/8/9 car événement natif déjà actif/changement de monde/refus ou garde du laboratoire « scène jamais prête », pas crash graphique prouvé. La séquenceVita tentera les20contextesPC terminés après l'endurance, puis386cas. Aucun propriétairePC actif au contrôle final du préflight.

**Point d'entrée pour une nouvelle conversation dans ce dépôt.** Les nouveaux messages de Valentin et les processus réels priment si l'état a changé. Les anciennes sessions et leurs listes « next session » sont historiques.

## Lecture et mission

Lire ce fichier, l'en-tête de [STATUS.md](STATUS.md), puis [QUALITY-2026-10-29.md](QUALITY-2026-10-29.md). Détails opérationnels locaux : **dernières entrées** de `build/room-audit/CHECKPOINT-V26.md` (06h41 notamment), puis journaux/propriétaires ci-dessous. [WORKFLOW.md](WORKFLOW.md) contient les commandes ; [CONSOLE-CHANGE-REGISTER.md](CONSOLE-CHANGE-REGISTER.md) conserve avant/après/repli ; [NATIVE-MODEL.md](NATIVE-MODEL.md) et la [roadmap](ROADMAP-PORTAGE-NATIF.md) couvrent les travaux natifs antérieurs.

Valentin a laissé la console disponible pour la nuit : améliorer le rendu si le coût le permet, réduire les pauses et optimiser sans régression, viser30images logiques/s et interpolation quand elle tient. Nouveaux chantiersUI, fonctionnalités de sauvegarde et distribution/licences **reportés** ; protection des sauvegardes et non-régression obligatoires. Pas de sous-agents, reprise planifiée, push ni publication pendant ce lot. Une demande de compte rendu n'est pas un arrêt.

Le contexte de reprise ne dépend pas d'une mémoire extérieure au dépôt. Les preuves/scripts sous `build/` sont locaux et ignorés par Git : ils ne suivront pas un clone neuf.

## Propriétaire actif : vérifier avant toute action

**MISE À JOUR : le contrôleur35732 a terminé tous ses enfants. Aucun jeu PC ni contrôleur Python actif au dernier contrôle.** `validation-owner.json` donne `finished:true`. L'endurance finit16/16retours, zéro diagnostic bloquant, restaurationC5/savef39/configuration prouvée dans `quality-full-endurance-20261029/restored.json`. Les lots images/précision/mouvement/endurance sont terminés mais leurs nouvelles captures et leurs résultats détaillés restent à examiner. Les indications de processus ci-dessous sont l'historique de cette séquence, **pas un propriétaire encore actif**.

Diagnostic prioritaire ajouté après la nuit : [crash audio du testeur](TESTER-CRASH-2026-10-29.md). Le dump révèle un accumulateur audioNULL, encore non vérifié dans la source avant correction. Le correctif utilise8Kio statiques pour les blocs normaux ;160comparaisonsPCM/états exactes,1000callbacks sans allocation,23/23capturesPC et buildsPC/Vita passent. **Non installé ni qualifié dans la scène Vita.** Candidat séparé `tester-crash-20261029/eboot-audio-scratch.bin` (SHAaab38e3eec8da8c1495ddf5430495c78f0c6ca416604b98c628655234187264b), bibliothèque normaleCG : ce n'est pas le candidat qualité4092. La source courante est donc plus récente que les mesures nocturnes. Garder les deux états distincts et intégrer/qualifier avant toute remise de version combinée.

La dernière restauration de console est vérifiée par le pilote, mais Valentin peut maintenant la reprendre : vérifier son usage avant tout nouveau lancement ou fermeture. Ne pas prolonger automatiquement l'autorisation nocturne sans tenir compte des messages récents.

**Contrôleur unique observé : `python -u build/room-audit/quality-20261029/resume-validation.py`, PID35732, session outil64186.** La reprise visuelle vient de finir : CPU5/5 et matériel5/5, cinq captures de chaque côté, originaux restaurés et vérifiés. Le contrôleur enchaîne `precision-vita-inner.py` ; vérifier `validation-owner.json` et sa commande réelle. **Les dix nouvelles images du lot précédent ne sont pas encore examinées.**

Consulter les fichiers frais :

- `build/room-audit/quality-20261029/validation-owner.json` ;
- `build/room-audit/quality-20261029/resume-validation.log` ;
- journal du child indiqué, dans le même dossier ;
- `build/room-audit/STOP_ALL` : aucun nouveau lot s'il existe.

Vérifier réellement les commandes des processus et l'avancement des journaux. **Ne pas lancer de deuxième scénario ni de commande Vita pendant ce contrôleur.** Anciens propriétaires18660/31632/9920 terminés ; leurs fichiers owner ne sont pas des verrous actuels. Aucun jeu PC actif au dernier contrôle.

Ordre du contrôleur courant :

1. `audio-small-learn.py` : **terminé**, deux profils complets, résultats ci-dessous.
2. `full-images-retry.py` : **terminé**, CPU5/5 et matériel5/5,180+240images, cinq captures de fin chacun, zéro diagnostic bloquant, restauration vérifiée. **Images à examiner**, ne pas annoncer encore leur validation visuelle.
3. `precision-vita-inner.py` : **en cours**,6destinations, DXT corrigé toujours actif, TEX32=0 puis1,180+240images/endshots ; vérifier bénéfice réel et marge mémoire.
4. `full-motion.py` :3destinations, vrais sticks injectés(PAD1, déplacement/caméra), HEAD0/SMALL0 puis HEAD1/SMALL1,180+600images/endshots.
5. `final-endurance.py` :4destinations×4retours, réglages combinés,600+600images/endshots16 ; suivre ressources par cycle.

Chaque enfant sauvegarde avant manipulation, puis restaure et relit les originaux dans son finally ; le parent s'arrête sur un échec. Une interruption externe peut empêcher le finally : si le propriétaire a disparu, préserver le dernier journal et récupérer depuis son dossier original vérifié **avant** de repartir. Ne pas réutiliser un dossier de preuves existant.

## Versions, sauvegarde et bibliothèque

| Élément | Identité SHA-256 |
|---|---|
| Binaire joueur d'avant la nuit, restauré entre lots | `c5f74b9db7f24d850fbd590488532acc519d85e4070056f2a6d1b85ea8d87761` |
| Sauvegarde originale protégée,694288octets | `f39b8e485113b0907332d809e37b8b8c6b9ead4f3bc5e2010debffe835f9fa20` |
| Candidat courant des derniers lots | `eboot-quality-tail-learn.bin` et `.velf`, SHA `40929950f6995b48a31160a6bd9fdf31157010da27c046bfc0b4c1667c90703a` |
| Ancien candidat figé | `eboot-quality-tail.bin`, SHA `cedb923490a2c78c9d0470614f8a06c18d2da6f9ef10c97d371b68ee03f74a7c` |
| VPK interne archivé | `sh4-quality-tail-internal.vpk`, SHA `e221a0d81d3c6b528ee8bef4836fdcdb833f68a5cb0059b70f29a21565e1e65d` ; contient **cedb**, pas4092 |

Ces candidats sont sous `build/room-audit/quality-20261029/`. Ne plus écraser un candidat mesuré. Nouveau code : nouveau nom/empreinte.

**Le nouveau profil complet n'est pas encore remis au joueur.** Entre les lots, retourC5/configuration normale ; pendant un lot, binaire/configuration de test. Aucun paquet testeur ni défaut public remplacé cette nuit.

Vita192.168.1.175, FTP1337, vitacompanion1338, applicationSILH00004. Quitter avant envoi d'eboot seulement après libération par le propriétaire autorisé. Restaurer uniquement `sh4.sav`, supprimer les concurrents `.bak/.new/.prev`, relire l'empreinte. OriginalJSON français, env de test absent. Pas de nouvelle préparation des données imposée cette nuit.

Build : toujours GUEST_MB200 et TRACE_CALLSOFF ; allocation réelle observée166Mio. NormalCMake actuellement restauré vers `/home/val/src/vitaGL-sh4-cgcache/libvitaGL.a`, SHA `613874a21eeddc7d3cdf33591b0eded6fffd3ce624daec76cebf23fa860566c3`, inchangé. Bibliothèque isolée corrigée : `/home/val/src/vitaGL-sh4-hwdxt-swizzle-20261029/libvitaGL.a`, SHA `2dfd9cc9eda3411c37777e959b472f1bf1818624a51913ecada0455ab6fc6f10`.

Patch reproductible : `port/patches/vitagl-sh4-dxt-blocks.patch`, après le patch de cache compilé, même révisionupstream. Build complet NO_DEBUG1/STORE_DEPTH_STENCIL1/HAVE_SHADER_CACHE1. SDK et autres bibliothèques non modifiés. **Ne pas installer aveuglément `build/sh4.vpk` ou les outputs du build normalCG en croyant y trouver le backendDXT corrigé.**

## Confirmé cette nuit

- Textures256→512 : détail visible amélioré, variation moyenne <0,2ms sur4premières scènes. Seuilalpha512 testé pour ne pas réduire involontairement la précision des transparences.
- DXT1 logiciel direct :483conversions,2369→1633msCPU cumulés(−31% **de conversion**, pas de fps), temporaire1Mio→512Kio, images contrôlées identiques. Source : défaut1Vita, `SH4_DXT16_NATIVE=0` repli ; PC0.
- PrévisionBGM : marge finale≤4096octets, même plafond. Deux ordres confirment6lectures~70–94ms→5–6ms dans l'intervalle mesuré. `SH4_PREFETCH_GUARD` défaut1Vita, =0repli ; PC0.
- DXT matériel corrigé : ancien défaut de chambre reproduit puis disparu dans les contrôles isolés.8destinations et2cinématiques pilotes contrôlées ; ne pas généraliser à tous les états ou aux anciens défauts v83.
- VRAM libre à qualité512 comparable : Hôpital28393→53646Kio ; Bâtiment22338→46055 ; Monde final23489→44265. Chargements~2,4–2,8s, pas de raccourcissement notable démontré.
- Résolution :20mesures complètes,960initial interrompu exclu puis repris. Intermédiaires/logiques au métro :~0,95en640 ;0,066en704 ;0,013en768 ;0,009en960. **Pas de hausse globale retenue.** Eau~36,5mslogiques en640 : pas30constants partout.
- HEAD borné : deux ordres, premières boucles87–89,5ms→5–6ms ; maxima123–126→82ms. Environ1Mio hôte supplémentaire, dans le plafond partagé original2×1310720octets. `SH4_AUDIO_HEAD_CACHE=1` opt-in, défaut0.
- SMALL avec historique corrigé, dernière paire complète : queues242Kio **40,1/45,5ms→1,6/1,9ms** ; maximum **80,4→58,1ms** ; moyenne33,322→33,331ms ; tas hôte17218→17219Kio. Boucles toujours5–6ms. Gain de pauses, pas de moyenneFPS. `SH4_PREFETCH_SMALL=1` opt-in, défaut0.
- PC actuel, REUSE1/HEAD1/SMALL1 après correction d'historique : **23/23captures identiques**, aucun fatal, sauvegardes restaurées. Dossier `quality-pc-regression-tail-learn-20261029`. Tests de copies/EOF, budgets concurrents, conversions et allocations refusées passent.
- Ancienne combinaison16retours : pas d'erreur,4/4premières captures exactes, tas invité stabilisé après premier cycle dans ce parcours. Elle **précède** les derniers caches/backend : ne remplace pas l'endurance finale.

## Écarté, incomplet et pièges

- Déclarations de sommets et bornes d'indices partagées : gains négligeables, prototypes retirés.
- Limite1024 : aucun niveau de plus dans7destinations ; pas de plafond512 global démontré. ScannerDDS33archives/2,44Go/0en-tête standard valide : ne permet pas d'inventorier le format propriétaire.
- RGBA8 logiciel global :~9,6Mio libres à l'hôpital, non retenu. DXT natif+précision des seules autres textures reste à qualifier ; surPC4destinations donnent8/8images identiques, donc aucun détail ajouté dans ces vues.
- PremierSMALL :43,0/44,5→1,4/44,8ms, seulement une queue corrigée. Version suivante apprend la queue comme nœud et ne lance aucun job après elle ; résultat complet ci-dessus.
- Premier contrôle d'images complet : erreur **du banc**, NO_SHOTS1 neutralisait END_SHOT1. CPU5/5arrivées mais0capture ; arrêt et restauration vérifiés. La reprise actuelle utilise NO_SHOTS0/END_SHOT1.
- Le compteur des intermédiaires n'est pas une mesure de l'affichage physique. Captures et chargements sont exclus des conclusions de cadence stabilisée. `[long]` ne détaille que12longues images par fenêtre ; `[io]` est plafonné.
- `SH4_IO_TAIL_PROFILE=1` est un diagnostic ; l'omettre chez le joueur. `SH4_HWDXT` teste encore la présence : même`=0` active le backend ; supprimer la variable pourOFF. Ne jamais activer avec l'ancienne bibliothèque.
- SDL installé inspecté : WaitThreadEnd puis DeleteThread. Pas de preuve justifiant un remplacement de ce nettoyage, ni de garantie d'absence de fuite ailleurs.
- Windowsrg : dossiers avec `-g '*.c'`, pas chemins `dossier/*.c`.

## Suite exacte

**Priorité actualisée :** les processus ci-dessus sont finis. Examiner les preuves restantes hors console, conserver le diagnostic du nouveau crash, puis intégrer le correctif audio à un candidat qualité explicitement identifié et le qualifier avant distribution. Les anciennes étapes1à3ci-dessous ne sont plus à relancer : leurs essais sont terminés, leur **analyse** reste à faire. Aucun VPK nouvellement corrigé n'a été envoyé au testeur.

1. Vérifier le propriétaire et laisser avancer la séquence saine sans lancer de doublon.
2. Examiner réellement les5paires du candidat complet et les6paires16/32bits ; consigner défauts, précision utile ou absence de bénéfice, et marge mémoire.
3. Vérifier mouvement/caméra et combinaisonHEAD1/SMALL1 ; examiner ensuite16retours, photos et ressources par cycle. Exiger `event=complete`, aucun timeout et les captures attendues : arrivée seule ne suffit pas.
4. Choisir le profil final selon ces preuves. Provisoirement :640x362, textures512/alpha512, DXT corrigé avec sa bibliothèque, optimisations utiles, HEAD/SMALL. **Pas encore un défaut public ni une qualification générale de stabilité.**
5. Archiver un VPK correspondant exactement au binaire final retenu (pas l'anciencedb), symboles et empreintes ; pas de publication automatique.
6. Avant remise au joueur : relire la sauvegarde originale, enlever audit/live/pilotage/entrées scriptées, configuration normale explicite, identifier le binaire et vérifier son lancement. Actualiser ce fichier, STATUS et le rapport, rendre bilan et limites. Ne pas promettre zérobug/30partout/60physiques.

Branche réellement ouverte au dernier contrôle : **`codex/prep-data`**, dernier commit **`8d09d1f`**, auteurValentin. Les mentions d'autres branches dans les comptes rendus décrivent leur époque ; ne pas changer de branche pour les suivre aveuglément. Les modifications courantes sont locales et ne sont pas toutes committées.

Le dépôt comporte beaucoup de WIP antérieur(UI/contrôles/mémoire/assets). Pas de reset/stash/add-all pour nettoyer un essai. Ne pas réencoder globalement le registre des changements : octets anciens nonUTF8. Source, candidat, installation, ZIPtesteur et publication sont des états distincts.

## 2026-10-29 — VERSION VALIDÉE COMME RÉFÉRENCE FONCTIONNELLE (décision de Valentin)

Valentin a décidé de valider l'état actuel comme version fonctionnelle de référence (« v1 fonctionnelle »). Contenu : candidat5 films (fluidité, eboot c15c0e5a…, VPK `build/room-audit/movie-fluidity-20261029/candidate-5/sh4vita.vpk`) + curseur téléphone au stick gauche/croix seulement + correctif stick droit des menus titre/pause. **Eboot installé sur Vita1 : `build/cursor-20261029/eboot-menu.bin`, SHA256 490f5f77693e55338196abd27484e062d263e0b8b2ad732d4f05d46920406689.** Repli : `build/cursor-20261029/eboot-before.bin` (c15c0e5a…). Validé à la main par Valentin : points de sauvegarde, veille/réveil, écran boutons (affichage seul), slider de sensibilité caméra, films, 30 fps, cinématiques. Les logs/dumps testeur du jour (3 dumps) = crash `sh4pf`/`_open_r` déjà corrigé en v83 ; rien de nouveau identifié. Source non committée (WIP Codex mêlé dans dinput.c/draw.c) ; aucun VPK final regroupant ces changements n'est encore construit ; Vita2 vierge à installer par Valentin pour une partie complète (preuve épuisement mémoire).

**Prochaine étape (après cette validation) :** réfléchir au bouton « Continue » du titre, toujours grisé (voulu : allumé dès qu'une sauvegarde existe, charge la dernière sauvegarde effectuée). Étape 1 = lecture seule du code du titre pour trouver la condition de grisage ; ne rien forcer sans équivalence avec « Charger ». Ne pas écraser cette version : tout essai Continue part de l'eboot ci-dessus.

**Commit et paquet de la version validée (2026-10-29, soir).** Commit local `188afc2` (auteur et committer vallfrr, sans co-auteur, **non poussé**) : tout le travail validé, y compris le WIP Codex. VPK final : `build/final-v1-20261029/sh4vita-v1.vpk`, SHA256 `0e5665ec70ff05344102d4ef39cc9a7c2a45a356fd0e2a8c4357e662d2a22605`, 511 entrées = VPK candidat5 (`0a90c20c…`) dont seul `eboot.bin` est remplacé par l'eboot validé `490f5f77…` (vérifié dans l'archive). Copié sur la Vita2 (lecteur USB I:) en `I:\data\sh4vita-v1-2026-10-29.vpk`, qui remplace `sh4vita-test-2026-10-29-b8d0.vpk` (supprimé à la demande de Valentin ; référence b8d0 toujours dans `build/`). Les données préparées (742 fichiers, 3,6 Go) sont déjà dans `I:\data\sh4` ; Valentin installe le VPK lui-même. Vita1 : `ux0:app/SILH00004/eboot.bin` relu par FTP = `490f5f77…`, donc déjà la version validée ; rien modifié.
