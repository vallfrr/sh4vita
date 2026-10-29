# Mémoire invitée et plantages après une longue session — essai du 27 octobre 2026

## Incident et cause encore ouverte

Les testeurs ont eu des arrêts à différents endroits après une session prolongée. Dans `sh4log(3).txt`, après environ 3 085 secondes, une allocation de tampon D3D de 3 776 octets échoue avec un tas invité à 145 Mio : `FATAL: D3D buffer: guest memory exhausted`. Le lieu où le jeu se trouvait n'est pas une cause démontrée. Le journal ne distingue pas une saturation réelle de la fragmentation ; un diagnostic du total libre et du plus grand bloc libre a été ajouté uniquement au chemin d'échec d'allocation, pour le prochain cas.

## Réserve fixe découverte dans le jeu GOG

La fonction GOG `0x551f80` réserve et initialise au démarrage une arène linéaire de 64 Mio, alignée sur 64 Kio. La fonction `0x552000` distribue ses blocs. L'option de diagnostic `SH4_ARENA_TRACE=1` enregistre le maximum utilisé. Dans le tour PC des 386 configurations de pièces, le maximum est de **47 463 Kio**. L'arène n'est donc pas entièrement utilisée par ce parcours, mais ce résultat ne couvre ni tous les états de l'histoire ni toutes les cinématiques.

Une option expérimentale `SH4_ARENA_MB=56` réduit la réserve de 8 Mio et garde environ 9,6 Mio de marge par rapport au maximum observé sur PC. Sans l'option, le chemin de l'exécutable GOG et sa limite de 64 Mio restent actifs. Le moteur graphique, la taille des textures, les commandes de dessin et le dossier préparé ne changent pas.

## Comparaisons terminées

| Vérification | 64 Mio | 56 Mio | Résultat |
|---|---:|---:|---|
| Tour PC de 386 configurations | Terminé | Terminé | Aucun échec d'allocation ; maximum de l'arène identique, 47 463 Kio |
| Pic du tas invité sur ce tour PC | 152 Mio | 145 Mio | Baisse proche des 8 Mio réservés en moins ; les scènes ne sont pas synchronisées image par image |
| Deux pièces PC reprises séparément | 4 captures | 4 captures | Captures identiques octet pour octet |
| Vita, trois pièces dans le même ordre, même eboot | Pic de 131 Mio | Pic de 123 Mio | Six captures identiques octet pour octet ; aucune erreur fatale, GL ou GXM relevée |
| Régression PC de référence | — | 23 captures | Aucune différence |
| PC, cent alternances entre deux zones | — | Terminé | Le tas revient à 114 Mio au dernier passage, comme au quarantième ; aucune hausse cumulative établie pour ces zones |

Le grand tour PC donne 577 captures identiques et 177 différentes entre deux exécutions. Il dérive aussi dans ses temps et certains états de jeu au fil des transitions ; les écarts les plus visibles disparaissent dans les deux reprises isolées. Ces 177 différences ne sont donc pas qualifiées comme des régressions graphiques, mais le grand tour ne permet pas non plus d'exclure à lui seul toute régression.

## Qualification encore nécessaire

Le tour Vita à 56 Mio a terminé ses 386 demandes en un seul lancement après 4 189 secondes ; neuf demandes ont expiré sans atteindre leur cible. Les 754 captures ont été récupérées, sans que leur seule présence garantisse leur contenu. Le pic de l'arène est de 47 465 Kio et celui du tas de 141 Mio. À l'entrée 10:11, deux demandes de surface de 1 Mio ont échoué, puis échoué à nouveau après récupération des libérations différées : 5 164 Kio étaient libres en 13 blocs, mais le plus grand ne faisait que 947 Kio. Le parcours ne s'est pas arrêté pour autant. Cette preuve de fragmentation indique un risque de ressources graphiques manquantes ; elle ne démontre pas la cause des anciens défauts de texture ou crashs des testeurs. Le candidat 56 Mio reste désactivé par défaut.

Les preuves sont sous `build/room-audit/arena-vita-v142/`. Un premier lancement de 55 pièces a été interrompu proprement car sa durée autorisée était insuffisante ; ses données sont conservées dans `arena-vita-v141/`. La sauvegarde originale, l'eboot et l'environnement normal ont été restaurés après le tour. L'empreinte vérifiée de la sauvegarde est `04315d28a0639804cf37c1c9336278a8ae46a300133ae1daf58c4c99dcccca38`.

Un défaut du compteur `g_heap_used` a ensuite été reproduit : une réallocation à zéro libère le bloc dans TLSF, mais le compteur comptabilisait ensuite ce bloc libéré. Dans un banc de 4 Mio, après libération d'un bloc de 1 Kio, il annonçait à tort 4 183 656 octets utilisés. Le suivi a été corrigé et le banc vérifie aussi un redimensionnement refusé et la réutilisation du bloc. Il n'est pas établi que ce chemin s'est produit pendant les incidents précédents : les valeurs de `used` et de pic antérieures restent donc à interpréter avec prudence. Les échecs de `tlsf_malloc` et le relevé indépendant des blocs libres du tour Vita sont, eux, des observations réelles. Le suivi optionnel des allocations a également été corrigé pour retirer les blocs libérés par `VirtualFree` et par réallocation à zéro.

Un banc PC facultatif permet maintenant de limiter le bloc invité à 166 Mio (`SH4_PC_GUEST_MB=166`), comme sur la Vita observée. Avec l'arène expérimentale de 56 Mio, le parcours PC v143 demande les 386 configurations en un lancement ; neuf expirent. Au passage 10:10, une demande de texture de 1 Mio échoue bien que 7 527 Kio soient libres : le plus grand bloc n'atteint que 984 Kio. Six demandes de ce type échouent au total, sans `FATAL` dans ce parcours. Cela reproduit la fragmentation constatée sur Vita dans un environnement où l'on peut chercher sans interrompre le joueur. Les allocations et chemins graphiques PC ne sont pas tous identiques à ceux de Vita ; cela ne prouve pas que le crash du testeur a suivi exactement ce chemin.

Une seconde exécution PC v144, avec compteur corrigé et cartographie des blocs au premier échec, termine également en un lancement avec neuf délais dépassés et cinq demandes de surface refusées. Le premier relevé montre environ 5 991 Kio libres, mais un plus grand bloc de 975 Kio. Parmi les blocs occupés apparaissent l'arène de 56 Mio et un bloc de 22,61 Mio à l'adresse attribuée au chargement de `sound/sound.pac`. Les petits trous se répartissent entre d'autres allocations toujours occupées ; le tas ne peut pas les réunir sans déplacer ces données. Les données brutes et les journaux sont dans `build/room-audit/heap-map-pc-v144/`. Une comparaison avec l'arène originale de 64 Mio est en cours ; ces réserves ne doivent pas être réduites ou libérées sans conserver le comportement du jeu.

Même un tour complet sans erreur ne reproduirait pas les gestes et l'état narratif des testeurs. Il faudra un essai de jeu normal prolongé, puis leurs retours avec le prochain VPK candidat, avant d'activer 56 Mio par défaut ou de présenter la panne comme corrigée. Si une allocation échoue encore, le nouveau diagnostic du tas permettra de choisir entre manque global, fragmentation et ressources différées.

## Journaux conservés entre lancements

Un nouveau candidat garde `sh4log.txt` pour la session active et 19 anciens journaux, `sh4log.txt.01` à `.19`, du plus récent au plus ancien. La rotation se fait au lancement ; elle conserve donc aussi le dernier journal après un crash. En cas d'échec de rotation, le journal courant est ouvert en ajout pour ne pas l'écraser. L'écriture Vita reste asynchrone : un arrêt brutal peut perdre les messages encore en attente. La logique a passé un essai sur 25 lancements et un essai d'échec de rotation sur PC. Sur Vita, deux démarrages du candidat ont confirmé que le journal précédent est conservé octet pour octet. Le premier contrôle avait lu trop tôt le journal de l'ancien binaire ; le contrôle corrigé a attendu explicitement le démarrage du candidat.

Le candidat a été installé par remplacement vérifié de l'eboot, les autres fichiers du VPK étant identiques aux paquets v83 et v131. Son démarrage, la présence du journal précédent et l'empreinte de la sauvegarde ont été vérifiés ; il reste expérimental et ne corrige pas à lui seul la fragmentation observée. Son VPK est conservé localement dans `build/room-audit/log-candidate/sh4.vpk`.

## Piste vitaGL à 28 Mio — 28 octobre

Une compilation expérimentale laissant 28 Mio au lieu de 32 Mio pour la RAM de vitaGL accorde 170 Mio au bloc invité sur cette console, contre 166 Mio auparavant ; la mémoire libre annoncée par vitaGL passe de 10 034 à 5 938 Kio. L'essai de quatre destinations a atteint l'appartement, le métro et l'hôpital, puis a expiré pour 10:10 alors que le jeu était encore en 5:15. La première capture de l'hôpital est noire pendant une transition, la seconde montre une scène avec Henry. Ce parcours ne démontre pas que la réserve réduite est suffisante pendant toutes les cinématiques et ne démontre pas que les crashs de longue durée sont réglés. Garder 32 Mio par défaut tant que ce risque n'est pas qualifié. Preuves : `build/room-audit/vgl28-20261028/`.

Le parcours PC de comparaison à 170 Mio (`build/room-audit/heap-map-pc-170-20261028/`) a été interrompu en une seule session à la demande 136 par `FATAL: indirect call to 0x00000000`, pendant une transition automatique où l'état observé était 1:0 ; le tas mesuré était d'environ 114 Mio. Ce n'est pas l'échec de mémoire observé dans les journaux des testeurs. La reprise automatique a été arrêtée car elle ne permettait plus une comparaison sur une session continue. Ce test ne valide donc ni les 4 Mio supplémentaires ni une correction de crash. L'origine de cet appel nul reste à établir ; le parcours de téléportation lui-même peut produire des états d'histoire incomplets.
