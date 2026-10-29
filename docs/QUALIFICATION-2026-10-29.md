# Qualification combinée après installation neuve — 29 octobre 2026

## Mise à jour de reprise —09h41Paris

**Modification ultérieure autorisée : minimum continu2heures, puis variété de zones prioritaire.** `after-two-hours.py` est un relais ponctuel qui attend ce seuil, demande un STOP propre au pilote35216, attend sa sortie et vérifie ses preuves avant toute commandeVita. Il lance ensuite une route intercalant les mondes (351cas salle/visite, contextes narratifs connus séparés). Vérifier `handover-owner.json`/`handover-error.txt` ; aucun double pilotage. Le résultat brut decontinuous-soak reste operator-stop avec l'ancien minimum3h nonatteint, conservé et expliqué par `two-hour-endurance.json`. Ne pas l'annoncer comme3h ou192visites terminées. Le cinéma et le reste des386variantes restent à reprendre après cette nouvelle priorité. Revue visuelle et recherche de dérive mémoire restent obligatoires.

Installation et vérification distante intégrale **terminées** ; ne pas les refaire. Contrôleur courant `resume-endurance.py` PID35216/session81960 (vérifier en direct), journaux `resume-endurance.log`/`campaign-owner.json` dans le dossier de preuves. Batterie52%, **en charge**, confirmée parwatchdog ; les anciens avertissements sanscharge ne décrivent plus l'état courant.

Le premier parcours lancé correctement a mesuré7/8destinations,14captures regardées sur `smoke/review.png`, pas de corruption évidente dans ces vues. La8e demande9:2 a expiré en1:2 après le retour narratif du bâtiment5:15 ; ce lot reste en échec de scénario.3122images mesurées, moyenne34,360ms/P9538,206/P9940,846/max99,915 ; tas hôte pic9841Kio, VRAM min44615Kio. Ces chiffres ne valident pas30fps constants, toutes les textures ni l'endurance. Originaux conservés sous préfixe `narrative-smoke-` et dans `smoke/`.

La reprise démarre avec TITLE_SELECT1200:0, sans les injectionsPADIN abandonnées. Elle enchaîne les deux profils chronométrés sur4salles, puis192visites **sans relancer le jeu**, minimum3h réelles exigées ; ensuite9:2 dans un lancement isolé, pilotes cinéma1/8 et20cinématiquesPC qualifiées. La couverture386cas sera organisée **après** ce pilote ; elle n'est pas implémentée dans `resume-endurance.py`. Un succès du pilote n'est donc pas un succès de la campagne entière. Voir le premier bloc de [HANDOFF.md](HANDOFF.md) pour l'ordre exact, les arrêts et les artefacts à reprendre. Le candidat figé n'a pas changé.

## Demande et candidat figé

Valentin autorise explicitement la suppression de l'application SH4, de ses données et de sa sauvegarde sur sa Vita, puis une préparation neuve depuis son installation GOG, une réinstallation et au moins trois heures de tests. Il retient les textures512 et la résolution actuelle640×362. Pas de nouvelle réduction de qualité. Aucun push ni publication dans ce lot.

ZIP local : `build/release/sh4vita-test-2026-10-29-q512.zip` (14265620octets), SHA256 `3c8538bfe29a2edf1c80a795ba7ee5c46ff1090ba45e3ff95f35cb1b528832cb`.

- Eboot combiné : `27d4ff04f2272755b61b09e7e627d90d92ec3704daea2ec715f31a230a369480`.
- VPK : `db2f5a15c79a5ea8e90a37d59646a393a4f24eed63d17387d6ff2c5575e76c26`.
- Symboles : `d4d935eaa27058f21016744cee18a4d56a56475f0ab42bab6aede98dcde3e601`.
- Bibliothèque vitaGL isolée : `2dfd9cc9eda3411c37777e959b472f1bf1818624a51913ecada0455ab6fc6f10`.
- Sources exactes, manifestes, scripts et preuves : `build/room-audit/fresh-combined-20261029/` (ignorés par Git).

Ce ZIP contient le nouveau VPK, les trois préparateurs natifs déjà distribués et compatibles (0.2.0), un petit README anglais et leurs empreintes. Les nouveautés du code source du préparateur0.3.0 ne sont **pas** annoncées comme recompilées pour les trois OS. Aucun fichier GOG, sauvegarde ou environnement de pilotage n'est inclus.

## Ce qui change, avant/après

Le nouveau flag de compilation `SH4_TEST_QUALITY_512=ON`, avec `SH4_TEST_OPTIMIZATIONS=ON`, intègre le profil dans le VPK : TEXMAX256→512, ALPHA32_MAX256→512, backendDXT corrigé actif, PREP_SCALAR/INTERP_SKIP_LARGE/PREFETCH_GUARD/AUDIO_HEAD_CACHE/PREFETCH_SMALL actifs. Les défauts du build normal restent accessibles en compilant sans ce flag. Celui-ci exige la bibliothèque isolée corrigée, identifiée ci-dessus ; ne pas reconstruire ce profil avec l'ancien vitaGL.

Le mixeur audio réutilise son accumulateur8Kio et protège le secours pour les blocs exceptionnels. Les correctifs et autres WIP de menus/commandes/mémoire du dépôt courant sont inclus. Résolution640×362 conservée ; TEX32 global et préchauffage global des shaders non activés. Pas d'audit, de téléportation ni d'entrées automatiques en usage normal du ZIP.

Tests hôte refaits avant campagne :160comparaisonsPCM/états exactes,1000callbacks sans allocation d'accumulateur, refus d'allocation et ownershipDirectSound, petites prélectures et cache de préfixe passent. Build Vita réussi. Les précédentes23/23imagesPC concernent le code audio/rendu avant l'ajout du seul sélecteur de profil Vita ; ce n'est pas une nouvelle qualification graphique combinée sur Vita.

## Installation réellement neuve

Le préparateur distribué est exécuté dans un nouveau dossier sur l'installation `D:/Games/Silent Hill 4`.741fichiers,3632595346octets, langueFrench. Comparaison complète avec les fichiers source effectuée avant effacement.

L'application est supprimée par l'API système `scePromoterUtilityDeletePkg("SILH00004")`. L'utilitaire temporairement employé est restauré octet pour octet. Les881anciennes entrées explicitement sous les cheminsSH4 sont supprimées ; application, données et sauvegarde absentes à ce point, preuve `reset-proof.json`. Une copiePC des derniers journaux/sauvegarde est conservée par précaution, mais **elle ne sera pas restaurée** : la nouvelle instruction de Valentin prime.

L'envoi utilise un seul transfertFTP à la fois, suivi d'une relectureSHA256 de chaque fichier de données. Le VPK est installé via promotion système du contenu exact du paquet, puis les fichiers installés sont relus. Ne pas confondre préparationPC réussie, transfert en cours et installation terminée : `install-result.json` est le jalon exigé pour lancer les tests.

La première copie a été arrêtée pendant la phase de données uniquement, après restauration complète de l'utilitaire, pour faire tourner VitaShell au premier plan pendant le long transfert et éviter une attente prolongée surLiveArea. Le transfert reprend les tailles complètes ; la relecture intégraleSHA256 suivante reste obligatoire. Aucun test du jeu n'était actif à cette interruption.

## Séquence unique et critères

Le parent `run-all.py` enchaîne installation puis `campaign.py`, sans tâche planifiée. Propriétaires réels : `sequence-owner.json`, `owner.json` pour le transfert et `campaign-owner.json` pour les tests. Respecter STOP_ALL et le fichierSTOP local. Aucun autre piloteVita ne doit intervenir pendant cette séquence.

1. Démarrage normal froid sans configuration de développement : lecture du journal, absence deFATAL et d'audit actif.
2. Huit destinations exigeantes ou utiles, captures avant/après mesure et diagnosticsGL/GXM actifs : Eau, Passé, métro, hôpital, appartement, forêt, bâtiment, monde final. Puis paire chronométrée en mouvement sur quatre destinations, sans captures : mêmes binaire/qualité, sélecteurs récents scalar/interpolation/préfixe/petites prélectures désactivés puis activés. Cette paire n'est pas une comparaison complète avecmain.
3. Deux cinématiques pilotes via leurs événements communs d'origine, avec préchargement des ressources et captures ; leurs déclencheurs narratifs ne sont pas reproduits.
4. **Un processus continu sans redémarrage**,192visites des quatre destinations déjà utilisées pour l'endurance (Eau, Passé, métro, hôpital), mouvements et caméra par véritables axes injectés, sons actifs.345600images logiques de séjour demandées et durée réelle minimale10800secondes exigée. Le bâtiment5:15 déclenche spontanément une cinématique qui ramène ensuite dans l'appartement, également reproduit surPC : il reste dans les visites courtes, mais pas dans cette boucle longue indépendante de la progression. Mémoire hôte/invitée/graphique, temps d'image et longues pauses relevés. Une durée insuffisante n'est pas validée.
5. Élargissement aux groupes de cinématiques dont le contexte a effectivement terminé surPC (parmi32candidats à événement commun). Un contexte refusé ou incomplet surPC est consigné, pas présenté comme défaut graphiqueVita. Puis386cas de salles/visites/fonds du catalogue, par groupes de20 afin de séparer la couverture graphique de l'endurance continue. Captures attendues et complétions vérifiées.

Le runner lit le journal par morceaux et garde les preuvesPC, sans reparcourir un énorme journal à chaque interrogation. Une erreur fatale, allocation refusée, blocage, timeout, nouveaucore ou capture manquante arrête la séquence ; pas de relance cachée permettant de présenter une passe en échec comme réussie. Les sources, le binaire et les réglages du candidat restent figés pendant les comparaisons.

## Limites et remise en main

Premier essai de smoke arrêté : le script clavier historique n'a pas démarré une partie depuis cet état neuf et reste dans les menus0:0, puis timeout du laboratoire. Aucun crash du jeu ni arrivée validée ; preuves conservées sous suffixe`initial-startup-failed`. Diagnostic de démarrage : le chemin normal de sélectionNewGame0 à1200 fonctionne, texte français et chambre capturés. La reprise ajoute les confirmations au pad et le secoursTITLE_SELECT1800:0 ; aucune modification du VPK. Le défaut du scénario ne compte pas dans les trois heures requises. Alimentation observée60puis57% sanscharge : demande de branchement envoyée à Valentin, arrêt prévu sous10% si elle ne charge pas.

Les captures restent **à examiner réellement**. Leur présence ou des logs sans erreur ne prouvent pas l'absence de corruption de textures. Le test n'est pas un parcours narratif intégral, et la caméra/téléportation artificielle peut créer un contexte non représentatif. Les durées de chargement, captures, cinématiques, logique et interpolation sont distinguées dans l'analyse. La latence physique bouton-écran n'est pas mesurée par ces journaux ; seuls les temps d'image/pipeline et la réponse au pilotage sont observables ici.

Après réussite, retirer env/ctl/key/shotnow/tour, garder le nouveau VPK, ne pas remettre l'ancienne sauvegarde, lancer normalement. Un échec conserve les preuves et demande une analyse ; ne pas déclarer le paquet stable. Faire le rapport final après les mesures **et** la revue visuelle.
