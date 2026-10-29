# Décodeur audio natif — essais v112 à v121

Branche expérimentale, 27 octobre 2026. Le décodeur `0x40a880` du GOG traite un bloc de 13 octets et produit seize échantillons PCM signés 16 bits, soit 32 octets. Il copie deux échantillons initiaux, lit une échelle 16 bits et développe quatorze deltas de quatre bits avec les prédicteurs entiers du jeu.

## Contrat et frontière

`audio_adpcm.c/.h` fournit un calcul indépendant du CPU invité, sans allocation ni état persistant. Les calculs conservent le bouclage arithmétique 32 bits, le décalage signé et la troncature des prédicteurs en 16 bits. Il n'y a ni saturation ajoutée, ni changement de fréquence, de mixage ou de qualité sonore. Les blocs viennent toujours des archives GOG du joueur.

Le pont est désactivé sans `SH4_NATIVE_ADPCM=1`. Il accepte seulement le CPU principal et les retours audités `0x40aada`, `0x40ab45`, `0x40ab92`, qui correspondent aux voies mono et stéréo du remplissage de flux `0x40a960`. Il vérifie les plages et conserve l'original si l'entrée et la sortie se recouvrent ou touchent la pile privée du décodeur. L'original peut avoir un ordre d'écriture particulier sur ces recouvrements ; aucune copie anticipée ne remplace cette règle.

Le GOG utilise **`ret 8`** : le pont consomme l'adresse de retour et les deux arguments. Il reconstruit aussi exactement EAX (fin de source), ECX (dernier résultat décalé) et EDX (partie haute du dernier terme prédicteur et partie basse du dernier échantillon). Tous les autres octets du CPU/FPU sont conservés. Ce contrat est comparé, plutôt que supposé à partir d'une déclaration C.

`SH4_ADPCM_VERIFY=N` calcule le résultat natif, exécute l'original et compare les 32 octets PCM et tout l'état CPU. Pendant cette vérification, le résultat original reste appliqué ; une différence désactive le candidat.

## Preuves obtenues

- `port/test/adpcm_test.py` : **131 072 blocs** comparés au x86 GOG exécuté sous Unicorn, à la référence générée, au calcul natif et au pont de production. Toutes les valeurs d'échelle 16 bits sont parcourues, avec historiques extrêmes/aléatoires et nibbles variés. Les octets PCM, canaris, registres et tout l'état CPU/FPU correspondent. Les **6 899 cas de recouvrement** gardent l'ordre original ; sept familles de gardes sont exercées. Après les 1 000 premiers blocs vérifiés, le vrai chemin rapide du pont est utilisé.
- **65 536 blocs du code ARM réellement compilé**, confrontés au noyau hôte qualifié contre le x86 : toutes les échelles, entrées/sorties byte non alignées, résultat de registre, prédicteur et canaris passent. Les modes FPSCR normaux et FTZ/DN sont alternés, sans effet attendu sur ce calcul entier.
- Les premiers parcours de données réelles PC et Vita comparent respectivement au moins **540 000 et 600 000 blocs**, sans différence ni refus ; les deux sites stéréo sont présents dans ces journaux. Cette preuve ne revendique pas encore une exécution réelle du site mono.

La régression PC v112 donne 23 images identiques. La paire Vita garde les douze poses et les deux captures, mais ne démontre aucun gain : **33,322 / 33,313 ms** en moyenne, neuf images ≥ 50 ms dans chaque variante. Les pointes chaudes passent même d'environ 52–61 ms à 62–65 ms avec ce premier pont. **v112 est rejeté comme optimisation**, malgré son équivalence fonctionnelle. Il recopiait inutilement les 128 octets du CPU dans les deux sens pour chaque bloc, et utilisait une boucle générique à quatorze itérations.

v113 garde la copie complète du CPU et le tampon de comparaison uniquement pendant la vérification. Le chemin rapide écrit directement les 32 octets PCM et les quatre registres modifiés ; il traite les deux nibbles d'un octet ensemble, comme le GOG. Les 131 072 comparaisons complètes et les 65 536 comparaisons ARM sont relancées avec succès. Les 10 000 premiers blocs réels sont vérifiés au démarrage, avant la fenêtre mesurée ; la régression PC donne 23 images identiques.

Deux paires Vita parcourent les mêmes 1 500 images de Past, avec sticks réels et préchargement conservé. La deuxième inverse l'ordre des variantes. Les douze poses et les deux captures sont identiques dans chaque paire ; le montage `native-adpcm-v113/review.png` a été examiné, Henry reste normalement texturé.

| Essai | Original / natif, moyenne | Images ≥ 50 ms hors captures | Pointes sans lecture comptabilisée |
|---|---:|---:|---:|
| v113, référence puis natif | 33,322 / 33,309 ms | 9 / 7 | 52,5–60,9 / 51,1–54,7 ms |
| v113, natif puis référence | 33,304 / 33,362 ms | 8 / 7 | 51,5–60,7 / 52,5–54,2 ms |

La cadence moyenne reste inchangée. Les pointes chaudes sont plus courtes dans les deux paires, mais elles restent visibles ; les premières lectures atteignent encore environ 87 ms. Le journal conserve au plus douze détails par fenêtre de deux secondes : les fenêtres complètes de ces quatre mesures n'en comptent pas plus de sept, donc cette limite ne masque pas leurs longues images. Les moyennes excluent les fenêtres touchant une capture ; les comptes individuels excluent les captures et leurs trois images voisines. Ce n'est pas une preuve de 30 fps constants.

## Paquets et entrelacement — candidat v118/v121

Le noyau `sh4_adpcm_packets` traite des paquets contigus de 26 octets : le mode compact utilise le premier canal et produit 32 octets, l'autre entrelace les deux canaux et produit 64 octets. Il ne conserve aucun état entre les appels et n'alloue rien. Les contrôles de flux, banques, slots, fin de fichier et notifications restent dans le jeu.

L'adaptateur optionnel `SH4_NATIVE_AUDIO_PACKETS=1` consomme seulement un préfixe de la boucle `0x40ab20`. **La dernière itération reste originale**, ce qui reconstitue ses registres volatils, indicateurs, arguments et données de pile. Les tailles, banques, adresses attendues, débordements et recouvrements avec source, métadonnées et pile sont vérifiés avant toute écriture. Un préfixe refusé garde les itérations originales ; des plages initialement superposées peuvent devenir admissibles après ces itérations.

`SH4_AUDIO_PACKETS_VERIFY=N` confronte les N premiers préfixes au décodeur original sur les vraies données. Une copie CPU privée et une petite zone temporaire de pile restaurée permettent la comparaison ; les octets PCM originaux sont appliqués pendant cette vérification. Une différence désactive le candidat. Ce mode est exclu des mesures de vitesse.

Le banc `port/test/audio_packets_test.py` compare 20 000 boucles réelles x86 GOG, référence générée et adaptateur de production : 15 000 cas stéréo, 5 000 compacts, 646 recouvrements initiaux, lots allant jusqu'à 4 096 paquets, PCM, source, canaris, pile vivante et CPU/FPU entier. Quatorze familles de refus transactionnels passent et le banc vérifie que le chemin natif a réellement été exécuté. Les huit premières vérifications de production sont également exercées. Le noyau ARM réellement compilé passe 4 000 lots face au noyau hôte qualifié, avec buffers non alignés, lots vides/maximaux, canaris et modes FPSCR variés.

v115 déplace la lecture de l'option hors de la boucle : l'option désactivée n'appelle pas l'adaptateur pour chaque paquet. Les 20 000 comparaisons de boucle ont été relancées et le chemin natif a été effectivement exercé. Les régressions PC de production, option désactivée puis activée, donnent chacune 23 images identiques à la référence. Une première régression v114 avec la vérification active avait toutefois six pixels différents à l'image 4 750 ; cette différence n'a pas été reproduite dans les deux séries de production et n'est pas expliquée. La première paire Vita v114 avait une arrivée et un temps de chargement anormaux, avec des poses différentes : elle **ne sert pas de comparaison de vitesse**.

Deux paires Vita contrôlées ont ensuite utilisé exactement le même binaire v118, avec les gardes de collision qualifiées et les mêmes options ; seul `SH4_NATIVE_AUDIO_PACKETS` change. Le parcours déplace réellement Henry dans Past pendant 1 500 images. La seconde paire inverse l'ordre. Les douze poses et les deux captures sont identiques dans chaque paire ; les deux images v121 ont aussi été examinées. Les captures cadrent surtout un mur proche et ne qualifient pas toutes les cinématiques. Les fenêtres de deux secondes contiennent au plus huit longues images, en dessous de la limite de douze détails enregistrés.

| Essai | Option désactivée / activée, moyenne | Images ≥ 50 ms hors captures | Dont sans lecture disque |
|---|---:|---:|---:|
| v119, désactivée puis activée | 33,296 / 33,326 ms | 7 / 2 | 5 / 0 |
| v121, activée puis désactivée | 33,296 / 33,322 ms | 6 / 2 | 4 / 0 |

Les deux longues images restantes dans chaque série activée sont les premières lectures, environ 79–88 ms ; le traitement par lots ne les supprime pas. Le gain constaté concerne les pointes de traitement audio, **pas la cadence moyenne ni une garantie de 30 fps constants**. Il n'est pas encore activé par défaut : le site mono réel, d'autres scènes audio et les cinématiques ne sont pas couverts par ces parcours. La sortie PCM est bit pour bit identique sur les corpus x86, ARM et les préfixes réellement vérifiés sur PC/Vita, mais cela ne prouve pas tous les états du jeu.

Les données, références temporaires, journaux et binaires/symboles restent dans `build/room-audit/native-adpcm-v112/`, `native-adpcm-v113/` et `native-audio-packets-v114` à `v121`. La première implémentation est archivée dans `v112/initial-source/`. Ces essais ne démontrent pas une correction générale des pauses.

## Origine de la piste

Après la [conservation du préchargement v110](STREAMING-EXPERIMENTS.md), des pointes périodiques sans lecture comptabilisée sur le fil principal subsistent. Le profil isolé v111 conserve les entrées de grands traitements et les fonctions des plages audio/fichiers, en gardant `-Os`. Sa fenêtre 3 980–4 100 comprend les premières lectures et le traitement suivant ; elle identifie notamment `0x40a880` et son appelant `0x40a960`. Les pourcentages d'un tel profil ne sont pas des économies de temps promises. La paire sans profilage doit établir si ce remplacement réduit effectivement les pointes.
