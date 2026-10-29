# Revue ciblée des allocations — 29octobre2026

Revue statique complémentaire à la qualification du candidat27d4ff04. **Aucune modification du moteur ni du candidat dans cette revue.** Un chemin sans garde est un défaut potentiel sous échec d’allocation ; il n’identifie pas la cause d’un dump sans correspondance de pile. Les crashes testeur déjà attribués au mélangeur audio restent décrits dans TESTER-CRASH-2026-10-29.md.

## Déjà protégés dans la source et le candidat

- Accumulateur normal du mixeur :8Kio réutilisables, aucun calloc par callback normal ; échec du secours sur grand bloc traité.355PCMréels mélangés et comparés ; blocageNULLdu dump corrigé dans ce chemin précis.
- CreateSoundBuffer : contrôle DSBuf, allocationPCM invitée et compteur de partage ; rollback avant publication de l’objet en cas d’échec.
- CreateVertexShader : retourne0 si l’objethôte ne peut pas être alloué.
- Réserve de samples vidéo : allocation transactionnelle avec libération des nouvelles structures si échec ; réservesPCM conservées.
- Tampontexture vide et sortiesYUV : échecs vérifiés. Le premier peut abandonner l’upload, donc rester visible comme défaut de texture : une garde contre crash n’est pas une texture valide.
- Anneauprep : contrôle d’allocation et fatal explicite. AnneauGL : création du thread conditionnée au tampon/sémaphore ; interpolation conditionnée aux deux tampons.

## Chemins à durcir après la qualification figée

| Chemin | Constat dans la source | Suite concrète |
|---|---|---|
| prep.c,prep_gfree | résultatrealloc affecté directement àgf puis écriture, capacité modifiée avant succès | préserver ancien pointeur/capacité ; organiser un secours borné compatible avec le threadpropriétaire du tas invité ; ne pas libérer le tas invité depuis le consommateurGL |
| prep.c,prep_gdrain | même problème surgd avantmemcpy ; seconde réserve seulement pour déplacer une liste | étudier échange des listes sousmutex pour éviter cette seconde allocation ; prouver ordre/unicité deslibérations et absence de deadlock souspression |
| prep_init,pd | calloc puis copie deDevice sansgarde | traiter l’échec avant publication de la file/thread ; vérifier également sémaphore et création du thread |
| resource.c,surf_create/tex_create/buf_create | structures et table des niveaux déréférencées sans contrôlemalloc systématique | rollback multi-niveaux, sortieHRESULT dans les créateursD3D ; aucun objetCOM partiellement publié |
| shader.c,ps_create | callocPShader non vérifié avant écriture | retour0 et propagationHRESULT cohérente ; contrôler aussi la sortie de traduction |
| dsound.c,listener/DirectSoundCreate8 | deux allocationshôte sans garde avantcom_wrap/déréférence | E_OUTOFMEMORY avec sortieNULL ; initialisation audio/COM cohérente |
| movie.c,mkobj/movie_new/EnumPins | plusieurscalloc non vérifiés dans l’assemblage du graphe | construction transactionnelle, déconstruction d’un graphe partiel, HRESULT propagé ; ne pas changer le candidat en cours |

## Durée de vie des graphes vidéo

`movie_cocreate` joint les threads précédents et libère les décodeurs, mais laisse explicitement l’ancien graphe et ses objetsCOM. Les méthodes génériquesRelease bornent les références à1 au lieu de détruire. C’est une rétention visible dans le code, **pas une croissance mesurée sur la campagne actuelle** : les14films sont lancés séparément et ne valident donc pas une boucle de graphes dans un seul processus. Il faut une série répétée sans redémarrage, compteurs pargraphe et vérification des références possédées par le rendererinvité avant suppression. Ne pas annoncer arbitrairement le volume réel ni relier ce chemin à tous les crashes après sessionlongue.

## Prélecture : plafond sain, arbitrage à mesurer

Le budgetadvisoire2×1310720octets est atomique et borné. Le refus d’une troisième réserve est traité sans déréférencementNULL. En revanche, conserver la capacité de lecteurs inactifs peut empêcher une autre prélecture et réintroduire des lectures synchrones : instrumentation requise avant d’attribuer les longues pausesSE à ce cas. Voir QUALIFICATION-ANALYSIS-2026-10-29.md. Ne pas relever simplement le plafond sur une console déjà contrainte en mémoire.

## Validation requise pour chaque futur correctif

Injecter des échecs à chaque point d’allocation concerné ; vérifier sortie, ancienpointeur, référencesCOM et ressources libérées exactementunefois. Pour la fileprep, contrôler concurrence et séquencefilepleine/drain/arrêt. Puis régressionPC et candidatVita distinct, avec reprise des seuls parcours affectés. Le binaire27d4ff04 et ses preuves restent immuables.


## Candidat ultérieur : pileCPUvidéo et slotsYUV

Après le constat initial, un banc reprenantrt_new_cpu/Stop/nettoyage a caractérisé16842752octets invités conservés sur16cycles (piles1Mio+TIB4Kio). Nouveaucode source, **absent du candidat27d4encours** : propriétédes allocations stockéehorsCPU, `rt_free_cpu` aprèsjoin, libération des5slotsYUV aprèsStop ; Pause garde les ressources, décodeurs gardent position/durée jusqu’au nettoyage normal. Tests :2000cyclesCPU, échecalloc/TIB/pile, étatmain et propriétéTIBaltérable ;1000cyclesvidéo, ordrejoin→free, idempotence. Build et régressionPC/Vita à qualifier avant installation. À640×480, les5slotsYUVreprésentent2304000octets ; c’est un volume théorique du code, gainréelVita à mesurer. Les objetsCOM encore retenus ne sont pas corrigés par ce changement.
