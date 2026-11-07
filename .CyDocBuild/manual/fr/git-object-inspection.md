---
id: git-object-inspection
title: Contrôles bornés des objets Git
description: Comprendre les vérifications de fichiers bruts et de petits blobs avant intégration.
section: Git
order: 44
parent: git-review
sourceRevision: 1
---

# Contrôles bornés des objets Git {#bounded-git-object-inspection}

## Fichiers de travail bruts {#raw-working-files-035}

Les aperçus d’intégration, contrôles après application et vérifications de reçus emploient `git hash-object --no-filters -- <absolute-file>...` par lots bornés.
Chaque fichier est relu et comparé à son identité Git attendue ; aucun cache de dates ne remplace le contrôle des octets.
CLI, ABI C/C++ et CyUI/Web utilisent la même implémentation.
Un appel accepte au plus 128 chemins, 12 000 octets UTF-8 d’arguments avec marge de citation et 32 Mio de contenus.
Un fichier autorisé plus grand est hashé seul. Les limites restent 1 Gio par fichier, 16 Gio au total, 10 000 fichiers.
Chaque chemin est un argument distinct, donc les retours à la ligne POSIX ne créent pas d’enregistrements supplémentaires.
Un inventaire vide ne lance aucun Git. SHA-1/SHA-256 sont acceptés.
Les fichiers doivent être réguliers, présents, sous le projet, sans lien ni alias de chemin.
Taille, date et permissions sont observées avant inspection, revérifiées avant chaque lot et après tous les lots.
Identités incorrectes, lignes absentes ou sortie supplémentaire font échouer le contrôle.
Aucun filtre, conversion, téléchargement, écriture d’objet ou mise à jour d’index ; les redirections héritées d’environnement/configuration Git sont refusées.
Ces observations ne sont ni une transaction ni un verrou exclusif. Suspendre les autres écritures pendant l’intégration.
Un acteur restaurant les métadonnées pendant une course de lecture dépasse cette garantie ; une modification de même taille/date faite avant inspection est détectée par le nouveau hash.
Référence : [Git hash-object](https://git-scm.com/docs/git-hash-object).

## Petits blobs commités {#small-committed-blobs-028}

L’intégration lit les blobs de moins de 1 024 octets avec `git cat-file --batch` pour reconnaître les pointeurs LFS.
Les identités sont dédupliquées, avec au plus 256 objets et moins de 256 Kio de contenus par lot, plus en-têtes.
L’inventaire reste limité à 10 000 entrées ; les plus gros blobs ne sont pas chargés à cette étape.
Les en-têtes doivent correspondre exactement aux identités, types et tailles demandés.
Objet absent, données tronquées, tailles incohérentes d’un doublon ou sortie finale supplémentaire font échouer l’opération.
NUL et retours à la ligne sont conservés. Aucun filtre, conversion, écriture d’objet ou téléchargement n’est demandé.
Les contrôles de chemins, LFS, attributs, checkout et reçus restent actifs.
Voir [l’interface batch de Git](https://git-scm.com/docs/git-cat-file#_batch_output) et [l’intégration contrôlée](doc:git-integration).
