# Archive audio résidente — audit du 28 octobre 2026

**À confirmer sur Vita par Valentin avant activation.** Le remplacement de la réserve résidente de 22,61 Mio par un cache de lecture de 512 Kio reste expérimental et incomplet. Le binaire distribué conserve la réserve originale. Il faudra qualifier les sons, les cinématiques, les premières lectures et une session longue ; aucune économie mémoire appliquée ni correction des crashs longue durée n'est annoncée. Valentin souhaite faire ce contrôle lorsqu'il aura la console sous la main.

## Problème et potentiel

Le GOG charge `sound/sound.pac` dans une allocation invitée de 23 707 648 octets (arrondie sur les pages), alors que le fichier fait 23 704 344 octets. Cette réserve reste présente pendant le jeu, en plus des tampons DirectSound. Son remplacement par un service natif de lecture des sons demandés pourrait libérer une part importante du tas invité. Aucun octet n'est encore économisé par l'audit ; il n'est pas établi que cela suffira à résoudre les crashs.

Le conteneur commence par `SDPA`, le nombre d'entrées et une table de 355 triplets de 32 bits. La table et l'en-tête font 4 268 octets. Les entrées se suivent jusqu'à la fin exacte du fichier. Chaque triplet contient un offset, une longueur et un troisième champ conservé comme opaque ; il ne doit pas être remplacé par une hypothèse de boucle ou d'en-tête.

Les 355 entrées ont un en-tête WAVE PCM mono à 22 050 Hz. Les longueurs RIFF ne correspondent pas toujours à la taille du triplet. Pour les données PCM déclarées, 326 entrées se terminent exactement à la limite du triplet, 23 annoncent deux octets de plus, deux en annoncent 362 de plus, et quatre laissent 16 octets supplémentaires. Un extracteur WAV générique qui coupe ou normalise ces données ne constitue donc pas une référence équivalente au lecteur GOG. Une version native doit reproduire les octets effectivement lus et les bornes du lecteur original, y compris les lectures éventuelles dans l'entrée suivante ou dans le remplissage final.

## Frontières repérées

La fonction `0x408ac0` alloue le fichier à partir de `VirtualAlloc` (retour `0x408b8a`) et stocke la base à `this+0x10ec`. Les références directes à ce champ dans le module appartiennent à six fonctions : `0x408ac0`, `0x408d50`, `0x409480`, `0x409530`, `0x409a30`, `0x40a4b0`. Cela ne garantit pas l'absence de pointeurs copiés ou de lecteurs indirects.

`0x409530` et `0x409a30` reçoivent l'objet dans ECX et l'identifiant de son comme premier argument. La sonde optionnelle `SH4_SOUND_ARCHIVE_TRACE=1` enregistre leur triplet avant d'exécuter intégralement l'original. Elle ne modifie ni le CPU, ni les pointeurs, ni le PCM ou la politique d'allocation. Son test vérifie l'absence de modification de l'état CPU et les contrôles de plages. Tous les triplets observés en jeu correspondent à ceux du fichier source.

## Parcours effectués

Une session traverse prison 4:11, Past 8:24 et métro 2:14 avec mouvements et balayage demandés par le banc. Les poses doivent être utilisées pour qualifier le mouvement réel ; les nombres ci-dessous décrivent les demandes de sons effectivement enregistrées.

| Plateforme | Appels enregistrés | Sons distincts | Somme des tailles des entrées distinctes |
|---|---:|---:|---:|
| PC, tas invité limité à 166 Mio | 439 | 15 | 972,04 Kio |
| Vita | 545 | 18 | 1 095,99 Kio |

Les trois demandes de pièces ont terminé dans une seule session sur chaque plateforme, sans délai dépassé ni erreur fatale. Seule la voie `0x409530` a été observée. La somme sur toute une session n'est pas une mesure des sons simultanément actifs et ne dimensionne pas un cache pour l'ensemble du jeu. La voie `0x409a30`, les cinématiques, les interruptions et la restauration des tampons restent à couvrir.

Les preuves sont dans `build/room-audit/sound-residency-20261028/`, `sound-residency-pc-20261028/` et `sound-residency-vita-20261028/`. La sauvegarde et le binaire de référence ont été restaurés avec vérification SHA-256 après le parcours Vita.

## Premier service isolé

`sound_archive.c/.h` valide le répertoire sans modifier la destination sur erreur et lit des plages logiques du fichier. Le lecteur conserve les lectures entre deux entrées et les zéros jusqu'à la limite de page de l'allocation originale. Il ne reconstruit pas les WAV et n'interprète pas le champ opaque.

`sound_archive_cache.c` fournit un cache de huit blocs de 64 Kio (512 Kio de stockage fourni par l'appelant), sans allocation dynamique. Il retourne des copies et ne publie aucun pointeur dont la durée de vie dépend de l'éviction. Une lecture ratée invalide le bloc concerné ; elle ne peut pas produire un faux succès lors du prochain accès. Le cache n'est pas concurrent : un futur pont devra sérialiser ses accès ou utiliser une propriété de fil explicite.

`sound_archive_test.py` a comparé 11 070 plages exactes avec et sans cache au fichier original : les 355 entrées, leurs données PCM déclarées, les frontières entre entrées, le remplissage final, des plages aléatoires non alignées, les erreurs d'entrée et d'I/O, la réutilisation et le débordement du compteur du cache. Les zones de contrôle entourant les sorties et le stockage du cache sont intactes. Le code ARM compilé a également passé 1 712 plages pour chacun des deux chemins et la comparaison des 355 triplets du répertoire. Les binaires et le banc ARM sont conservés hors Git dans le dossier de preuves.

Ce service ne remplace **pas encore le stockage résident** : la réserve reste présente dans toutes les versions de jeu actuelles. Les 512 Kio ne sont donc ni un budget de cache qualifié pour toutes les scènes, ni une réduction mémoire mesurée en jeu. Le raccordement complet doit couvrir les copies `rep movs` de `0x408d50`, qui échappent au simple remplacement de `0x404a00`, et les lectures directes des en-têtes dans les deux fonctions de demande.

## Vérification sur appels réels

`SH4_SOUND_ARCHIVE_VERIFY=1` utilise le lecteur et son cache en parallèle du jeu : chaque entrée demandée est comparée à son équivalent dans l'archive résidente avant l'exécution originale. Sur les trois zones du banc, PC a enregistré 423 demandes et au moins 400 vérifications confirmées, sans différence (30 lectures source au relevé 400). Vita a enregistré 538 demandes et au moins 500 vérifications confirmées, sans différence (37 lectures source au relevé 500). Les compteurs sont imprimés tous les cent appels après les vingt premiers : le dernier message ne constitue pas un compte final exact. Six captures Vita sont identiques à la passe précédente. Aucun échec d'allocation, erreur fatale ou délai dépassé n'est relevé. Les dossiers de preuves sont `sound-shadow-pc-20261028/` et `sound-shadow-vita-20261028/`.

Le banc de vérification teste également les 355 entrées avec une archive résidente simulée, la conservation de tout le CPU et de la mémoire invitée, ainsi qu'une divergence volontaire. Une divergence désactive le lecteur de diagnostic et garde l'original actif. Cette lecture supplémentaire sert à la qualification, pas à une mesure de performances.

L'étape suivante expérimentale, `SH4_SOUND_ARCHIVE_TRANSFER=1`, fournit réellement les octets de certaines copies audio depuis le cache, tout en gardant l'archive originale pour comparaison et repli. Le prototype couvre la routine `0x404a00` et les huit copies avant `rep movs` de `0x408d50`, sans changer leurs effets sur ESI/EDI/ECX ni le traitement arrière et les recouvrements. Un refus ou une divergence laisse la copie originale rétablir l'intégralité de la destination. Les tests différentiels de 20 000 séquences et de livraison d'une grande entrée passent ; la qualification de ce transfert en jeu reste à effectuer. L'archive résidente n'est toujours pas retirée et aucune économie de 22,6 Mio ne doit être annoncée.

La première paire de transfert a terminé sur PC et Vita avec six captures identiques sur chaque plateforme et sans divergence du lecteur. Le journal Vita confirme au moins cent copies réellement livrées. Ce premier mode relisait chaque entrée entière lors d'une demande, puis comparait encore les seules plages copiées : la prison est plus lente d'environ 1,39 ms en moyenne dans la portion mobile retenue (36,326 → 37,714 ms), Past d'environ 0,14 ms et le métro d'environ 0,71 ms. Il ne faut pas retenir ce double contrôle comme un chemin de performance. Le candidat suivant réserve le contrôle intégral à `SH4_SOUND_ARCHIVE_VERIFY=1` ; le mode de livraison seul vérifie uniquement les octets copiés. Son cache et sa zone temporaire (512 + 64 Kio) sont alloués seulement à l'activation du diagnostic. Les preuves de la première paire sont dans `sound-transfer-pc-{ref,on}-20261028/` et `sound-transfer-vita-20261028/`.

Le mode allégé a terminé le même parcours PC et Vita. Les six captures PC sont identiques à la référence et les six captures Vita sont identiques entre les deux passes. Le test de production de la routine de copie couvre aussi 10 000 cas, avec livraison, repli après écriture partielle, recouvrements et conservation du CPU.

| Vita, portion mobile entre captures | Résident | Cache, contrôle des copies seulement |
|---|---:|---:|
| Prison 4:11 | 36,424 ms | 36,357 ms |
| Past 8:24 | 34,195 ms | 34,208 ms |
| Métro 2:14 | 33,700 ms | 33,724 ms |

Ces écarts sont faibles et ne démontrent ni gain de cadence ni régression notable sur ces fenêtres. Les plus longues images observées dans le métro restent autour de 152 ms dans les deux modes ; le nouveau lecteur ne résout pas ces pauses. Les mesures excluent les captures et les chargements, mais ne garantissent pas l'absence d'une première lecture coûteuse lors d'autres sons ou cinématiques. Au moins cent transferts sont confirmés sur Vita ; au relevé du centième, le compteur cumulé n'indique que quatorze lectures source. Il n'y a pas d'échec de lecture, d'allocation, de divergence ni d'erreur fatale relevé. Les preuves sont dans `sound-transfer-fast-pc-20261028/` et `sound-transfer-fast-vita-20261028/`. La sauvegarde et le binaire de référence ont été restaurés et vérifiés. La réserve originale reste indispensable au contrôle et au repli : aucune économie mémoire n'est encore appliquée.

## Extension expérimentale aux en-têtes — 28 octobre

Le candidat non distribué route aussi les lectures scalaires de 8, 16 et 32 bits dans `0x408d50`, `0x409530` et `0x409a30`, ainsi que les huit copies avant de chacune de ces trois fonctions. `SH4_SOUND_ARCHIVE_HEADERS=1` exige le mode de transfert. Les lectures ordinaires de l'objet et de la pile conservent leurs données résidentes ; les plages appartenant à la banque passent par le lecteur, avec comparaison et repli. Aucun pointeur de cache n'est publié.

Les tests de production des 355 entrées et les 20 000 cas différentiels passent avec les accesseurs. Un premier parcours PC terminé a confirmé au moins 2 500 lectures d'en-têtes sans divergence. La paire suivante utilise le même binaire, une limite invitée de 166 Mio et exactement le même parcours : résident puis routage, trois destinations terminées dans chaque processus en environ 109 secondes, six captures identiques, aucune erreur du lecteur ni erreur fatale. Preuves : `sound-headers-pair-pc-20261028/`. Les compilations PC et Vita réussissent ; cette extension n'a pas encore été exécutée sur Vita. La voie `0x409a30` reste non observée sur ce parcours. L'archive originale est toujours allouée, y compris dans ce candidat : ces vérifications ne mesurent aucune économie de RAM et ne prouvent aucune correction de fuite.

## Alternative examinée : source compressée sans perte

Une sonde hors ligne, sans modification du préparateur ou du jeu, compresse des blocs indépendants de 64 Kio. Le zlib direct totalise 18 303 605 octets ; séparer les plans d'octets donne 15 957 283 octets ; appliquer d'abord une différence modulo 16 bits donne 14 800 751 octets (environ 14,12 Mio), contre 23 704 344 octets d'origine. Chaque bloc a été décompressé et reconstruit exactement, en-têtes et queues compris.

Ce résultat mesure une taille sur PC, sans index final ni coût ARM de décompression. Il ne valide aucun changement du préparateur. Une source compressée résidente pourrait réduire les accès au disque au prix d'un budget mémoire supérieur au lecteur par plages ; elle nécessite le même audit de pointeurs et une qualification de cadence. Preuve : `sound-residency-20261028/compression-probe.json`.

## Suite nécessaire avant remplacement

La réserve fixe et une fuite éventuelle sont deux problèmes distincts. Retirer la banque résidente laisserait davantage de place aux ressources utiles ; cela ne corrigerait pas une allocation oubliée à chaque transition. Le cache proposé reste limité à huit blocs et réutilise leurs emplacements : il ne peut pas accumuler une copie de chaque son rencontré. Les tampons DirectSound actifs restent une autre catégorie d'allocations, dont la durée de vie doit aussi être vérifiée.

Pour qualifier la stabilité, il faudra comparer des retours au même état après stabilisation, examiner séparément textures, sommets, audio et tas, puis prolonger le parcours dans un seul processus sans reprise automatique. Un arrêt plus tardif ne suffira pas à déclarer une fuite corrigée. Les boucles déjà mesurées reviennent à un niveau stable dans certaines pièces ; la saturation du grand parcours ne permet toujours pas de distinguer toutes les ressources retenues des pics de coexistence et des états narratifs différents.

1. Établir les plages lues et la durée de vie des pointeurs dans les six fonctions et leurs callbacks ; conserver l'opaque et les éventuels dépassements des longueurs RIFF.
2. Construire un banc différentiel sur les 355 entrées comparant les octets effectivement transmis aux tampons DirectSound, les boucles et la chronologie des commandes.
3. Tester un service natif avec cache borné et repli intégral avant de retirer l'allocation résidente. Un repli ne doit pas conserver une vue partiellement modifiée de l'objet invité.
4. Mesurer la mémoire économisée, les nouvelles lectures et les pauses sur Vita. Le service devra servir les sons sans retarder le fil de jeu et sans dégrader l'audio.
5. Qualifier les parcours longs et toutes les voies avant toute activation dans le paquet des testeurs.

Les données restent extraites du jeu GOG de l'utilisateur. Cet audit ne demande aucune modification des données déjà préparées et n'ajoute aucun traitement au démarrage normal lorsque la sonde est absente.
