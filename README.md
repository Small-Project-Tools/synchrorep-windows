# Synchrorep Windows

Portage natif C++20 de [Synchrorep pour GNOME](https://github.com/sebk69/Synchrorep) (Sébastien Kus, 2009–2019, GPL-3.0). Objectif : **le même fonctionnement et, à terme, la même interface**, intégrés à l'Explorateur Windows, pour les dossiers locaux, WSL, FTP et SFTP.

**État : prototype, pas encore prêt pour des données critiques.**

## Fonctionnalités disponibles

- Moteur commun de synchronisation bidirectionnelle avec état persistant par couple d'emplacements.
- Comparaison des contenus par **SHA-256**, y compris si les fichiers ont la même taille et la même date de modification. Vérification après transfert avant enregistrement de l'historique.
- Propagation des créations, modifications et suppressions, conflits bilatéraux et politiques Ignore / Newest / Oldest.
- Endpoints local (Windows/UNC), WSL (`wsl://Ubuntu/home/user`), FTP et SFTP (libcurl).
- Interface Windows avec fiches de synchronisation, configuration des endpoints, politiques, aperçu des actions, confirmation des suppressions et commandes pour les identifiants Windows.
- Intégration au menu contextuel **classique** de l'Explorateur (Windows 11 : « Afficher d'autres options »).
- Archives locales de recuperation avant suppressions et remplacements, avec verification SHA-256 des anciennes versions (local, WSL, FTP, SFTP).
- Journaux d'execution JSONL, consultation directe des dossiers archives/logs sous Windows.
- Tests C++ du moteur, empreintes SHA-256, sauvegardes de recuperation et stockage des profils ; tests d'integration FTP/SFTP sur serveurs locaux jetables.

## Compiler et tester sous Linux

Prérequis : compilateur C++20, CMake, libcurl-dev, Python 3, pyftpdlib et Paramiko.

```bash
cmake -S . -B build-remote -DSYNCHROREP_REMOTE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-remote --parallel
ctest --test-dir build-remote --output-on-failure
bash tests/integration/run.sh
```

Les serveurs de test écoutent uniquement sur 127.0.0.1 et leurs données sont détruites à la fin de l'exécution.

## Compiler l'application Windows complète

Prérequis : Windows, Visual Studio C++ (MSVC), CMake et [vcpkg](https://github.com/microsoft/vcpkg). Le manifeste déclare **curl avec les fonctionnalités `ssl` et `ssh`**. Utiliser le triplet statique pour obtenir une distribution autonome :

```powershell
cmake -S . -B build-win-full -A x64 -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static -DSYNCHROREP_REMOTE=ON
cmake --build build-win-full --config Release --parallel
ctest --test-dir build-win-full -C Release --output-on-failure
```

**Ce build complet n'a pas encore été exécuté sur un véritable Windows/MSVC.** Un job manuel `windows-msvc-full` est prévu sur un runner GitLab tagué `windows`. Le binaire produit par la CI MinGW (Linux) utilise `SYNCHROREP_REMOTE=OFF` : il ne prend en charge que **local et WSL**.

## Utilisation Windows

Démarrer `synchrorep.exe`. L'écran **Synchrorep - Settings** propose :

- Une liste de fiches enregistrables (New / Save / Delete).
- Deux emplacements (chemins locaux/UNC ou URL `wsl://`, `ftp://`, `sftp://`).
- Les identifiants FTP/SFTP de chaque extrémité, avec sauvegarde et lecture explicites via le **Gestionnaire d'identifiants Windows**. Ils ne sont jamais enregistrés dans le fichier des fiches.
- Le chemin du `known_hosts` requis pour SFTP, les politiques de conflit, la propagation des suppressions, la prévisualisation et l'exécution.
- Une case activant ou supprimant le menu contextuel classique pour les dossiers.
- La case **Preserve old versions** (activee par defaut et memorisee dans chaque profil), avec les commandes **Open recovery archive** et **Open execution logs**.

Les fiches sont enregistrées sous `%LOCALAPPDATA%\Synchrorep\profiles.dat`, sans mot de passe. Les historiques individuels sont enregistrés dans le même répertoire avec une clé SHA-256 dérivée du couple d'emplacements.

La CLI Linux lit actuellement les identifiants dans les variables d'environnement suivantes :

| Protocole | Variables |
| --- | --- |
| FTP | `SYNCHROREP_FTP_USER`, `SYNCHROREP_FTP_PASSWORD` |
| SFTP | `SYNCHROREP_SFTP_USER`, `SYNCHROREP_SFTP_PASSWORD`, `SYNCHROREP_SFTP_KNOWN_HOSTS` |
| SFTP optionnel | `SYNCHROREP_SFTP_PRIVATE_KEY` |

## Compatibilité et limites

- Nouveau format `SYNCHROREP4` avec empreintes SHA-256. Les états historiques `SYNCHROREP3` sont **refusés explicitement**, sans migration silencieuse. Les migrations depuis SQLite de GNOME restent à écrire.
- Le hachage SHA-256 de tous les fichiers est réalisé à chaque analyse. Sur FTP/SFTP, cela demande actuellement le **téléchargement du contenu distant**, potentiellement coûteux sur de gros volumes. Optimisation incrémentale à prévoir sans sacrifier la fiabilité.
- L'execution d'un lot n'est pas transactionnelle : une interruption peut laisser une synchronisation partiellement appliquee. Un journal de reprise et des copies de recuperation existent maintenant, mais sans rollback automatique.
- FTP est en clair : préférer SFTP pour des données sensibles. SFTP exige un `known_hosts` approuvé.
- L'interface comprend les fiches, les choix de conflits, les journaux et la progression par fichier, mais **n'est pas encore une reproduction visuelle complete** : groupes, fenetre expert, progression en octets et menu moderne Windows 11 restent a faire.
- Les tests Win32 graphiques et FTP/SFTP natifs Windows ne sont pas encore validés sur un poste Windows.

Voir [etat de compatibilite](docs/compatibility.md), [interface Windows](docs/windows-ui.md) et [archives et restauration](docs/recovery-archive.md).

## Licence

Licence originale GPL-3.0 conservée dans [LICENSE](LICENSE). Les notices de licences des dépendances restent applicables.

## Reprise controlee apres interruption

Le moteur cree un fichier journal `<historique>.journal` **avant toute modification**. Si l'execution est interrompue, ce journal est conserve : une execution ulterieure est refusee par defaut. Utiliser **Preview** pour examiner les changements, puis activer **Resume interrupted run** dans l'interface Windows avant de relancer.

Dans la CLI Linux, utiliser `--resume` uniquement apres avoir verifie le contenu des deux dossiers :

```bash
./build-remote/synchrorep /tmp/source /tmp/target /tmp/synchrorep.state --dry-run
./build-remote/synchrorep /tmp/source /tmp/target /tmp/synchrorep.state --resume
```

Cette reprise fait une **nouvelle analyse complete** et ne rejoue pas aveuglement les anciennes operations. Elle controle que le journal correspond exactement aux deux endpoints. Elle ne garantit pas de rollback transactionnel, notamment apres une panne de courant. Voir [execution](docs/execution.md).

## Execution asynchrone et annulation

L'interface Win32 lance l'analyse et les copies dans un thread de travail. Elle affiche la phase, le compteur et le chemin du fichier courant. La liste des conflits permet de choisir Keep left, Keep right ou Skip file. La demande d'annulation peut interrompre les transferts FTP/SFTP via libcurl; le hachage d'un fichier local reste interruptible uniquement entre fichiers. Voir [execution et limites](docs/execution.md).


## Protection des fichiers et journaux (0.7)

Les anciennes versions avant suppression ou remplacement sont copiees dans
`%LOCALAPPDATA%\Synchrorep\recovery` sous Windows et sous `recovery/` a
cote du fichier d'etat sous Linux. Ces copies sont controlees par SHA-256.
Le moteur bloque une operation destructive si cette sauvegarde ne peut pas
etre validee.

Les journaux JSONL sont sous `%LOCALAPPDATA%\Synchrorep\logs`.
Les archives et journaux ne sont ni chiffres au repos ni nettoyes
automatiquement. Cette fonction n'utilise **pas** la Corbeille native
de Windows ; la restauration est encore manuelle.

Consulter [le guide de restauration](docs/recovery-archive.md).
