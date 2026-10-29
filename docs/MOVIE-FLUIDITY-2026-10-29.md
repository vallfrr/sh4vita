# Fluidité des films — essais du 29 octobre 2026

Objectif : respecter les 29,97 images/s des MPEG d'origine, sans baisse de qualité, retard accumulé ni désynchronisation sonore. Les présentations du moteur et les images intermédiaires ne constituent pas un compteur des nouvelles images vidéo. Le candidat5 est installé normalement sur Vita1 après deux films de contrôle. Il améliore nettement les séquences mesurées ; des intervalles isolés jusqu'à63ms subsistent. Référence b8d0 conservée ; autres films non rejoués sur Vita conformément au choix de Valentin.

| Essai | Décodage/livraison | Sortie observée | Décision |
|---|---|---|---|
| b8d0, sans audit ni captures, films7/11 |25,55 /25,40fps en moyenne | ancienscompteurs insuffisants pour tous les retards | référence |
| Cache512Kio+toutesimages, core1 priorité normale | proche30fps | irrégulière, parfois13–22/s | non retenu |
| Même lecteur, core0 priorité normale |29,96 /29,97fps |23,63 /21,73fps ; pointes100,81 /106,47ms ; audio en avance~100ms | non retenu |
| Candidat5 : core0 / prioritéLOW / cache / toutesimages par défaut |29,875 /29,957fps livrées |29,917 /29,962 nouvellesimages/s hors démarrage ; maximum61,91 /62,64ms ; aucun intervalle≥100ms | retenu pour ces deux essais, installé Vita1 |

Le cache est borné à quatre blocs de128Kio, partagé par les deux démultiplexages du même fichier. La politique de sautB est supprimée si ce cache est disponible ; un refus d'allocation reprend le chemin ancien. La fileYUV utilise de vrais identifiants de slots et protège celui qu'un consommateur traite encore. Aucun changement des pixels, dimensions640×480, conversions couleur, compression ou audio n'a été introduit. Les réglages cache/toutesimages/core/priorité permettent les comparaisons ; les anciennes valeurs sont cacheOFF,toutesimagesOFF,core1,prioritéSDLnormale0xa0. La prioritéLOW du SDK installé vaut0xbf, vérifiée dans son implémentation locale.

Preuves dans build/room-audit/movie-fluidity-20261029 : snapshots before/, candidats figés, backups originaux, journaux et paired-analysis.json. Le premier ancieneboot pris par erreur a été rejeté et la référence restaurée ; son essai est exclu. Les nouveaux eboots sont extraits duVPK puis comparés au fichier de compilationWSL.

Vérifications : 8000comparaisons concurrentes des lectures/seek/EOF ; deux lecteurs séquentiels ne lisent qu'une fois le volume du fichier sur disque.632916paquetsMPEG identiques sur14films,avecPTS,durées etrembobinage.4985imagesYUV/PTS identiques au décodage séquentiel complet.100000interleavingsFIFO sans réécriture du slotconsommé.1000cyclesStop/Pause/nettoyage.10000comparaisonsPCM/états pour la simplehorloge de diagnostic.Candidatfinal :23/23imagesPCréférence identiques, sauvegardePC restaurée.

Les nouvelles traces movie-flow mesurent les prises de nouvelles textures, leurs intervalles, le temps d'attente dans la file et une estimation audio-minus-video basée sur les échantillons réellement consommés par le mélangeur. Elles ne mesurent pas le scanout ni la latence physique des haut-parleurs, et l'horloge sonore est quantifiée par les callbacksSDL.


## Fin du lot et limites de remise

Candidat5 c15c0e5a… installé normalement surVita1 ; sauvegardee5074a… conservée et relue. Preuvesnormal-final/complete.json etqualification.json. Deux captures examinées séparément, sans corruption évidente, et une sortie anticipée du film11 observée ; pas une validation exhaustive de tous les skips. Les mesures de cadence excluent ces captures. Le lot14a été arrêté avant premierfilm sur demande explicite ; ne pas le relancer.

Aucun nouveau ZIP fluidité ou mise àjourVita2 n'a été effectué avant l'interruption demandée. VPK prêt :candidate-5/sh4vita.vpk, SHA0a90c20c3b233dca6e0cc822ddf74c3c134e29c6dd7a61e032a503c4caf048f4. Seul eboot change parmi511entrées. Données et préparateurs inchangés. Les compteurs de prises de textures ne mesurent pas le scanout ; audio-minus-video35,71–64,99ms est une estimation par callbacks, pas une garantie physique de synchronisation. Les irrégularités restantes et les autres12films restent à confirmer.
