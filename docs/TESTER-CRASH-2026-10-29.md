# Crash testeur après le déplacement du train — 29 octobre 2026

Le testeur situe l'incident dans la cinématique suivant l'utilisation de la poignée pour déplacer le train. Fichiers reçus : `sh4log(10).txt` et `psp2core-1793250831-0x00031a32c3-eboot.bin.psp2dmp`. Originaux préservés, empreintes et analyse sous `build/room-audit/tester-crash-20261029/` (hors Git).

## Cause directe identifiée

Le dump indique une exception d'accès (`0x30004`) dans **SDLAudioP2**, le thread du mixeur audio. Les threads de prélecture ne sont pas les threads fautifs de cet incident.

- Module applicatif RX à `0x81014000`, taille `0x7942d4` ; RW taille `0x475694`.
- PC fautif : `0x815b9396`, soit offset RX `0x5a5396`.
- Dans le VPK distribué du28octobre, à cet offset : `vldr s10, [r5]`.
- Le dump donne **R5=0**, indice de sortie initial,1024trames audio ; le résultat d'allocation conservé àSP+12 est aussi nul.

Le paquet local distribué est `sh4vita-test-2026-10-28.zip`, eboot SHA `2fb943a4a190e8bcc956c93bd3c78ec67b73707791944afe7c03f53bbd1c6b8a`. Ses tailles RX/RW correspondent exactement à celles du module du dump. Le SELF a été décompressé pour lire **ses propres instructions**, sans plaquer les symboles d'un build récent sur une ancienne adresse. Le journal ne contient pas de hash d'eboot permettant une comparaison cryptographique complète avec la console du testeur ; sa version est celle indiquée par Valentin, avec cette correspondance d'implantation supplémentaire.

Le début de cette routine calcule1024×2floats, appellecalloc avec2048éléments de4octets, puis stocke son retour àSP+12. Le code de mixage relit ensuite ce pointeur pour accumuler le premier échantillon. Il ne teste pas son succès. Cela correspond au mélangeur `mix()` de `port/src/dx/dsound.c` dans les sources effectives archivées du paquet.

**L'accumulateur audio temporaire de8192octets est nul et le mixeur le déréférence.** Le défaut était également présent dans la source courante avant le correctif de ce jour : les précédentes corrections de création/lifetime des tampons audio et de redimensionnement vidéo ne l'avaient pas supprimé.

## Contexte et limites de l'attribution

Le journal couvre environ1270secondes, soit21minutes. La vidéo arrive à sa fin puis le jeu prépare de nouveaux programmes graphiques. Le dernier relevé du tas hôte est22859Kio utilisés, environ22,3Mio, pour un plafond configuré à24Mio. La mémoire invitée relevée est138125Kio sur166Mio alloués ; la VRAM libre est encore42090Kio. **Ne pas confondre ces trois pools.**

Le snapshot montre aussi le fil de préparation dansSceShaccCg. Une pression transitoire d'allocation au retour vidéo est cohérente avec ce contexte, mais ce relevé ne départage pas pic, fragmentation, fuite ou autre perturbation de l'allocateur. Il ne suffit pas à expliquer toute la croissance mémoire de la session.

La dernière clé de shader journalisée (`43ee0c17f6bbc24a`) existe dans le cache du VPK local. Le dernier message ne démontre donc ni un shader absent du paquet ni une corruption de cache. **Pas de raison de retirer les caches sur ce seul incident.** La faute directement démontrée est dans le mélangeur audio, pas dans l'instruction de rendu.

## Correctif écrit, distinct des candidats de la nuit

Le chemin normal utilise désormais un accumulateur statique de1024trames stéréo,8Kio, réservé par le programme et remis à zéro à chaque callback. Le port ouvre un seul périphériqueSDL et son callback est séquentiel : aucun buffer temporaire n'est alloué/libéré à chaque bloc normal. Ne pas réutiliser aveuglément cette propriété si un deuxième périphérique audio est ajouté ultérieurement.

Les blocs plus grands restent supportés par une allocation contrôlée. Si elle est refusée, le callback produit du silence sur ce bloc, sans lireNULL ni avancer les tampons/notifier une lecture inexistante. Le bloc normal suivant peut reprendre. La fréquence48kHz, l'interpolation des échantillons, les gains, les positions, la saturation et l'ordre des notifications restent inchangés.

Ce correctif **ne répare pas à lui seul toutes les pénuries mémoire du jeu**. Il supprime l'allocation répétée qui a échoué ici et sa déréférence non contrôlée. Il est postérieur au binaire4092 de la campagne qualité ; ne pas attribuer ce correctif aux captures/endurance de cet ancien candidat.

## Vérifications réalisées

- Test différentiel contre le mixeur du paquet distribué :160cas PCM/états/notifications identiques, mono/stéréo,8/16bits, changements de fréquence, boucles, fin de lecture,3D, pan, gains et différentes tailles de callback.
-1000callbacks normaux avec toutes les allocations forcées en échec : aucun appel d'allocation de l'accumulateur, pas de crash.
- Échec du bloc surdimensionné : silence dans les limites du bloc, gardes intactes, aucun avancement/notification, puis reprise du bloc normal.
- Tests existants de création audio et durée de vie des interfaces : passent.
- RégressionPC du code corrigé :23/23captures identiques, aucun fatal ; sauvegardesPC restaurées.
- CompilationPC etVita réussie. Candidat séparé et symboles archivés sous `tester-crash-20261029/eboot-audio-scratch.bin` et `.velf`, bibliothèque normaleCG (pas le backendDXT corrigé de la nuit).

**Pas encore installé ni reproduit dans la scène exacte sur Vita ou chez le testeur.** Ne pas annoncer une correction vérifiée en conditions réelles, ni distribuer ce build normalCG comme s'il s'agissait du candidat qualité combiné. Aucun changement de la console n'a été nécessaire pour ce diagnostic.

Le test de production réutilisable est `port/test/dsound_mix_scratch_test.py`, avec une fixture du précédent mélangeur du port (aucun code ou fichier original du jeu). Le premier banc manquait la constanteOUT_RATE ; elle a été ajoutée avant le passage des comparaisons.
