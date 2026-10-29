# Nouveaux incidents testeur — journaux11/12,29octobre2026

Originaux reçus dans Downloads : `sh4log(11).txt`, `sh4log(12).txt`, core1793259684 et core1793264328`.tmp`. Copies, SHA256, statistiques et analyse des registres : `build/room-audit/tester-incidents-11-12-20261029/` (horsGit). Diagnostic en lecture seule ; aucune commande envoyée à la Vita, dont l'endurance du candidat combiné continue avec le propriétaire35216.

## Crash identifié : même mixeur audio que le précédent

Le core complet1793259684 (239056octets) indique une exception0x30004 dans **SDLAudioP2**. ModuleRX base0x81071000, taille0x7942d4, RW taille0x475694. PC0x81616396, soit **offsetRX0x5a5396** ; R5=0 et pointeur sauvegardéSP+12 nul,1024trames au callback. Dans le binaire du paquet distribué28octobre déjà extrait, cet offset est `vldr s10,[r5]`, au mixage de l'accumulateur temporaire. Même offset, instruction, taille de bloc et pointeur nul que le [crash précédent](TESTER-CRASH-2026-10-29.md).

Cette correspondance d'implantation et le journal de profil expérimental sont compatibles avec l'ancien paquet ; ils ne constituent pas un hash du binaire installé chez le testeur. Aucun VPK fourni pour les seuls assets n'a été analysé. Les symboles récents n'ont pas été appliqués à une ancienne adresse : l'instruction est lue dans leRX du paquet ancien conservé.

Le correctif audio du29octobre, **inclus dans le candidat27d4ff04 actuellement sur la Vita de Valentin**, utilise8Kio statiques pour ce callback et protège l'allocation des blocs exceptionnels. Il supprime le chemin nul observé. Ses160comparaisonsPCM/états et1000callbacks sans allocation normale ont déjà passé ; pas de répétition inutile de ces tests pendant l'endurance. Ce correctif ne garantit pas la disparition de toutes les pénuries mémoire ni de tous les crashs.

## Deuxième core incomplet : pas de conclusion sur sa faute

Le `.tmp`1793264328 ne fait que4608octets : fluxgzip tronqué,15111octets partiellement décompressables, sans fin de flux. Les métadonnées complètes nécessaires ne sont pas lisibles par le parseur. **Impossible d'attribuer ce deuxième incident au mixeur à partir de ce fichier.** Conserver l'original ; un éventuel `.psp2dmp` terminé permettra de conclure. Ne pas présenter le `.tmp` comme une seconde confirmation.

## Textures : allocation refusée explicitement journalisée

| Observation | Log11 | Log12 |
|---|---:|---:|
| Dernierwatchdog |910s |780s |
| Tas hôte, premier relevé |6621Kio |6621Kio |
| Tas hôte, dernier/pic relevé |23179Kio |22928Kio |
| VRAM encore libre, dernier relevé |54080Kio |52485Kio |
| Textures ignorées après allocation refusée |15 |0 |
| Hits de programmes du cacheVPK |127 |131 |

Log11 :3refus de1024Kio pour512×512, puis12refus de256Kio pour256×256. Le port écrit explicitement `texture skipped`. Le tableau concerne le tampon temporaireRGBA de décodage logiciel, pas un épuisement démontré deVRAM. Le tas hôte approche le plafond24Mio ; ce sont des pools distincts de la mémoire invitée166Mio et de laVRAM. Les logs n'expliquent pas à eux seuls si la croissance vient d'objets conservés, de fragmentation, de pics ou d'une fuite.

La capture montre une anomalie visible sur Henry. Elle est compatible avec des données de texture manquantes, mais sa correspondance exacte avec l'une de ces15allocations n'est pas enregistrée. Log12 ne contient ni ces avertissements ni un fatal ; son arrêt et le `.tmp` ne suffisent pas à déterminer sa faute.

Les deux logs montrent des succès du cacheVPK et aucun message de compilation source/miss dans les motifs relevés. Cela ne prouve pas tous les shaders corrects, mais **ne démontre pas un cache corrompu et ne justifie pas de le supprimer** sur cet incident.

## Ce que le candidat plus récent change, et ce qui reste à confirmer

Le candidat27d4ff04 utilise le cheminDXT matériel avec la bibliothèque au swizzle corrigé2dfd9cc9. Pour les texturesDXT admissibles, l'envoi ne passe plus par la grosse allocation temporaireRGBA du chemin qui échoue danslog11 ; le stockageGPU diminue aussi. Le repli logiciel reste utilisé pour certains formats/dimensions : ce changement n'est pas une garantie universelle contre les refus d'allocation. Les gardes récents des allocations noires et des chaînes de mipmaps ne réparent pas automatiquement toute corruption.

Pendant le diagnostic, l'endurance courante atteint32visites terminées et environ45minutes sans redémarrage : tas hôte dernier9347Kio, pic9674Kio depuis le début du lancement, contre environ23Mio dans les logs reçus. **Parcours, scènes et binaire différents : comparaison descriptive uniquement**, pas preuve de suppression d'une fuite ou validation de la scène du testeur. Les trois heures et les cinématiques du candidat restent à terminer et examiner.

Conclusion : premier crash directement expliqué par le correctif audio récent ; défaut de textures accompagné d'une pénurie hôte prouvée danslog11, avec changements pertinents mais pas encore confirmés chez ce testeur ; deuxième crash non attribuable faute de core complet. Aucune modification du candidat figé, du cache ou de la console n'a été faite pour cette analyse.
