---
id: git-lfs-integration
title: Avance rapide avec pointeurs LFS
description: Mettre à jour la branche en gardant les assets sous forme de pointeurs commités.
section: Git
order: 36
parent: git-integration
sourceRevision: 1
---

# Avance rapide avec pointeurs LFS {#reviewed-fast-forward-with-lfs-pointers-034}

La politique explicite de pointeurs seuls permet à une branche importée de mettre à jour une branche locale propre ou sans premier commit contenant des pointeurs LFS canoniques.
Elle convient aux imports initiaux et serveurs qui développent les assets plus tard. La politique par défaut refuse toujours LFS.
La fusion divergente LFS et le remplacement automatique des assets développés ne sont pas implémentés.
[La restauration contrôlée des pointeurs](doc:git-dehydration) prépare séparément les assets inchangés en conservant leurs octets dans la récupération.

```sh
cysynck git integrate PROJECT PEER main --lfs-policy pointers
cysynck git integrate PROJECT PEER main --lfs-policy pointers --apply true --plan PREVIEW_TOKEN
```

POST authentifié `/api/projects/PROJECT/git-integrate` accepte `lfs_policy:"pointers"` ou `"reject"` (défaut).
Répéter exactement cette politique à l’application. Le plan lie profil/projet, révisions, configuration, index, octets suivis, métadonnées locales et politique.
`project_hash` fixe aussi tous les paramètres enregistrés ; les changer exige un nouvel aperçu pour chaque type d’intégration.
Les vérifications de branche, chemins, verrou, premier import et absence d’écrasement restent actives.

## Résultat du checkout {#resulting-checkout}

Branche et index avancent vers la tête importée. Les fichiers de travail contiennent exactement le texte des pointeurs commités.
Aucun objet de cache créé, téléchargement, helper LFS ou filtre configuré. La politique d’export `git_lfs` reste indépendante.
L’import de bundle peut avoir déjà rempli le cache par son workflow LFS distinct.
Après intégration, [vérifier/recharger le cache](doc:git-cache) puis préparer [la matérialisation](doc:git-materialization) des pointeurs depuis les octets locaux vérifiés.
Cette expansion a son aperçu, reçu et originaux préservés ; elle ne forme pas une transaction avec l’intégration.
Les fichiers suivis doivent correspondre aux octets bruts commités. Assets développés, changements locaux/stagés et flags assume-unchanged/skip-worktree bloquent l’intégration suivante.
Les préserver/réconcilier avec un workflow adapté ; la restauration explicite des pointeurs traite les assets développés inchangés.
L’intégration elle-même ne déshydrate, ne stash et n’écrase pas automatiquement ces fichiers.

## Attributs et limites {#attributes-and-bounds}

- Seulement des pointeurs v1 canoniques terminés LF avec identité SHA-256 ; formes malformées, anciennes ou étendues refusées.
- `filter=lfs` est permis seulement quand le blob commité est un pointeur canonique.
  Les autres filtres et conversions texte/encodage/EOL/ident restent refusés.
  `filter=lfs diff=lfs merge=lfs -text` fonctionne pour l’avance rapide, sans exécuter de pilote de fusion.
  La politique n’existe pas pour `git-merge` divergent.
- Au plus 10 000 fichiers réguliers par arbre, 1 Gio par blob Git ordinaire et 16 Gio de blobs.
  LFS ajoute 4 096 identités uniques, 4 Gio par asset et 16 Gio d’octets référencés uniques par arbre.
  Des tailles différentes pour le même SHA-256 dans un arbre sont refusées.
  Ces inventaires avant/après sont distincts des contrôles d’historique des échanges/cache.

Résultats et aperçus exposent `lfs_policy`, `lfs_before`, `lfs_after` : `files` compte les chemins de pointeurs,
`objects` les identités uniques, `bytes` les octets uniques référencés. Un asset partagé ne compte qu’une fois dans objets/octets ; les assets vides comptent comme objets.
`files_before`/`files_after` annotent l’identité et la taille `lfs` de chaque pointeur.

## Reçus et interfaces {#receipts-and-interfaces}

Historique d’intégration, contrôles des reçus et revue d’interruption fonctionnent avec cette politique.
Ils recalculent les arbres de pointeurs depuis les commits et valident les compteurs LFS.
Les reçus décrivent l’état de checkout des pointeurs. Une matérialisation ultérieure change les octets de travail et apparaît comme différence au contrôle d’intégration ;
utiliser le reçu de matérialisation pour les assets développés et pointeurs préservés.
CyUI/Web proposent une case décochée par défaut, l’explication et les compteurs en anglais/français.
CLI et C/C++ emploient l’API et l’ABI C version 1 existantes. L’exécution native Linux/macOS/Unreal et l’interaction réelle des contrôles restent à valider sur leurs cibles.
