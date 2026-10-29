# Mémoire après les vidéos — candidat du 29 octobre 2026

Le candidat précédent reste `27d4ff04…`. Les mesures de sa campagne ne sont pas attribuées au nouveau binaire.

## Défaut caractérisé

`rt_new_cpu(1 MiB)` réserve une pile invitée et un TIB de 4 Kio pour le thread de sortie vidéo. Le lecteur joint correctement ses threads à l’arrêt, mais ne libérait pas ce contexte. Le nettoyage du graphe suivant libérait les décodeurs et les slots YUV, sans rendre cette pile. Un banc utilisant les fonctions de production conserve **16 842 752 octets invités après 16 cycles**. Il établit ce chemin de rétention ; il ne permet pas d’attribuer tous les dumps des testeurs à cette fuite.

## Changement et anciennes valeurs conservées

| Élément | Avant | Candidat |
|---|---|---|
| Pile du décodeur | 1 Mio par création, non libérée | même taille pendant lecture ; libérée après les trois `join` |
| TIB du décodeur | 4 Kio par création, non libéré | même taille ; rendu avec la pile |
| Cinq slots YUV | conservés après Stop jusqu’au prochain graphe | libérés après Stop ; Pause les conserve |
| État MPEG, position, durée | conservé jusqu’au prochain graphe | politique conservée |
| Objets COM du graphe | rétention résiduelle | non traitée dans ce changement |
| Qualité | textures 512, rendu 640×362, DXT corrigé | identique |

Les adresses des allocations appartiennent à une structure hôte qui commence par `CPU` ; le layout de `CPU`, utilisé par le code généré, reste inchangé. La libération ne reconstruit pas les adresses à partir d’un TIB modifiable par le jeu. Le CPU principal statique n’est jamais libéré. Les allocations du contexte sont vérifiées avec rollback avant diagnostic fatal.

À 640×480, les cinq slots YUV occupent théoriquement 2 304 000 octets hôte. Le gain effectivement rendu sur Vita doit être mesuré ; ne pas confondre tas invité, tas hôte et VRAM.

## Vérifications réalisées avant le matériel

- 2 000 durées de vie de CPU secondaires : libérations uniques, CPU principal préservé, TIB modifié sans perte de propriété, échecs allocation hôte/TIB/pile.
- 1 000 cycles de méthodes vidéo de production : Pause conserve, Stop joint avant libération, arrêt/nettoyage répétés sans double libération.
- Ordonnanceur de laboratoire : séquence de films sans réentrance pendant les Present récursifs ; aucun travail supplémentaire dans un lancement ordinaire sans ce drapeau.
- Builds PC et Vita réussis ; **23/23 images de référence PC identiques**.
- Huit films courts alternés dans un seul processus PC, retour après chaque et reprise finale : terminés. Les derniers relevés précis de tas sont absents car le tour d’audit avait expiré durant un film ; les résumés de tas restent à 108 Mio. Cela constitue une vérification de lecture répétée, pas huit mesures précises de tas. Le scénario matériel allonge le timeout.

## Candidat figé

Dossier local : `build/room-audit/fresh-combined-20261029/movie-lifetime-candidate/final/`.

- Eboot : `b8d0c09f804ac2b0ff58a53c6516b313519985cbcd60f2188d6ac12d8151d7c0`.
- VPK : `6ff9892a2c4f3b6a71b6b0f3a634a286eed1b9c7aa09cc2c746ca1275bc78d1f`.
- PC : `e6b3b3a2bd3d0dcf0f662be29b8007c6689dbdb23cba64f7276e377688a0da08`.

Seul `eboot.bin` change parmi les 511 entrées du VPK. Le préparateur et les données préparées ne changent pas. Repli matériel : eboot combiné `27d4ff04…`, VPK `db2f5a15…`, archives intactes dans la campagne. Les intermédiaires `448f…` ne sont pas à distribuer.

## Qualification matérielle requise

Le pilote attend la libération du propriétaire de la première Vita. Il lit un film court sur l’ancien binaire, puis installe le candidat : huit lectures dans un seul processus, captures précoces et mesures de tas après chaque retour, puis une cinématique d’Henry/Cynthia dans la bonne salle. Arrêt et repli au premier diagnostic bloquant. Revue des images après collecte.

**À la rédaction : candidat non qualifié sur Vita, non déposé sur la seconde console.** Ses 742 fichiers de données sont déjà copiés et relus par SHA-256 sur I:. Valentin installera le VPK dans VitaShell après sortie USB. Ce document doit recevoir le résultat matériel avant remise du paquet.


## Fluidité des films : chantier distinct demandé par Valentin

Les14en-têtesMPEG annoncent29,97images/s ; sur le candidat27d4, les moyenneslivrées vont de25,475à29,818images/s et les minima des fenêtres~2s de23,3à28,4. Ces pertes ne doivent pas être justifiées par un film sourceà24fps. Elles restent à traiter séparément de la fuiteCPU. Les captures peuvent ralentir ponctuellement le pipeline, mais ne suffisent pas à expliquer tous les relevés.

Pistesissuesducode, **sans modification retenue** :

- `push_frame` effectue deuxconversions du mêmeYUV, BGRApourle rendererinvité puisRGB565pourl’uploadVita. Étudier une conversionfusionnée à sortiesidentiques plutôt que réduire la définition desfilms. Vérifier touteslescouleurs/strides puis mesurer matériellement.
- Les framesB sont sautées si le décodage est enretard danspl_mpeg. Ce choix précédait la file dédiée de sortie ; contrôler la régularité avec/sans, en conservant synchronisationaudio et latence. Désactiver le saut sans mesure peut simplement déplacer le retard.
- Décodagecore1, sortie+audiofilmcore2, jeuprincipalcore0 ; mesurer tempsCPU et attentes avec le rendu concurrent avant de déplacer les threads.
- Lecturesvidéo débufferisées déjà présentes pour éviter les rechargesnewlib1Kio ; ne pas remettre une correction déjàfaite ou additionner des compteursimbriqués.

Le VPKb8d0 ne prétend pas corriger ces pertesd’images. La demande de remise d’un candidatmémoire vérifié sur la secondeVita reste prioritaire ; nouvelleopti vidéo doit produire un candidatet une comparaison distincts.

## Analyse du pipeline vidéo, sans changement de cadence retenu

Sur les14films, les images effectivement décodées et les imagesB sautées totalisent environ29,97/s : la perte correspond principalement à la politique de rattrapage du lecteur. Exemples de moyennes : film6=29,82décodées+0,16sautées ; film7=25,60+4,48 ; film11=25,92+4,07. Ce sont les compteurs du lecteur, pas une mesure externe de l’écran. La conversion double et le rattrapageB sont des pistes, pas des gains démontrés. Les fenêtres contiennent les captures de diagnostic.

Le compteurI/O est partagé entre les threads vidéo/audio et ses durées sont incluses dans d’autres durées murales ; ne pas les additionner ni les traiter comme des pourcentagesCPU exclusifs. Le lecteur possède deux flux sur le même fichier pour séparer les démultiplexages audio/vidéo. Avant toute refonte, séparer les mesures et comparer un parcours sans captures. Preuves : `movie-pipeline-analysis.json`.

La première qualification a été refusée au démarrage par la garde des paramètres : timeout30000 < settle90+dwell30000. Ancienbinaire restauré/relu ; aucune vidéo du nouveau binaire n’avait démarré. Reprise avec32000, preuves précédentes conservées, référenceancienne nonrejouée.


## Résultat matériel partiel : huit lectures terminées

Le candidat b8d0 a terminé huit lectures alternées 7/8 dans un seul processus Vita : 419,50 s pour le scénario et la collecte, huit retours, huit captures relues et examinées, aucun nouveau dump ni diagnostic bloquant signalé. Après retour, tas invité (Kio) : 109667, 109668, 109669, 109671, 109672, 109673, 109674, 109675. Soit +8 Kio entre premier et huitième retour, sans accumulation de sept piles de 1 Mio. Tas hôte : 13753 à 13785 Kio, soit +32 Kio. La rétention résiduelle du graphe COM reste ouverte.

Les huit images précoces montrent les films 7 et 8 sans corruption évidente. Les instants diffèrent : comparaison qualitative, pas identité pixel à pixel. La référence ancien film7 rendait 15991 Kio hôte après lecture, contre 13753 Kio sur le nouveau scénario ; les protocoles d'audit ne sont pas identiques, cette différence n'est donc pas un gain isolé strict. Le dégagement théorique des cinq slots reste 2304000 octets.

La cinématique 3D de contrôle et le nettoyage final sont encore en cours à cette rédaction. Aucun VPK remis à la seconde Vita avant leur résultat et la revue des images.


## Qualification finale et remise — 19 h 01

Cynthia ID6 dans2:24 a terminé en273,36s,15captures examinées sans corruption évidente. Les120images de séjour mesurées après le clip donnent moyenne33,332ms, P9533,857ms, max34,231ms ; ce n'est pas la distribution de toute la cinématique. Aucun diagnostic bloquant ni nouveau dump. Le lancement normal suivant a été relu : b8d0 installé, pas d'audit lourd ni fichier d'entrées scriptées. Première Vita disponible pour jeu manuel, vérifier son usage avant tout nouveau test.

Le candidat a donc passé cette qualification ciblée, sans promesse de stabilité universelle ni de FPS vidéo corrigé. La rétentionCOM résiduelle et le risque de réutilisation de slot identifié sur banc hôte sont distincts et restent ouverts. Le ZIP privé `sh4vita-test-2026-10-29-q512-video-memory.zip` contient ce VPK, les trois mêmes préparateurs et les instructions ; SHA25625b8697bb8cff08246798d4bb98f623414d3c843d22eaf28970474eccde34d30. VPK copié/relu surI:/data/sh4vita-test-2026-10-29-b8d0.vpk ; installation manuelle VitaShell encore à faire par Valentin.
