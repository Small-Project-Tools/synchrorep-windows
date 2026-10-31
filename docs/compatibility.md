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
| Deplacer les suppressions vers la corbeille | Corbeille native Windows sur lecteur fixe local ; archive applicative avant operation destructive selon le profil |
| Groupes de synchronisations | Non implémenté |
| Fenetres de logs et comportements expert | Journaux JSONL disponibles ; ecran expert non implemente |
| Extension native moderne Windows 11 | IExplorerCommand et package sparse préparés ; signature et activation natives non encore testées |

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

## Restauration guidee (0.8)

Une fenetre Windows affiche les archives et permet une exportation vers un
dossier local, sans ecrasement et avec controle SHA-256. Une restauration
directe sur l'endpoint d'origine, la corbeille Windows et les groupes de
restauration restent a implementer. Un manifeste WiX prepare un MSI, mais
le build MSVC/vcpkg/WiX doit encore etre valide sur runner Windows.


## Corbeille Windows (iteration 0.9)

La Corbeille native Windows est disponible via IFileOperation et
FOFX_RECYCLEONDELETE pour les lecteurs fixes locaux. Un echec
ne declenche aucune suppression permanente de secours.
WSL, UNC, FTP et SFTP conservent leur mecanisme d'archivage
et leurs suppressions selon les regles de synchronisation.
Les profils V1/V2 restent lisibles ; V3 contient la nouvelle option.
Le test de fonctionnement Windows 11 reste a executer sur un poste
Windows reel. Voir [integration Corbeille](windows-recycle.md).


## Menu Windows 11 (iteration 0.10)

Une DLL IExplorerCommand et un manifeste sparse MSIX permettent de préparer
l’intégration au menu contextuel moderne pour les dossiers. L’enregistrement
réel exige une signature de package, une identité Windows et un test sous
Windows 11. Le menu classique reste disponible. Le MSI embarque maintenant
la DLL, mais ne suffit pas à inscrire l’extension dans le menu moderne.
Voir [installation Windows 11](windows11-explorer.md).


## Mode miroir unidirectionnel continu (0.11)

SyncMode::Mirror ne genere jamais de copie cible -> source. Il compare la
source locale et la cible, surveille les changements locaux et refait
periodiquement une comparaison SHA-256 complete. Un scan source impossible
stoppe la surveillance. Le format de profil V4 enregistre ce choix,
les formats V1-V3 restant bidirectionnels. Voir [guide](mirror-mode.md).


## Demarrage automatique (0.12)

Les profils miroirs peuvent etre demarres lors de l'ouverture de session
Windows avec des valeurs HKCU Run par profil et sans elevation. Les
profils V1 a V4 restent desactives par defaut. Une double instance
du meme couple source/cible est bloquee ; les identifiants FTP/SFTP
proviennent de Windows Credential Manager. Le registre ne contient
pas les secrets. Le test de connexion Windows reel reste a mener.
Voir [demarrage automatique](automatic-startup.md).


## Stabilisation Windows et edition Lite (0.12.1)

Le fichier de sauvegarde temporaire est maintenant ferme avant son renommage
lors de l'archivage ; Windows refusait ce renommage tant que le fichier
restait ouvert. Le MSI Lite x64 est construit par la CI MinGW et ne contient
que les transports locaux/WSL. Les exports COM Explorer sont inclus, mais
l'inscription dans le menu moderne Windows 11 exige toujours un MSIX signe.

Les tests de la base Win32 (registre de demarrage, archives) ont ete executes
sous Wine, ainsi qu'un scenario de miroir automatique depuis un exe installe
par le MSI. La Corbeille native et la connexion interactive Windows 11 restent
a verifier sur une machine Windows reelle.


## Reservations des destinations des miroirs (0.12.2)

Les profils auto-demarres ne peuvent plus utiliser la meme destination
ou des destinations imbriquees. Leurs chemins sont normalises de facon
conservative : majuscules/minuscules et separateurs Windows, correspondance
UNC/WSL et ports standards FTP/SFTP. Un mutex par destination empeche
deux miroirs distincts d'ecrire dans le meme dossier. Les miroirs vers
des destinations independantes restent utilisables en parallele.

Le verrou historique source/cible est conserve pendant les mises a jour
pour eviter une double execution du meme profil avec 0.12.1 et 0.12.2.
Les alias reseau vers un meme volume restent une limite ; ils doivent
etre evites manuellement.
