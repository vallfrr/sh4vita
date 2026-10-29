# Pauses de lecture des archives — essai v110

Branche expérimentale, 27 octobre 2026. `SH4_PREFETCH_REUSE=1` active cette optimisation ; elle reste désactivée par défaut. Le format des données, les sauvegardes et le cache de shaders ne changent pas.

## Travail supprimé

Pendant le parcours avec sticks dans Past 8:24, le jeu relit environ toutes les 214 images les mêmes blocs de `demo/bgm.sh4` et `demo/cm.sh4`. Le préchargement existant apprend correctement cette répétition, mais `pf_start` arrêtait le job et rouvrait l'archive pour relire le bloc qu'il détenait déjà.

La nouvelle voie conserve uniquement un job/buffer dont **l'offset et la taille sont exactement ceux de la prochaine prédiction**, après application du plafond habituel. Elle accepte un job encore en cours ou un job terminé avec toutes les données demandées. Un job terminé sur lecture courte est réessayé par le chemin habituel. Tout changement de plage remplace le job comme auparavant, et la fermeture attend le job avant de libérer le tampon.

L'option concerne seulement les handles ouverts en lecture seule vers des archives `.sh4` et `.pac` du jeu. Les sauvegardes, configurations et handles en écriture gardent leur politique. Il n'y a aucun nouveau tampon de ressources ; le plafond global existant de `2 × PF_MAX`, soit 2,5 Mio, reste inchangé. Une impossibilité d'allouer conserve le repli existant.

## Qualification

`port/test/prefetch_reuse_test.py` compile temporairement la structure `File`, la politique et la fonction `pf_start` réellement présentes dans le code, avec des frontières contrôlées pour les jobs et l'allocateur. Ses 128 combinaisons exercent 11 conservations et 117 remplacements. Les vérifications couvrent les octets retenus, les jobs en cours/terminés, les lectures courtes, le plafond de taille, la politique d'extension/lecture seule et les échecs de budget/allocation.

Avec l'option active et les voies natives de collision/modèles, la régression PC finale donne **23 images identiques**. La paire Vita utilise le même binaire, les mêmes sticks, une fenêtre de 1 500 images logiques et le maintien des PV du banc ; seul le drapeau de conservation diffère. Les douze poses relevées et les deux captures sont identiques.

## Mesures Vita

| Mesure sur le parcours | Sans conservation | Avec conservation |
|---|---:|---:|
| Temps mural moyen, fenêtres hors captures | 33,470 ms | 33,304 ms |
| Images dans ces fenêtres | 1 389 | 1 399 |
| Images ≥ 50 ms dans la fenêtre de parcours, hors captures | 14 | 8 |
| Plus longue image hors captures | 88,4 ms | 88,8 ms |
| Lecture servie par préchargement, bloc BGM de 542 Kio | 4,7–5,7 ms | 2,7–3,2 ms |
| Lecture servie par préchargement, bloc CM de 320 Kio | 12,8–18,6 ms | 1,5–4,4 ms |

Ces durées de lecture incluent le service du préchargement et sa replanification, pas seulement le temps d'accès au stockage. Le gain moyen est faible ; l'intérêt observé est de retirer six des pointes au-dessus de 50 ms dans ce parcours. Les images à froid restent proches de 75–89 ms. D'autres pointes de 50–60 ms, sans lecture comptabilisée sur le fil principal, restent présentes à chaque répétition : leur traitement n'est pas encore localisé. **Ce n'est pas un correctif général de tous les freezes ni une preuve de 30 fps constants.**

Les preuves sont dans `build/room-audit/stream-retain-v110/` : binaires/symboles, configurations, journaux, fenêtres, séquences de lectures, poses et captures. Le runner a restauré et relu la sauvegarde originale (SHA-256 `04315d28a0639804cf37c1c9336278a8ae46a300133ae1daf58c4c99dcccca38`), le binaire normal (`a00ef94dbab228565dedbb188a802ae12ef5c21f9e487b65d4c1e995b3789527`) et l'environnement, puis relancé normalement.

Un profil isolé limité au son et aux fichiers doit maintenant localiser le traitement périodique restant. Les captures et ce scénario ne garantissent pas tous les états du jeu. Le VPK public et le ZIP des testeurs restent inchangés.
