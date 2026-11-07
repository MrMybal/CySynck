---
id: git-dehydration
title: Restaurer les pointeurs LFS commités
description: Préserver les assets inchangés avant de préparer une mise à jour Git.
section: Git
order: 40
parent: git-materialization
sourceRevision: 1
---

# Restaurer les pointeurs LFS commités {#restoring-committed-lfs-pointers-039}

Les assets développés bloquent normalement l’intégration Git à pointeurs seuls.
**Préparer les pointeurs pour les mises à jour Git** est une étape explicite : préserver les assets développés inchangés en récupération locale et restaurer les octets exacts du pointeur courant.
Prévisualiser ensuite [l’intégration à pointeurs](doc:git-lfs-integration), appliquer la branche puis [matérialiser](doc:git-materialization) séparément les assets nécessaires depuis le cache vérifié.
Ces opérations sur la branche et les fichiers sont des étapes distinctes avec revue.

```sh
cysynck git dehydrate PROJECT_ID
cysynck git dehydrate PROJECT_ID --paths-file selection.json
cysynck git dehydrate PROJECT_ID --paths-file selection.json --apply true --plan PREVIEW_TOKEN
```

`selection.json` reprend les chemins relatifs exacts ou préfixes de dossiers finissant par `/` de la matérialisation.
L’omettre inspecte tous les pointeurs commités. Sélecteurs inconnus/dangereux sont refusés.
Fichiers modifiés, supprimés, stagés, en lecture seule ou à flags spéciaux sont signalés/conservés ; les pointeurs déjà présents sont ignorés.
Les autres assets éligibles sélectionnés restent traitables. Seuls les octets correspondant exactement à la taille et au SHA-256 du pointeur commité sont éligibles.
Aucun helper LFS, filtre, téléchargement ou accès au cache.
POST authentifié `/api/projects/ID/git-materialize` reçoit `{"operation":"dehydrate","paths":["Assets/"]}`, puis `apply:true`/`plan`.
Le défaut `operation:"materialize"` conserve l’expansion. Répéter même opération et sélection à l’application ; l’ABI C/C++ version 1 utilise cette route JSON.
L’aperçu donne `operation`, `can_dehydrate`, `pending`, `pointers` déjà présents, `skipped`, `excluded`, `bytes` développés à préserver, états des fichiers et plan opaque.
`can_materialize` est faux pour cette opération. Le jeton lie profil/projet, HEAD/référence, index binaire/flags, configuration, sélection et empreintes observées.
Source changée, sélection différente, projet renommé ou autre opération exigent un nouvel aperçu.
L’application réserve le verrou Git existant, refait l’inspection et crée exclusivement tous les pointeurs préparés sous `.cysynck-lfs/OPERATION/after/`, les flush puis recontrôle Git.
Chaque asset inchangé est déplacé vers un `before/` correspondant absent, conservant ses octets, date et permissions.
La publication du pointeur copie permissions/DACL Windows, refuse une destination existante et vérifie le résultat.
HEAD, branche, index et configuration restent identiques. L’espace supplémentaire couvre seulement les petits pointeurs ; le déplacement des assets doit rester sur un système de fichiers permettant la publication.

## Reçus et interruptions {#receipts-and-interruptions}

Le catalogue commun indique `operation:"dehydrate"`. Les nouveaux reçus utilisent le schéma 2 dans `STATE/git-materializations/PROJECT/ID.json` ; la matérialisation garde le schéma 1.
Utiliser 0.39+ pour lire le schéma 2 ; les anciens clients le refusent comme inconnu.
Les reçus schéma 1 restent lisibles, même sans champ `operation`.

```sh
cysynck git materializations PROJECT_ID
cysynck git materialization-check PROJECT_ID OPERATION_ID
cysynck git materialization-acknowledge PROJECT_ID OPERATION_ID --plan CHECK_TOKEN
```

Le contrôle valide les métadonnées sélectionnées contre le commit immuable enregistré puis hashe fichiers de travail, assets développés préservés et pointeurs préparés connus.
Ici `before` signifie assets encore présents, `after` signifie pointeurs de travail et originaux développés inchangés en récupération.
Entrées inconnues, corruption et liens bloquent le contrôle.
Un reçu incomplet ne peut être examiné qu’à un état exact avant/après vérifié, en conservant statut et erreur. La reprise de publication ne s’applique pas à cette opération.
Aucune restauration, annulation, nouvelle tentative, suppression d’assets ou retrait de verrou automatique.
Examiner reçu et fichiers avant récupération manuelle. Avancer HEAD change le contexte original du reçu sans supprimer les assets préservés.
Les conserver tant que leurs versions sont utiles.
[L’export des assets préservés](doc:git-asset-export), depuis 0.40, les copie après vérification dans un nouveau dossier séparé, même après changement de HEAD.
Remplacer un fichier de travail par une version enregistrée reste une décision manuelle après vérification de l’identité commitée courante.
CyUI/Web exposent sélection, aperçu/application et résultats des reçus propres à l’opération en anglais/français.
Suspendre les écritures externes ; le verrou CySynck ne verrouille pas les autres applications.
L’exécution native Linux/macOS, les coupures, les gros assets réels et les interactions des nouveaux contrôles restent à valider.
