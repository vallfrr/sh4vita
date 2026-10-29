# Campagne de jeu automatisée : interactions et transitions

## Demande et distinction des preuves

Valentin demande de solliciter au maximum les chemins du vrai jeu : chargements, portes, objets, cinématiques, sons, sorties d'événement et retours au contrôle. Les téléportations servent à préparer une scène ; elles ne remplacent pas l'exécution de l'interaction. Le but est un parcours massif reproductible, pas uniquement un atlas de captures. Les testsVita déjà en cours gardent leur candidat figé et leur propriétaire unique.

Trois résultats différents doivent rester séparés : ressource chargée, événement réellement exécuté, état d'histoire correctement reproduit. Un appel forcé à la fonction de fin d'une énigme ne valide pas l'énigme. Lancer les182IDs cinéma à la suite avec tous les drapeaux activés ne serait pas une reproduction cohérente du jeu.

## Travail commencé le29octobre

Nouveau banc `tools/gameplay_scenario.py` : exécutionPC bornée avec le binaire courant `build/pc/sh4.exe`, empreinte, configuration et preuves par essai. Il refuse un autre jeuPC actif, protège/restaure les sauvegardes du miroirPC, respecteSTOP_ALL et conserve les captures. Il ne touche jamais à la Vita. Arrêt sur fatal, absence de progression, dépassement du temps ou sortie inattendue. Les actions sont injectées par le système d'entrée existant ; les transitions et messages sont ceux du jeu.

Premier pilote courant : `fresh-combined-20261029/gameplay-pc-tunnel04`, sessionoutil49006, vérifier le processus réel avant un autre essaiPC. Placement dans le bâtiment, Action/confirmation, caméra et chargement normaux du tunnel4 puis sortie. Les prédicats exigent la présence en10:4 puis un retour hors du tunnel. La fin de7100images seule ne suffit pas ; captures encore à examiner. Le test utilise un départPC neuf, pas une restauration de l'ancienne sauvegarde de Valentin surVita.

**Résultat ensuite obtenu : pilote4 terminé en136,18s, entrée10:4 puis retour1:2 confirmés, aucune erreur enregistrée.33captures examinées : proposition d'entrée, intérieur texturé du tunnel et appartement au retour ; pas de nouvelle corruption évidente. SauvegardesPC restaurées et vérifiées.** La série des dix autres routes est lancée via `gameplay-pc-batch.py` (propriétaire dans `gameplay-pc-owner.json`, résultats dans `gameplay-pc-results.json`, journal `gameplay-pc-batch.log`). Elle s'arrête au premier contexte non prouvé ; ne pas lancer de deuxième jeuPC en parallèle. Cette série est indépendante des pilotesVita et ne prouve rien sur leurs performances matérielles.

Manifestes sous `build/room-audit/fresh-combined-20261029/gameplay-plan/`, ignorésGit, extraits des inventaires de l'exécutableGOG supporté :

| Famille | Inventoriés | Qualification à faire |
|---|---:|---|
| Cas salle/ressource/fond |386 | chargement et revue du candidat récent |
| Enregistrements d'entrée de porte |578 | interaction source, conditions, destination et aller-retour |
| IDs de cinématique |182 | contexte, chargement des acteurs, exécution, fin et retour |
| Traversées normales des trous |11 | routes historiquement vérifiées, reprise du code actuel |

**1157enregistrements ne signifient pas1157tests passés ni1157interactions distinctes.** Les entrées de portes se répètent entre salles et ne définissent pas à elles seules les liaisons sortantes. Le catalogue contient180en-têtescinéma valides et32candidats à pilote commun, dont20IDs ayant terminé dans le préflightPC de ce jour ; les pilotes spécifiques exigent du travail supplémentaire. Les états de première visite/revisite ne reconstruisent pas les prérequis narratifs.

`scenarios.json` conserve les statuts par famille et les limites ; `normal-tunnel-01..11.json` réutilisent les configurations normales historiquement observées, avec le sélecteurNewGame1200:0 actuellement fiable. Ne pas lancer les anciens scriptsVita qui remettent d'autres binaires et sauvegardes.

## Développement et ordre de qualification

1. Terminer le minimum d'endurance autorisé de2h, puis chargements variés déjà préparés. Ne pas interrompre ni concurrencer les propriétaires35216/relais22032 ; vérifier les commandes réelles.
2. QualifierPC les routes d'interaction déjà connues avec le binaire actuel ; commencer par les11trous, puis portes/échelles du même contexte. Enregistrer la situation initiale, l'action et l'état effectivement obtenu.
3. Étendre la découverte : `SH4_AUDIT_PLAY=1` expose `audit-play` et `audit-action` (descripteur,kind,position). Lister les candidats ne prouve pas leur exécution ; la trace actuelle est limitée aux8premiers nœuds par prélèvement. Ne pas prétendre avoir inventorié tous les objets à partir de cette liste seule.
4. Pour chaque famille objet/porte/énigme : identifier le véritable déclencheur et ses prérequis depuis l'exécutable, préparer un état isolé, appeler le chemin d'interaction ou injecterAction, attendre un résultat vérifiable (message,inventaire,destination,événement). Garder les refus normaux : porte verrouillée, objet manquant ou événement déjà fait ne sont pas automatiquement des bugs.
5. Associer les cinématiques aux déclencheurs et à leurs ressources ; tester chargement, lecture, annulation si supportée, fin et reprise du jeu. Les32pilotes communs ne couvrent pas les événements spécifiques ; ne pas forcer ces derniers sans leur initialisation.
6. Réunir les segments prouvés dans des boucles variées **sans redémarrage du jeu**, avec checkpoints explicites entre segments incompatibles. Ajouter cinéma→jeu, objet→transition, mort→chargement et retourmenu quand ces états seront reproductibles.
7. SurVita, rejouer les segmentsPC qualifiés sous un propriétaire unique et un candidat identifié. En cas de changement du code de laboratoire nécessitant un nouvel eboot : finir/archiver le lot courant, donner un nouveau nom/hash au candidat, puis lancer le nouveau lot. Ne pas attribuer les mesures de l'ancien au nouveau.

Chaque essai doit conserver les temps de chargement, temps d'image horscaptures, longs intervalles, évolution mémoire parpool, programmes chargés, erreurs, captures et identifiant d'événement. Le rapport final séparera réellement exécuté, refusé pour contexte, non implémenté et non testé. Aucune promesse d'exhaustivité de tous les états de progression.
