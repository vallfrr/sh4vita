# Reprise du projet

Avant de modifier le code, lancer un test ou utiliser la Vita, lire **[docs/HANDOFF.md](docs/HANDOFF.md)**, puis **[docs/STATUS.md](docs/STATUS.md)**. Le handoff indique l'état courant ; les anciennes sessions et leurs « next session » sont historiques.

- Vérifier les processus réels, leurs commandes et leurs journaux. Un PID consigné peut être terminé ou réutilisé. Ne jamais concurrencer un propriétaire actif.
- Respecter `build/room-audit/STOP_ALL`, les sauvegardes et leurs empreintes. Les instructions récentes de Valentin priment sur les documents.
- Distinguer source, candidat compilé, version installée, paquet des testeurs et publication. Ne pas annoncer de gain ou de validation sans preuve.
- Actualiser `docs/HANDOFF.md` après un changement de propriétaire, de candidat, de résultat important ou de prochaine étape. Garder les valeurs précédentes et les replis.
- Français avec Valentin. Aucun sous-agent, automatisme récurrent ou publication pendant la campagne courante. Commits éventuels uniquement sous `vallfrr <valentin.fortier51110@gmail.com>`, auteur et committer, sans co-auteur ajouté.
- Ne pas versionner les fichiers du jeu, `port/gen/`, `re/out/` ni les preuves sous `build/`. Ne pas écraser les changements préexistants.
