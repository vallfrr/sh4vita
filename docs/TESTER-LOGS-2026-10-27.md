# Retours testeur — journaux 4 à 7

Les journaux originaux sont conservés hors Git dans le dossier Downloads de Valentin. Le testeur indique utiliser le VPK v83 ; ces journaux ne contiennent pas d'empreinte du binaire permettant de vérifier indépendamment la version installée.

## Journal 7 : épuisement du tas invité confirmé

Après environ 3 515 secondes, pendant des chargements où apparaissent notamment `data/bd15.bin` et `demo/bd.sh4`, plusieurs allocations de 256 Kio et plus petites échouent ; le tas invité est indiqué à 145 Mio. L'échec fatal suit : `D3D buffer: guest memory exhausted (410 bytes)`. Les messages peuvent s'entrelacer entre les fils, donc leur ordre dans le journal ne suffit pas à reconstituer chaque opération. La VRAM libre relevée peu avant reste d'environ 51 Mio ; c'est le tas invité qui est signalé plein.

Les refus d'allocations de textures proches de l'arrêt rendent une disparition de ressources plausible dans cet épisode. Ils ne prouvent pas la cause des glitches de cinématiques rapportés à d'autres moments. Le journal ancien ne contient pas le détail du plus grand bloc libre, donc il ne permet pas de séparer épuisement et fragmentation. Le parcours récent sur notre Vita a, lui, mis en évidence des refus de grandes allocations liés à la fragmentation ; cela concerne une autre exécution et une configuration expérimentale à 56 Mio.

## Journaux 4, 5 et 6 : cause non établie

Leurs dernières lignes sont des messages ordinaires de rendu, lecture ou audio, à environ 2 285, 2 265 et 2 145 secondes respectivement. Aucun `FATAL` ni échec d'allocation n'y est enregistré. Une fin de journal sans message d'erreur ne permet pas de décider entre crash natif, arrêt externe ou autre interruption. Demander pour chaque incident le fichier `ux0:data/psp2core-*.psp2dmp` s'il existe, son code d'erreur, la scène et les circonstances ; un dump ne peut être symbolisé correctement qu'avec les symboles correspondant au binaire exact.

Les quatre journaux montrent des chargements de programmes depuis le cache livré. Cela montre son utilisation, sans prouver qu'il est responsable des glitches ou qu'il est innocent. Demander une courte vidéo ou capture datée, la cinématique, le moment d'apparition et si le défaut persiste après redémarrage ; récupérer le journal de la session correspondante.

## Journal 8 : arrêt sans message fatal

Le nouveau journal se termine après environ 3 020 secondes, peu après l'ouverture de `data/di38.bin` puis `demo/hs.sh4`. Il contient 116 chargements de programmes depuis le cache, mais aucun `FATAL` ni échec d'allocation signalé. L'utilisation du tas invité et la cause de l'arrêt ne sont pas connues dans ce journal. Une éventuelle vidéo de la scène et le dump `psp2core` correspondant, s'il existe, sont nécessaires pour progresser sur cet incident précis. La dernière ressource ouverte n'identifie pas à elle seule la cause du crash.

## Journal 9 et quatre fichiers de dump reçus le 28 octobre

Le journal 9 se termine après environ 1 415 s par `FATAL: D3D buffer: guest memory exhausted (4928 bytes)` : une allocation du tas invité a effectivement échoué, sans désigner la pièce comme cause. Le journal ancien ne contient pas la taille du plus grand bloc libre. Sur un tour PC indépendant limité à 166 Mio avec l'arène originale de 64 Mio, le tas atteint réellement 0 Kio libre après de nombreux chargements ; ce cas reproduit une pression mémoire, mais pas forcément l'histoire exacte du testeur. Réduire l'arène à 56 Mio a déjà provoqué des refus de grandes textures sur Vita, donc ce réglage ne constitue pas un correctif qualifié.

Les trois dumps complets indiquent une exception d'accès dans un fil `sh4pf` et la même adresse relative ; le quatrième fichier `.tmp` est tronqué. Les instructions lues dans le VELF archivé ne permettent pas une attribution fiable à une fonction précise pour ces dumps : ils ne prouvent pas à eux seuls que le défaut est corrigé par le changement d'I/O depuis v83, ni qu'il s'agit du même incident que l'épuisement mémoire. Les trois analyses sont conservées dans `build/room-audit/tester-dumps-20261028/` ; ne pas attribuer une cause sans correspondance vérifiée entre dump et symboles.
