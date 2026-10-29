# Banc de lecture des cinématiques du moteur

Outils de développement sur la branche expérimentale ; ils ne constituent pas un menu de triche destiné au VPK public. Les vidéos MPEG (`SH4_PLAY_MOVIE`) et les cinématiques animées par le moteur sont deux chemins distincts.

## Inventaire local

```powershell
python tools/cinematic_audit.py --out build/room-audit/cinematics/catalog.json
```

L'outil exige l'exécutable GOG v5 reconnu et lit les archives `demo/*.sh4` de la copie PC locale. La table à `0x650640` contient 182 identifiants, avec un pas de six octets : identifiant signé 16 bits, index de fusion signé 16 bits, stage et octet de remplissage. `0x509670` et `0x5097c0` utilisent cette table. Les préfixes d'archives proviennent des chaînes du même exécutable.

Le conteneur commence par `SH4\0`, puis un nombre d'entrées et des couples **offset 32 bits, longueur 32 bits**. Ce ne sont pas des offsets 64 bits. Les en-têtes de cinématiques portent la signature `DE30`. Sur les données locales, 180 en-têtes passent les contrôles de format ; les identifiants 306 et 308 ont des champs non pris en charge. **Cela ne valide pas leur lecture.**

Le catalogue trouve aussi 32 candidats dont le pilote d'événement utilise le couple commun init/exec vérifié et une liste bornée de ressources. Le stage désigne une archive/un monde, pas une pièce narrative prouvée. Les séquences avec initialisation particulière nécessitent encore une analyse de leur contexte.

Ces observations d'adresses et de format ont été vérifiées dans le GOG. Les noms d'API ont aussi été rapprochés des descriptions de symboles PS2 du [dépôt Silent-Hill](https://github.com/Sparagas/Silent-Hill) ; ses adresses ne sont pas utilisées comme adresses PC. Aucun exécutable PS2 ni fichier du jeu n'est requis dans Git. Catalogues, captures et données extraites restent dans `build/`.

## Lecture contrôlée

```powershell
python tools/cinematic_runner.py pc --out build/room-audit/demo-pc --world 2 --scene 1 --ids 1,8
python tools/cinematic_runner.py vita --out build/room-audit/demo-vita --world 2 --scene 1 --ids 1,8 --eboot build/room-audit/candidate/eboot.bin --symbols build/room-audit/candidate/sh4.velf
```

Le lanceur vérifie d'abord les en-têtes et les pilotes communs dans les données PC locales. Il utilise le parcours d'entrée et la demande de changement de pièce du banc existant, attend la fin des chargements et de toute cinématique naturelle, puis enchaîne les identifiants demandés. Il attend chaque événement `complete`, refuse une liste non prise en charge et respecte `STOP_ALL`. Un seul propriétaire PC et un seul propriétaire Vita sont permis ; l'outil ne remplace pas cette discipline.

Le chemin du jeu est conservé :

- `0x4cfbb0` : préchargement asynchrone de la liste de ressources du pilote ;
- `0x4cd170` : initialisation commune, dont `0x4f1d00` ;
- `0x4cd1b0` : exécution commune, dont la machine d'états `0x4f30d0` ;
- `0x4cd2e0` : fin commune et retour des commandes/caméra ;
- `0x4cfd10` : chemin commun de libération de la liste.

Le hook `0x553c70` conserve la routine originale et l'état CPU de son appelant. Le pilote utilise un objet et un bloc d'événement privés ; il ne s'inscrit pas comme un événement narratif normal. Le chemin est désactivé sans `SH4_DEMO`. Son activation exige `SH4_AUDIT=1`. Le monde doit correspondre à l'archive. Les délais sont bornés et les identifiants/pilotes inconnus sont refusés.

Les captures sont demandées toutes les 120 images logiques, avec des indices à partir de 10001. `--shot-every 0` les coupe pour mesurer la cadence ; `SH4_DEMO_SHOT_EVERY` est l'équivalent côté runtime. Les erreurs GL/GXM et les statistiques restent contrôlables par les options existantes. Les captures perturbent les temps : ne pas prendre leur durée pour le coût normal du jeu.

Sur Vita, le lanceur sauvegarde et relit la sauvegarde originale avant de lancer, archive le binaire remplacé, quitte le jeu avant le transfert, puis restaure uniquement `sh4.sav` et supprime ses variantes `.bak/.new/.prev`. Il restaure le binaire et l'environnement, vérifie les empreintes et relance normalement pour éviter d'attendre dans LiveArea. Les journaux et symboles restent associés au test. Une interruption brutale du propriétaire exige de contrôler la restauration avant de reprendre la console.

## Limites et première qualification (v90)

Les callbacks narratifs spécifiques de fin ne sont pas exécutés à la place de la fin commune. Certains callbacks internes de la cinématique restent ceux du jeu. **La lecture forcée ne reconstitue donc pas l'état narratif**, et le processus de laboratoire doit être quitté avant de rendre la console au joueur. Le lanceur le fait et restaure la sauvegarde. Il ne faut pas sauvegarder une progression issue de ce mode.

Les identifiants 1 et 8 s'enchaînent complètement sur PC et Vita dans le contexte 2:1. Chacun des deux parcours produit 16 captures de cinématique. Les captures examinées montrent notamment Henry assis dans l'escalator avec visage, cheveux et vêtements normaux. Aucun fatal, diagnostic `audit-error` ou code d'erreur GL/GXM non nul n'est relevé dans ce parcours Vita. Le fond de l'identifiant 8 est noir dans ce contexte sur **les deux plateformes** : son contexte narratif n'est pas qualifié, ce n'est pas une preuve de défaut Vita ni un rendu de scène déclaré correct.

Hors fenêtres de capture, les échantillons exploitables donnent 33,33 ms sur 182 images du clip 1 et 33,30 ms sur 121 images du clip 8. La couverture temporelle est limitée par les captures fréquentes ; ce n'est pas une mesure intégrale sans instrumentation. Une lecture sans captures et un contrôle des images intermédiaires sont les étapes suivantes.

Un deuxième parcours Vita active l'interpolation, sans les captures périodiques. La capture brute ponctuelle `SH4_IPSHOT=6000` intervient aux images de rendu 6088/6089 (le compteur interne de rejeu diffère du compteur des présentations). Les deux lectures GPU coûtent environ 2,7 secondes chacune : ces fenêtres sont exclues. Les captures réelles avant/après montrent Henry correctement texturé ; l'image intermédiaire demandée est absente, car l'interpolation était désactivée par son adaptation à cet instant. Ce n'est pas une capture manquante déclarée correcte.

Le clip 1 donne 33,49 ms sur 1 270 images de fenêtres propres ; le clip 8, 33,33 ms sur 181 images. Les journaux de rendu n'indiquent que 28 images intermédiaires sur 1 480 passes du clip 1 et 61 sur 265 pour le clip 8 : **ce parcours n'est pas fluide à 60 images affichées constantes**. Le mécanisme adaptatif reste souvent désactivé après les passages lourds. Aucune erreur GL/GXM non nulle n'est relevée et les deux clips se terminent. La sauvegarde, le binaire et l'environnement d'origine sont restaurés puis le lancement normal est vérifié par son journal.

Le chemin désactivé passe la régression PC de 23 images identiques, y compris après l'ajout du réglage de fréquence des captures. Les 21 tests des outils de pièces/performance passent également. Preuves privées : `build/room-audit/cinematics-v90/`. Cette qualification ne couvre ni les 182 identifiants, ni toutes les transitions de l'histoire, ni les autres consoles des testeurs.
