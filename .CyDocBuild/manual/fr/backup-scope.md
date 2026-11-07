---
id: backup-scope
title: Périmètre et exclusions des sauvegardes
description: Prévisualiser les fichiers inclus et enregistrer des exclusions après revue.
section: Sauvegardes
order: 11
parent: backups
sourceRevision: 1
---

# Périmètre et exclusions des sauvegardes {#backup-scope-and-exclusion-rules-031}

La vue native **Périmètre des sauvegardes** et la section Web indiquent quels fichiers du projet seraient sauvegardés.
Modifier le brouillon des exclusions, prévisualiser son effet, puis enregistrer explicitement les règles examinées.
L’aperçu ne change pas les paramètres, n’appelle aucune extension, ne lit pas les contenus, ne crée pas d’objet et ne publie aucun snapshot.
Après l’enregistrement, refaire un aperçu pour activer **Sauvegarder le périmètre examiné** sur les règles sauvegardées.
Les interfaces démarrent en anglais et proposent le français.

## Règles et sous-dossiers exclus {#rules-and-pruning}

`*` correspond à zéro ou plusieurs octets, y compris `/`, et `?` à un octet. Une règle correspond au chemin relatif complet ou à un composant du chemin.
La comparaison distingue la casse, sauf le traitement Windows existant des composants intégrés `.git`, `.stfolder`, `.stversions` et `.stignore`.
Les noms Unicode sont acceptés, mais `?` compte des octets UTF-8 plutôt que des caractères Unicode.
Il n’existe ni négation Git-ignore, ni syntaxe de commentaire, ni échappement, ni sens spécial pour une barre oblique finale. `!` et `#` restent littéraux.

La première règle correspondante est indiquée par `rule_index`, à partir de zéro. Un dossier exclu apparaît une seule fois et n’est pas parcouru : ses enfants, tailles et nombres ne sont pas inventoriés.
Les liens, jonctions et fichiers spéciaux sont ignorés. Les composants `.cysynck-recovery` restent protégés même sans règles.
Les autres exclusions par défaut peuvent être retirées explicitement. Les mêmes règles influencent les listes de changements et sélections locales ; elles ne modifient ni `.stignore` de Syncthing ni la configuration Git.

Les règles sont des chaînes UTF-8 de 1 à 1 024 octets, sans NUL ni retour à la ligne ; au plus 256 règles et 16 Kio de JSON encodé.
Les lignes vides de l’interface sont omises ; les espaces comptent. Les POST/PUT généraux des projets partagent cette validation sans exiger le jeton de l’éditeur.
Un ancien profil dépassant ces limites doit réduire ses règles avant d’utiliser l’aperçu.

## CLI et API {#cli-and-api}

```sh
cysynck backup-preview PROJECT
cysynck backup-preview PROJECT --excludes rules.json --limit 100
cysynck backup-excludes PROJECT rules.json --plan TOKEN
cysynck backup-preview PROJECT
cysynck backup PROJECT --plan NEW_TOKEN
```

`rules.json` contient un tableau tel que `[".git", "*.tmp", "build"]`. Sans `--excludes`, l’aperçu utilise les règles enregistrées.
POST `/api/projects/ID/backup-preview` accepte `excludes`, `offset`, `limit` et `inventory_hash` facultatifs.
Limites : 1–200 entrées par page (100 par défaut), décalage 0–100 000, 100 000 entrées du système de fichiers et 32 Mio de métadonnées.
Une entrée illisible, des métadonnées instables, un chemin dangereux ou un dépassement font échouer l’aperçu ; aucun inventaire partiel n’est présenté comme complet.

Le résultat contient `excludes`, `stored_excludes`, `rules_changed`, les totaux inclus `files`/`bytes`, `skipped`, `visited`, `content_verified:false`,
`inventory_hash`, `plan`, `total`, `offset`, `limit`, `has_more` et `entries`. Chaque entrée possède `path`, `kind`, `included`, `bytes`.
Les fichiers inclus ajoutent les permissions, les ticks opaques de date sous forme de chaîne et la prédiction `storage` (`whole`, `blocks-v1`, `blocks-cdc-v1`).
Les éléments ignorés ajoutent `reason` (`rule`, `protected`, `link`, `special`) et la règle applicable ; leur taille reste nulle.

Les pages suivent l’ordre des chemins UTF-8. Fournir le premier `inventory_hash` aux pages suivantes ; chacune reparcourt l’arbre borné.
Le plan lie le profil, tous les paramètres enregistrés du projet, les règles candidates et l’inventaire observé. Il reste identique pour différents décalages/tailles de page du même état.
PUT `/api/projects/ID/backup-excludes` reçoit `{"excludes":[...],"plan":"TOKEN"}`, recalcule l’aperçu, puis renvoie `saved:true` et `excludes`.
Un échec de validation ou de fraîcheur ne change pas le projet.
POST `/api/projects/ID/backup` accepte facultativement `{"plan":"TOKEN"}` provenant des règles actuellement enregistrées ; un brouillon différent ne l’autorise pas.
Les plans périmés, d’un autre projet ou profil sont refusés avant les callbacks et la capture habituelle. Les mêmes routes passent par l’API C/C++, sans changement de l’ABI C version 1.

## Portée du contrôle de fraîcheur {#scope-of-freshness}

L’inventaire lit tailles, dates et permissions des fichiers inclus, sans hasher les contenus. Il ne détecte pas les modifications préservant toutes ces métadonnées et ne garantit pas un snapshot du système de fichiers.
Les changements sous un dossier exclu ne périment pas l’inventaire. La capture normale lit et hashe les contenus, contrôle les modifications concurrentes et vérifie les manifests.
Un changement après le contrôle initial, y compris dans un callback d’extension, suit ces règles normales. Suspendre les écritures pour obtenir une sauvegarde cohérente au niveau applicatif.
Les tailles sont logiques. Les prédictions entier/fixe/CDC n’estiment ni gain de déduplication, ni allocation disque, ni espace temporaire.
Les sauvegardes automatiques, planifiées, avant synchronisation et `backup PROJECT` sans `--plan` gardent leur comportement. Ouvrir l’éditeur ne conserve pas d’aperçu et ne lance aucune sauvegarde implicite.
