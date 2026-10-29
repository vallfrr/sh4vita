# Stabilité — référence expérimentale du 28 octobre

La référence distribuée aux testeurs est `sh4vita-test-2026-10-28.zip`, SHA-256 `96c36c71409509f178aa18bb5ad56466fde02ad2abf8c8b90a512a15930b3cf5`. Son eboot a pour empreinte `2fb943a4a190e8bcc956c93bd3c78ec67b73707791944afe7c03f53bbd1c6b8a`. Une archive des sources effectives et un manifeste sont conservés hors Git dans `build/room-audit/baseline-20261028/`. Cette archive inclut les modifications locales présentes au moment du paquet ; HEAD seul ne décrit donc pas ce binaire.

## Observation prolongée

Le journal récupéré avant les essais couvre environ 6 870 secondes. Ses 229 relevés de mémoire invitée atteignent 133 072 Kio et restent constants sur les derniers relevés. Aucun échec d'allocation ni `FATAL` n'y apparaît. Cela couvre cette session et ne reproduit pas les nombreux changements de scène des testeurs.

## Création des tampons audio en cas de pénurie

`IDirectSound::CreateSoundBuffer` utilisait le résultat de `gheap_calloc` sans vérifier son succès et renvoyait `HR_OK` même si le stockage invité était nul. Le compteur audio était également incrémenté. L'allocation hôte du compteur partagé n'était pas vérifiée non plus. Ce comportement peut laisser le jeu continuer avec un tampon audio invalide après épuisement de mémoire ; aucun lien direct avec les dumps reçus n'est établi.

Le nouveau candidat remet la sortie à zéro et renvoie `E_OUTOFMEMORY` sur chacun de ces échecs. Les allocations déjà effectuées sont libérées avant retour. Le chemin normal conserve la même création et les mêmes données. Un échec de l'allocation hôte de la structure est aussi traité pour la création et la duplication.

`port/test/dsound_allocation_test.py` compile la fonction de production et injecte successivement trois échecs : structure hôte, stockage invité et compteur partagé. Il vérifie l'erreur, la sortie nulle, l'absence de publication de l'objet et de stockage invité restant, puis le succès normal. Le test passe, ainsi que les compilations PC et Vita. Cette modification n'a pas remplacé le ZIP distribué ni le binaire de référence utilisé pour le parcours Vita.

## Comparaison ciblée en cours

Un parcours de 24 demandes répète appartement, hôpital, tunnels et métro avec l'arène originale de 64 Mio. Il doit aider à distinguer accumulation et fragmentation dans un lancement continu. Vita utilise l'eboot de référence ; PC utilise les sources courantes avec une limite invitée de 166 Mio. Ce sont des chemins de rendu différents et des téléportations, donc aucun résultat ne remplace une partie normale. Les résultats, délais dépassés et éventuelles reprises doivent être examinés avant conclusion.

La sauvegarde est copiée deux fois avant essai et restaurée avec vérification d'empreinte ; le script Vita retire ses paramètres de test et relance la configuration normale dans son bloc de restauration.

## Résultats des premières reprises

Le parcours incluant les tunnels a été interrompu : la troisième destination a expiré dans un état incorrect, puis PC a quitté avec une violation d'accès et Vita a cessé de progresser pendant la quatrième demande. Cette activation artificielle n'est pas une reproduction qualifiée des crashs de jeu normal. Les reprises automatiques ont été arrêtées et la sauvegarde a été restaurée.

Le parcours suivant répète seulement 1:1, 5:15 et 2:19, quatre fois. Les douze demandes sont terminées en un lancement sur PC (134 s) et Vita (313 s), avec un délai dépassé au premier 2:19 sur chaque plateforme. Il n'y a ni refus d'allocation ni erreur fatale dans cette série. Pour les trois derniers cycles, les valeurs stabilisées sont respectivement 121 227 / 120 727 / 119 016 Kio sur PC et 119 870 / 119 372 / 117 662 Kio sur Vita. Elles reviennent aux mêmes niveaux : aucune accumulation n'est reproduite dans ces pièces après stabilisation. Cela ne suffit pas à exclure une fuite dans d'autres chargements.

Les preuves sont dans `build/room-audit/memory-rooms[-pc]-20261028/`. La Vita a été relancée avec son binaire de référence, sans paramètres de test, après restauration vérifiée de la sauvegarde `f39b8e485113b0907332d809e37b8b8c6b9ead4f3bc5e2010debffe835f9fa20`.

## Autre garde-fou pour les cinématiques

La fonction `IPin::QueryId` de la couche cinématiques écrivait une chaîne UTF-16 à l'adresse invitée zéro si son allocation échouait. Elle renvoie désormais `E_OUTOFMEMORY` et un pointeur de sortie nul dans ce cas. `port/test/movie_queryid_test.py` vérifie le chemin d'échec sans écriture à zéro et la chaîne obtenue en cas de succès. Le code compile sur PC et Vita. Ce défaut n'est pas identifié comme la cause d'un crash de testeur et le correctif n'est pas encore validé en jeu sur Vita.

La carte du tas du parcours v144 confirme 5 991 Kio libres en 13 blocs, le plus grand de 975 Kio, au refus d'une allocation de 1 024 Kio. L'arène expérimentale de 56 Mio et l'archive audio de 22,61 Mio représentent deux gros blocs occupés. Réduire ou déplacer ces blocs sans qualifier les scénarios modifierait le comportement du jeu ; aucune optimisation de mémoire n'est retenue sur cette seule observation.

Les tampons d'échantillons vidéo avaient aussi deux risques : une allocation invitée refusée laissait publier un tampon nul ; une seconde demande plus grande pouvait augmenter la taille annoncée sans agrandir les tampons déjà créés. Un candidat réserve tous les nouveaux blocs avant de libérer les anciens, conserve les anciens si une réservation échoue, et refuse la modification si un échantillon est encore occupé. Les tests `movie_sample_reserve_test.c` et `movie_setproperties_test.py` couvrent les redimensionnements, les échecs et l'état occupé. L'ouverture vidéo et la régression PC de référence ont terminé avec 23 captures identiques ; le délai de 250 secondes est attendu pour ce script. La compilation PC et Vita passe. Cela ne prouve pas que les crashs des testeurs viennent de là.

Sur Vita, la vidéo d'ouverture a été lue et les deux cinématiques moteur du métro 1 et 8 ont terminé. Les 18 captures de ce parcours sont identiques aux captures `model-qualified-v102/vita-cinematics` ; aucune erreur fatale ni allocation refusée n'a été relevée. Cette comparaison ne couvre pas toutes les vidéos. La sauvegarde, le binaire de référence et la configuration normale ont été restaurés et vérifiés dans `build/room-audit/movie-safety-vita-20261028/restored.json`. L'agrandissement transactionnel nécessite temporairement de la mémoire supplémentaire : les tests ne prouvent pas qu'il réussira si le tas est déjà presque épuisé, mais un échec conserve les anciens blocs et renvoie une erreur.

La fabrication des objets COM ne vérifiait pas non plus l'allocation du petit objet invité et de sa table de méthodes. Une pénurie pouvait donc écrire à l'adresse zéro. Le candidat interrompt maintenant ce chemin avec un message fatal explicite donnant l'interface et la taille ; l'objet n'est publié dans la table qu'après son initialisation. Cela améliore le diagnostic et évite l'écriture invalide, mais ne récupère pas la mémoire manquante et ne garantit pas la poursuite du jeu. `com_allocation_test.py` injecte les deux échecs et vérifie l'absence d'écriture à zéro, puis le chemin normal et la réutilisation de la table de méthodes. Les builds PC et Vita passent.

## Durée de vie des interfaces audio — candidat

Les objets `IDirectSound3DBuffer8` et `IDirectSoundNotify8` gardaient un pointeur sur leur tampon parent sans en conserver de référence. Si la référence du tampon était libérée avant les interfaces, celles-ci pouvaient utiliser un objet détruit. Le candidat ajoute une référence du parent à la création de chaque interface et la relâche au dernier `Release` de cette interface ; la destruction du parent et la libération du stockage passent par le même service que la libération normale.

`dsound_lifetime_test.py` utilise les fonctions de production, exerce trois ordres de libération avec deux interfaces, et vérifie une seule libération du stockage par tampon. Son contrôle négatif retire le maintien de référence et échoue comme attendu. Le banc d'échec d'allocation audio passe toujours, et la compilation Vita passe. Il n'est pas établi que cet ordre de libération a provoqué un crash des testeurs. Un parcours Vita est en cours avant de retenir le changement pour un candidat distribué.

Le parcours Vita suivant a terminé prison, Past et métro dans un lancement : six captures identiques à la sonde précédente, même dernier relevé audio de 10 Mio et aucun refus d'allocation ni erreur fatale relevé. Les fichiers sont dans `build/room-audit/audio-lifetime-vita-20261028/`. Le binaire de référence et la sauvegarde ont été restaurés et leur SHA-256 vérifié. Ce parcours ne couvre pas tous les ordres de libération possibles en jeu.

## Session continue PC à 166 Mio — panne reproduite

Le binaire courant avant correction de durée de vie audio, avec l'arène originale de 64 Mio et les gardes de pénurie, a échoué après 1 236 s et 308 demandes de pièces dans un seul processus. Sept destinations avaient expiré auparavant : ce parcours artificiel n'est pas une partie normale et ses états narratifs ne sont pas tous qualifiés. La panne mémoire est toutefois explicite : les demandes de 128 Kio, 32 Kio, 8 Kio et 2 Kio sont refusées, même après attente des libérations différées. La carte au premier refus ne contient que 800 octets libres ; le dernier objet COM de 16 octets échoue ensuite.

Le relevé précédent indique 25 Mio de textures, 6 Mio de sommets/indices et 10 Mio de tampons audio. Les allocations du jeu comprennent l'arène de 64 Mio, la réserve audio de 22,61 Mio, un bloc d'environ 2,2 Mio du lecteur audio et un autre de 2 Mio. Cela reproduit une saturation réelle, pas seulement la fragmentation de l'essai antérieur. Aucun lien direct avec le crash d'un testeur précis n'est encore établi.

Les preuves et l'empreinte du binaire sont sous `build/room-audit/sound-pressure-pc-20261028/`. L'option `room_runner --no-restart` a conservé l'arrêt à la première panne, sans masquer le résultat par un autre processus. Un contrôle de la destination 7:2 dans un processus neuf avec le même binaire est en cours pour vérifier l'effet de l'historique de chargement.

Le contrôle direct 7:2 dans un processus neuf a terminé avec environ 119 Mio occupés (15 Mio textures, 3 Mio sommets/indices, 1 Mio audio), sans échec. Son empreinte de binaire est exactement celle du grand parcours (`49de77b05f1d8c97f674198a6b5c35b20fa7e17446a720e1b42158d073016b77`). La paire locale 7:1 → 7:2 n'a pas été qualifiée : après une arrivée valide en 7:1, l'état a dérivé vers 7:4 et la demande suivante a expiré. Elle atteint environ 140 Mio sans refus. Ces différences d'histoire et de transition empêchent de conclure que les 26 Mio d'écart sont une fuite pure. Elles orientent l'analyse vers la rétention des ressources et les pics de coexistence, en plus du budget fixe.

La régression PC après les gardes COM et la correction des références audio termine avec les 23 captures de référence identiques (`sound-residency-20261028/pc-final-regress.txt`, fin sur délai 124 attendu).
