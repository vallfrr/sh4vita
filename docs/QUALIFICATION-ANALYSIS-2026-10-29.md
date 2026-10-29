# Analyse de la qualification combinée —29octobre2026

État de travail, pas un certificat de stabilité. Candidat figé27d4ff04, textures512/rendu640×362. Les mesures instrumentées ne représentent pas une partie complète ni une garantie30fps. Identités, installation neuve et propriétaires dans [HANDOFF.md](HANDOFF.md).

## Endurance continue2h

Valentin a ramené la durée minimale de3h à2h. Un relais a attendu ce seuil puis demandé un arrêt propre :7222,43secondes,90visites terminées des quatre destinations,162991images logiques de séjour mesurées. Aucune erreur bloquante ni core collecté. Les messages bruts de minimum3h/192visites nonatteints sont conservés ; ils correspondent à l'arrêt demandé, pas à un crash. Sources locales : `two-hour-endurance.json`, `continuous-soak/result.json`.

Temps moyens37,149ms, P9546,427ms, maximum285,946ms ;147images>100ms. Tas hôte pic9703Kio, VRAM libre minimale52243Kio. Après les premières visites, comparaison à salle identique : tas hôte+7à42Kio selon salle, mémoire invitée+512/513Kio ; variationsVRAM de-212à+393Kio. Il n'y a pas de remplissage hôte comparable aux23Mio du testeur dans ce parcours, mais la croissance invitée et les parcours différents empêchent de proclamer toute fuite supprimée.

Les90captures ont été examinées sur8planches. Henry et les acteurs visibles restent texturés ; pas de corruption évidente dans ces vues. Limites : caméra régulièrement masquée par des ennemis dans l'Eau, vue du Passé presque fixe contre un obstacle, instantanés et non examen de toutes les images. Les déplacements injectés ne constituent pas un parcours pédestre complet.

## Grandes pauses : pisteI/O documentée

Répartition des147images>100ms :114enEau4:11,19hôpital7:32,14Passé8:24.143contiennent au moins20ms d'I/O,133au moins50ms. Le compteurbody inclut ces lectures : **ne pas additionner body+io ni désignerbody comme une cause indépendante**.

Exemples : frame4188,105,840ms, dont66,992msI/O ; lecture à la frame4187 de614400octets dans `demo/se.sh4`, offset241471488,67ms, ahead=0. Même bloc aux frames4458et5582,64,7/64,1ms. Les traces individuelles sont limitées ;20des147images ont une lecture explicite associable à±1frame, les autres gardent seulement le compteur agrégé. Le décalage de1frame est dû au moment d'observation, pas à deux pauses indépendantes.

Le journal montre des répétitions du même blocSE600Kio (39occurrences dans les lectures enregistrées), d'un bloc de`demo/cm.sh4`327680octets (40), et d'un blocBGM555008octets (40). Ces comptes incluent les lectures journalisées de l'ensemble du parcours, chargements compris ; ils ne signifient pas39ou40images de séjour défectueuses. Cela justifie d'étudier un cache borné des blocsSE/dialogue réutilisés, **sans modifier le candidat au milieu de la qualification**. Le cache de préfixeBGM seul ne couvre pasSE. Réduction de pause à mesurer sur un candidat séparé ; aucun gain encore annoncé.

Preuves : `endurance-long-frames.json`, `endurance-adjacent-io.json`, `endurance-file-io.json`, `endurance-room-analysis.json` sous le dossier de campagne.

## Couverture des ressources

386cas tentés,381mesurés,762captures recueillies ;5contextes changent de salle avant fin de séjour :3:21revisite,4:39revisite,5:15revisite,6:21,7:2. Ce sont des cas nonqualifiés, pas des crashs identifiés. Les deux premières passes abandonnées restent dans les preuves et ne sont pas converties en succès.

La revue est suivie par page dans `visual-review/manifest.json`. Au point de rédaction :762/762captures de couverture vues,90/90d'endurance vues. Aucun nouveau défaut évident dans les vues examinées. Plusieurs scènes sont cadrées derrière un mur/au point d'origine, certaines déclenchent leurs événements natifs : identité chargée et captures valides ne prouvent pas toute la géométrie ni l'état narratif. Les autres captures et les cinématiques sont encore à examiner.

## Audio : inventaire et première vérification sur les vraies données

`tools/audio_catalog.py` a inventorié13archives locales :355WAVPCMmono22050Hz16bits,696entrées candidates à en-tête audio du jeu,182fluxcinéma et263entrées vides. Les696candidats ne sont pas déclarés décodés/joués ; les formats et conditions doivent être rapprochés des lecteurs d'origine. Les sons des vidéos restent une famille séparée. Aucune donnée audio n'est copiée dansGit.

Nouveau test `port/test/dsound_real_bank_test.py` : les355chargesPCM déclarées du bank sont mélangées intégralement avec le mélangeur du paquet ancien et celui corrigé. **25347paires de callbacks,103821312octets de sortie identiques** ; états des tampons identiques. C'est une vérificationhôte des vraies données, **pas355lectures surVita ni preuve d'un son audible correct**.

Piège du format original :25déclarationsdata dépassent leur enregistrementSDPA ; le dernier dépasse le fichier de2octets dans la queue de page virtuelle mise àzéro. Le premier banc, trop strict sur la frontière de l'enregistrement puis sur celle du fichier, a échoué. Il a été corrigé pour reproduire la banque résidente et sa queue de page, en conservant les limites. Aucune modification des assets ni changement du chargeur n'a été fait sur cette base.312taillesRIFF déclarées diffèrent de la taille d'entrée : inventaire explicite, pas assimilation à une corruption du transfert.

## Reste à faire

Un relais ponctuel `movies-qualification.py` est préparé : après succès et sortie du pilotecinéma courant, il utilise le lecteur original déjà intégré auVPK pour lire les14filmsMPEG, exige une fin de flux et observe90images après le retour. Même candidat et configuration de qualité, pas de nouveau code de jeu. Ce lot est distinct des182cinématiques du moteur et des696entréesaudio candidates ; aucune réussite des14films n'est annoncée avant leurs résultats.

Finir les cinématiques communes qualifiées surPC (20IDs, lotVita en cours), examiner leurs captures, traiter les contextes restants sans forcer des états arbitraires ; terminer la revue de couverture. Puis qualifier la lecture/décodage des autres ressourcesaudio et leur exercice surVita avec mémoire bornée. La simple présence dans le catalogue ne compte pas comme chargement ou lecture réussie. Toute modification du moteur pour ce laboratoire exige un nouveau candidat identifié, distinct du27d4mesuré.


## Contrôle des films : critère corrigé, candidat inchangé

Film0 : EOF observé, retour puis90images. Film1 : retour normal après environ60s, pas d’EOF. La fonction originale0x412ca0 compare la position de lecture aux limites par film à0x610078+index×0x104 ; limite du film1=60s. L’arrêt est donc cohérent avec cette commande native ; résultat brut conservé, classification séparée.0xecb198 est l’état de fondu ;0x5b7530=0,01 est le seuil de fin du fondu. Il serait erroné de les présenter comme IDdufilm et durée60.

Le banc exige maintenant EOF **ou** dernier relevé périodique à≤3s de la limite originale, retour et90images de reprise sans erreur/dump. Cette tolérance correspond au relevé vidéo toutes les~2s ; elle n’est pas un EOF artificiel ni une preuve de tous les derniers plans. Les12films non tentés continuent ; cinq contextesPC isolés ont tous terminé avec restauration, reprisesVita à suivre.


## Audio réel : comparaison de tous les blocs des696ressources (hôte)

`audio_real_streams_test.py` a parcouru les696charges candidates locales :97204346blocs de13octets,3110539072octetsPCM comparés, zéro différence entre le décodeur natif et la fonctionGOG générée conservée. Traitement par petits blocs, sans sauvegarder dePCM ni modifier les données. Rapportlocal `real-stream-decode.json`, empreintes par charge. Tempshôte13,05s : **pas une mesureVita**. L’oracle x86 indépendant existe dans `adpcm_test.py` ; ce nouveau test exhaustif compare à sa référence générée. Les en-têtes sont rapprochés de0x40a7c0 (canaux, fréquence, taille, en-tête12octets), mais la signification du byte7 n’est pas déduite du catalogue.Ordre stéréo, dispatch, boucles/seek, sortie et lectureVita restent distincts.

## Revue des20cinématiques communes

130/130captures examinées,9planches dans `cinematic-visual-review/manifest.json`. Pas de corruption multicolore évidente sur Henry et les personnages visibles ; notamment cheveux/visage de l’escalator visibles. Plusieurs scènes injectées ont un fond noir/gris, décor partiel ou origine mal cadrée : elles ne qualifient **pas** le rendu complet d’une scène naturelle. Les images initiales a0 sont souvent noires de transition. Comparaison de contextePC et déclencheurs naturels/spécifiques restent requis avant d’attribuer un décor absent à un défaut du port.


### Comparaison des décors de contextePC/Vita

Trois paires de captures déjà collectées ont été examinées : cinéma6du métro (audit10004), cinéma30Eau (audit10001), cinéma42hôpital (audit10003). Les fonds noir/gris ou partiels sont également présents surPC dans ces injections. Planchelocale `cinematic-visual-review/context-comparison.png`. Temps de prise différents : comparaison qualitative, **pas identité pixel à pixel ni preuve du décor attendu dans une progression normale**. Ces cas restent une limite du scénario, pas un nouveau défaut de renduVita établi.

### Décodeur : recoupement x86 indépendant

Le banc `adpcm_test.py --cases2000` a aussi terminé contre les instructions originales de l’exécutableGOG : PCM, registres et étatsCPU/FPU, alias et7gardes ; vérificationnative1000/1000, zéro échec. Voir `adpcm-x86-oracle.log`. Cette vérification renforce la référence de la passe réelle696charges ; elle n’est toujours pas une lecture audioVita.

### Pourquoi étudier les réservations de prélecture

La trace enregistrée complète les pauses :40lectures journalisées du même blocSE614400octets,2287,1msI/O cumulés observés (chargements inclus), toutes ahead=0 dans les18premiers exemples ; un seul CreateFileSE consigné. Ce n’est pas simplement une fermeture systématique du fichier. Le budget global de prélecture reste2×1310720octets. Deux lecteurs conservant leur capacité maximale peuvent refuser un troisième bloc, même si le budget total est correctement borné ; c’est une **hypothèse de contention/starvation, pas une cause runtime prouvée**. Le cacheBGM1Mio a déjà une éviction souspression, mais les capacités des autres lecteurs n’ont pas de redistribution. Instrumenter les refus/capacités et vérifier la durée de vie avant toute politique d’éviction/redistribution ; ne pas augmenter le plafond aveuglément.

Rapportlocal `io-handle-block-analysis.json` : diagnostics de grosses lectures plafonnés, pas une trace exhaustive. La séquenceopti cible donc les longues pausesSE/dialogue avant de promettre un gainFPS moyen ; aucune nouvelle optimisation retenue dans le candidat mesuré.


## Cadence des20cinématiques injectées

Les intervalles réels entre swaps de la file de rendu ont été rapprochés des borneslecture de chaqueID. Hors fenêtres de captures(-3à+6images), moyennes parclip33,287à33,786ms ; P9533,956à37,391ms. Cela ne fait pas disparaître les pauses :43intervalles>100ms, maximum260,688ms(ID44). Les scènes sans décor naturel complet peuvent sous-estimer le coût réel. Les temps de chargement, fondus et captures restent séparés de cette distribution ; durée instrumentée brute conservée.

`cinematic-timing.json` contient20fiches avec début/fin, statistiques et taux d’images intermédiaires. ExempleescalatorID1 : moyenne33,428ms, P9535,829ms, max99,780ms,1422intervalles retenus. Ratiointermédiaire~2,7% dans ce scénario ; cela ne prouve donc **pas60images affichées constantes**. Les moyennes proches33,3ms ne sont pas des mesures de latence physique bouton-écran ni un effacement des pointes.


## Décors : placements originaux retrouvés, nouveaux scénariosPC

La table des événements originale permet désormais d’éviter le choix d’une seule salle par monde.0x52c4c0 indexe les flux sérialisés par numéro de scène ;0x4f0af1→0x52ca00 utilise le numéro de scène courant. Les propriétés24 pointent vers les drivers. `cinematic_contexts.py` produit45placements couvrant28IDs de driverscommuns. L’exempleID6 se trouve dans2:24 ; son injection en2:1 expliquait le contrôle incomplet du décor. L’absence de décor sur le portPC n’était donc pas une validation du rendu naturel.

23nouveaux contextes/26IDs sont en préflightPC, lesIDs1/2 disposent déjà du bon contexte. Ces attributions viennent des donnéesGOG mais **ne prouvent pas tous les prérequis narratifs**. Résultats et restaurations explicitement suivis ; aucun nouveau testVita concurrent. Après préflight et libérationde18980, reprisesVita ciblées seulement. Les autres driversspécifiques ne sont pas inventés.


## FilmsMPEG14/14 terminés sur27d4

14lectures avec retour et reprise observés :9EOFdécodeur,5arrêts cohérents avec les limitesGOG. Film12initial : le script a fermé le jeu pendant l’écriture de la capture5700,196608/2088978octets ; logarrêté à l’amorce de cettecapture, sans marqueurshotterminé. Ce défaut du banc est conservé et corrigé par attente du marqueur, reprise12réussie. Pas un crashVita identifié.51captures examinées, aucune corruption évidente des filmsvisibles. **Films7/8 n’ont qu’une captureaprèsretour : imagefilm nonqualifiée visuellement** ; certains échantillons10/11 sont aussi aprèsretour. Le nouveau testmémoire prendra des vuesprécoces7/8.

Débit livré par le décodeur, moyennes des relevés parfilm25,475à29,818images/s ; ce n’est pas un comptage physique de l’écran. Chaque film repartait dans unprocessusneuf : aucune validation de la durée de vie cumulée des graphes. `movie-analysis.json` donne les valeurs et la mémoire, candidats distincts.


## Cinq reprises isolées : couverture386mesurée

Les5cas qui avaient changé d’état dans le chaînage ont terminé isolément surVita,10captures examinées ; total381+5=386cas mesurés, sans transformer les5échecsbruts initiaux.6:21présente une vue blanche/grise : même vue constatée surPC, planchecomparative conservée. Cadrage/état limité, pas une preuve de rendu naturel complet.

CinémaCynthiaID6dans2:24 : terminé avec15captures examinées, décor visible, pas de corruption évidente du visage/cheveux/chemise d’Henry dans ces échantillons. Les reprises des autrescontextes ciblés continuent ; elles ne représentent pas les182cinématiques.


## Reprises ciblées dans les salles des événements : terminées

Les six contextes Vita retenus après préflight PC ont terminé avec le candidat 27d4 : Cynthia (6), forêt (9), Passé (26–28), hôpital (42 et 44), appartement (60 et 66). Les 91 captures ont été examinées. Visages, cheveux et vêtements sont visibles sans corruption évidente dans les échantillons éclairés. Le clip 60 reste presque noir dans ses deux vues et ne reçoit pas de validation visuelle complète. Les autres drivers, prérequis narratifs et toutes les images des clips restent hors de cette preuve. Les résultats bruts marqués « unreviewed » ne sont pas réécrits ; les manifestes de revue et `targeted-review-complete.json` consignent la revue postérieure.

## Risque de concurrence à vérifier après la remise du candidat mémoire

Un banc déterministe extrait `hand_frame` de la source de production, immobilise le consommateur sur le slot 0 puis laisse le producteur avancer cinq images. Le slot en cours de lecture est réécrit (premier octet 17 devenu 37). La limite de trois images en attente ne protège donc pas à elle seule `obusy` lors d'un retard assez long du consommateur. Preuve hôte : `probe-movie-fifo-busy.json`, pas d'occurrence matérielle attribuée. Aucun changement de production n'est intégré au candidat b8d0 à ce sujet ; tester une réservation explicite des slots dans un candidat distinct. Ce risque concerne les images MPEG et n'explique pas par lui-même les textures d'Henry dans les scènes 3D des testeurs.
