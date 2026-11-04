# Sauvegardes et stockage

Chaque snapshot conserve un manifest complet et des objets de contenu SHA-256 dédupliqués
entre tous les projets d'un même profil. La rétention `backup_keep` retire les manifests
les plus anciens après publication d'une nouvelle sauvegarde. Les objets inutilisés sont
conservés jusqu'à un nettoyage explicite.

## Vérifier et restaurer

```sh
cysynck verify PROJECT_ID
cysynck verify PROJECT_ID --snapshot SNAPSHOT_ID
cysynck restore-plan PROJECT_ID SNAPSHOT_ID /srv/restored-copy
cysynck restore PROJECT_ID SNAPSHOT_ID /srv/restored-copy
```

La vérification relit chaque objet distinct, contrôle son SHA-256 et la taille attendue
par chaque fichier. `ok`, `issues`, `objects_checked` et `checked_at` décrivent le résultat.
Un résultat `ok: false` est un rapport valide, pas une erreur du programme : un script doit
examiner ce champ. Les erreurs de manifest, permissions ou arguments font échouer la commande.
La vérification n'est pas une signature : un tiers disposant des droits d'écriture sur l'état
peut modifier simultanément objets et manifests.

Si une nouvelle sauvegarde capture le contenu original d'un objet corrompu, elle remplace
atomiquement cet objet par les octets fraîchement capturés. Les anciennes références à ce
contenu redeviennent alors utilisables. Un contenu original qui n'existe plus doit être récupéré
sur une autre copie ; le contrôle ne le reconstruit pas.

`restore-plan` vérifie l'intégrité et énumère les fichiers sans créer de destination.
`restore` refait ces vérifications avant de créer un dossier neuf. Aucun dossier de projet,
d'échange ou d'état ne peut servir de destination, ni chevaucher celle-ci. Les liens et jonctions
dans le chemin sont refusés. Les chemins et métadonnées du manifest sont validés avant usage,
notamment les conflits de noms sous Windows. Le contenu copié est également vérifié.

Une erreur d'intégrité détectée avant copie laisse la destination absente. Un problème de
permissions, d'espace disque, une modification extérieure à CySynck ou un arrêt pendant
la copie peut laisser une restauration partielle : celle-ci n'est pas transactionnelle.
Le dossier source n'est jamais modifié. Seuls les contenus et permissions simples sont
restaurés ; les dossiers vides, liens, ACL, attributs étendus et dates originales restent exclus.

## Libérer l'espace

```sh
cysynck storage status
cysynck storage prune
cysynck storage prune --apply true --plan TOKEN_RENVOYE_PAR_APERCU
```

Sans `--apply true`, aucun objet n'est supprimé. L'aperçu retourne :

| Champ | Signification |
| --- | --- |
| `snapshots` | Nombre de manifests présents, tous projets compris |
| `logical_bytes` | Somme des tailles logiques des fichiers de ces versions |
| `stored_bytes` | Taille des fichiers d'objets reconnus présents |
| `missing_objects` | Nombre de SHA-256 référencés dont le fichier est absent |
| `unused_objects`, `reclaimable_bytes` | Objets sans référence et espace récupérable |
| `ignored_entries` | Entrées non reconnues, conservées |
| `errors`, `can_prune` | Erreurs de catalogue et possibilité d'appliquer le nettoyage |
| `plan` | Empreinte de l'aperçu à fournir pour l'application |

Ce bilan ne calcule pas le hash du contenu des objets ; utiliser `verify` pour l'intégrité.
Il ne mesure pas la taille totale du profil : journaux, fichiers reçus, Git et versions
Syncthing sont exclus. Un objet corrompu encore référencé est conservé.
Les copies indépendantes des résolutions dans `resolutions/` et `.cysynck-recovery/`
ne sont pas comptées ni nettoyées par cette collecte.

La collecte parcourt aussi les projets absents du registre. Un manifest invalide, une entrée
inattendue du catalogue de snapshots ou une jonction interdit l'application. Les fichiers
temporaires et inconnus dans `objects/` sont conservés. Aucune suppression récursive n'est
effectuée. Les sauvegardes, restaurations et collectes prennent le même verrou entre processus.

Le plan doit encore correspondre au profil, aux manifests et à l'inventaire au moment de
l'application ; sinon la commande demande un nouvel aperçu avant toute suppression.
Le résultat d'une application comporte `removed_objects`, `reclaimed_bytes` et `failures`.
Une erreur de suppression peut laisser un nettoyage partiellement effectué ; les références
restent préservées, et une nouvelle analyse permet de poursuivre.

La collecte reste manuelle dans cette version. Aucun nettoyage n'est lancé au démarrage,
après une sauvegarde ou en arrière-plan.
