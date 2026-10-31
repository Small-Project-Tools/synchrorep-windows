# Matrice de compatibilité Synchrorep GNOME → Windows

Origine : [sebk69/Synchrorep](https://github.com/sebk69/Synchrorep), licence GPL-3.0.

## Comparaisons et décisions

| État du moteur GNOME | Portage actuel |
| --- | --- |
| `_files_not_modified` | `Action::None` |
| `_from_modified` / `_to_modified` | Copie gauche → droite / droite → gauche |
| `_from_deleted` / `_to_deleted` | Suppression propagée si activée |
| `_both_deleted` | État absent, aucun transfert |
| `_both_modified_from_recent` / `_both_modified_to_recent` | Conflit ; stratégies Newest/Oldest/Ignore |
| `_both_modified_equal` | Aucun transfert si contenu SHA-256 identique |
| `_comparison_error` | Exception, pas d'enregistrement de nouvel état |

Le moteur original propose trois modes MD5 (`_no_md5sum`, `_both_modified_md5sum`, `_database_md5sum`). Le prototype Windows utilise désormais **SHA-256 systématique**, avec un coût plus élevé sur les protocoles distants. La reproduction exacte de ces trois options reste à faire.

## Endpoints

| Endpoint | État | Vérification |
| --- | --- | --- |
| Local Windows/UNC | Implémenté | Tests du moteur sur Linux, compilation MinGW |
| WSL | Conversion en chemins UNC Windows | Compilation MinGW, test sur Windows/WSL manquant |
| FTP | libcurl, MLSD, fichiers temporaires | Tests sur serveur local jetable |
| SFTP | libcurl SSH, connu_hosts obligatoire | Tests sur serveur SFTP local jetable |

Les fichiers sont lus intégralement lors du calcul SHA-256. Une erreur de listing ou un fichier incomplet bloque l'exécution avant enregistrement de l'état.

## Interface et préférences

| Fonction GNOME | Windows |
| --- | --- |
| Fiches de synchronisation et choix des dossiers | Édition et persistance des fiches |
| Préférences générales | Politique conflits et suppressions |
| Menu contextuel Nautilus | Menu classique Explorateur installable par utilisateur |
| Identifiants réseau | Champs et Windows Credential Manager |
| Fenêtre de progression | Barre par phase et fichier, annulation entre fichiers |
| Dialogue de conflits par fichier | Choix Keep left / Keep right / Skip dans la liste |
| Deplacer les suppressions vers la corbeille | Archive de recuperation applicative avant toute operation destructive ; Corbeille Windows native non implementee |
| Groupes de synchronisations | Non implémenté |
| Fenetres de logs et comportements expert | Journaux JSONL disponibles ; ecran expert non implemente |
| Extension native moderne Windows 11 | Non implémentée |

## Historique et récupération

Le format `SYNCHROREP4` stocke pour chaque fichier les empreintes SHA-256 des deux extrémités. Les anciens formats `SYNCHROREP3` et SQLite GNOME ne sont pas interprétés : ils provoquent une erreur et nécessitent une migration contrôlée. L'historique est lié au couple ordonné d'emplacements.

Les transferts sont préparés via des fichiers temporaires et vérifiés avant d'enregistrer le nouvel état. Cela **ne garantit pas une transaction** sur l'ensemble des fichiers : l'archivage des anciennes versions et le journal de reprise ne remplacent pas
une transaction globale. La restauration automatique et les garanties contre
toutes les modifications concurrentes restent a developper.

## Compilation et CI

- Linux : test du moteur, SHA-256, profils et intégrations réelles FTP/SFTP.
- Windows : MinGW local/WSL sans libcurl, compilation croisée.
- Windows complet : job GitLab manuel `windows-msvc-full`, nécessite un runner Windows avec MSVC/vcpkg, non encore exécuté.

## Priorités suivantes

1. Tests d'exécution sur Windows 11 réel (Explorer + WSL) et build MSVC/vcpkg FTP/SFTP.
2. Migration contrôlée de SQLite GNOME et des états expérimentaux précédents.
3. Optimisation du calcul de hash distant (sans faux négatifs).
4. Dialogues de conflits par fichier, corbeille, logs, progression et groupes.
5. IExplorerCommand natif et MSI.

### Execution

Le moteur propose des callbacks de progression et d'annulation cooperative, des resolutions individuelles et une conservation de l'historique precedent lors d'une interruption. Voir docs/execution.md.

## Journal de recuperation 0.6

Le moteur ecrit un journal des operations et impose une reprise explicite (option --resume). La reprise recalcule toutes les decisions avant de reprendre.

Les fichiers de staging partiels laisses par un processus brutalement arrete sont detectes et bloquent la synchronisation au lieu d etre propages. Une inspection et un nettoyage manuel sont necessaires. Le journal ne remplace pas une transaction multi-fichiers.


## Archives de recuperation (iteration 0.7)

Chaque operation de suppression ou de remplacement preserve maintenant
l'ancienne version, sauf desactivation explicite de l'option dans la fiche.
Les anciennes versions sont verifiees par SHA-256, conservees dans un dossier
local distinct et accompagnees d'un manifest. L'ancienne version d'un fichier
distant est telechargee depuis FTP/SFTP avant l'operation destructive.
Les journaux JSONL et les archives sont accessibles dans l'interface.
Ce n'est pas la Corbeille native Windows, et une restauration reste manuelle.
Voir [archives de recuperation](recovery-archive.md).
