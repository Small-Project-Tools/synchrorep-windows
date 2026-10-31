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
- Corbeille Windows native (IFileOperation) pour les suppressions sur lecteurs locaux fixes eligibles ; les fichiers WSL, UNC et distants continuent d'utiliser les archives de recuperation.
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
- Les cases **Preserve old versions** et **Use Windows Recycle Bin (local)**, actives par defaut et memorisees dans chaque profil, avec les commandes **Browse / restore archives** et **Open execution logs**.

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
automatiquement. L'archive reste une quarantaine applicative et ne remplace pas
la Corbeille Windows. La Corbeille native est maintenant disponible pour les
lecteurs locaux Windows compatibles et la restauration depuis l'archive
applicative reste manuelle.

Consulter [le guide de restauration](docs/recovery-archive.md).


## Restauration guidee (0.8)

Depuis la fenetre principale Windows, choisir **Browse / restore archives**.
Selectionner une ancienne version (nom du fichier, cote, execution et motif),
puis **Export selected**. Choisir un dossier local et confirmer l'exportation.
Le logiciel valide le manifeste et le SHA-256 et **refuse d'ecraser un fichier**.
Il ne restaure pas directement un dossier synchronise : verifier l'export, puis
copier manuellement la version retenue. Cette precaution evite de declencher
une modification distante accidentelle.

## Installeur MSI (preparation)

Le manifeste WiX est dans `windows/installer/Synchrorep.wxs`. Le job manuel
`windows-msvc-full` est configure pour construire l'application complete
(FTP/SFTP via vcpkg), executer les tests, puis generer un MSI avec WiX 5.
Ce job requiert un runner Windows avec Visual Studio, .NET et WiX 5 ; il
n'a pas encore ete valide sur une machine Windows. L'artefact MinGW Linux
reste limite aux endpoints locaux/WSL.


## Corbeille native Windows (0.9)

Lors d'une suppression sur un lecteur fixe local Windows compatible,
Synchrorep demande a l'API Shell **IFileOperation** de placer l'element
dans la Corbeille. Le moteur ne remplace jamais une erreur de mise a la
Corbeille par une suppression permanente automatique.

La case **Use Windows Recycle Bin (local)** permet de desactiver ce
comportement pour chaque fiche. Les partages reseau, WSL, FTP et SFTP
n'utilisent pas cette fonction et peuvent effectuer des suppressions
permanentes, precedees d'une sauvegarde dans l'archive applicative si
**Preserve old versions** est actif.

Le format de profil passe a \`SYNCHROREP-PROFILES3\` et conserve la
compatibilite de lecture avec les profils V1 et V2. Le nombre de
suppressions effectuees par la Corbeille est affiche separement.

La fonctionnalite a passe la compilation MinGW et les tests de politiques
de suppression avec un endpoint de simulation. Le fonctionnement effectif
de la Corbeille sous Windows 11 attend une validation native.
Consulter [les details et limites](docs/windows-recycle.md).


## Intégration moderne Windows 11 (0.10)

La DLL COM `SynchrorepExplorer.dll` expose une commande IExplorerCommand
pour les dossiers sélectionnés. La commande lance uniquement l’interface
principale, sans réseau ni synchronisation dans le processus Explorer.
Le binaire et la DLL sont compilés en x64 par la CI MinGW (local/WSL).

**L’apparition dans le menu Windows 11 exige un package MSIX sparse signé**
associé à l’identité de l’application. Le MSI seul ne suffit pas ; le package
préparé par le job manuel est non signé. Consulter
[installation de l’extension Explorer](docs/windows11-explorer.md).

Les tests et la compilation MSVC natifs, la signature MSIX et l’installation
réelle sous Windows 11 restent nécessaires avant diffusion publique.


## Mode miroir monodirectionnel continu (0.11)

Choisir un dossier local source a gauche et une cible locale, WSL, FTP ou
SFTP a droite. Cocher Continuous local -> target mirror, enregistrer le
profil, utiliser Preview puis Synchronize. Les changements de la source
sont recopies vers la cible automatiquement; aucune modification de la
cible ne remonte vers la source.

La surveillance locale se fait toutes les 500 ms, avec verification
SHA-256 complete au maximum toutes les 60 secondes. Les suppressions
autorisées se repercutent sur la cible et les anciennes versions sont
archivees selon les preferences de protection.

Sous Windows, fermer ou reduire la fenetre pendant le miroir la masque
dans la zone de notification, sans stopper la surveillance. Le menu
de l'icone permet de rouvrir l'application, stopper le miroir ou quitter.
Le demarrage automatique par profil est maintenant disponible via
**Start at Windows login** ; il demande confirmation lors de l'activation
et utilise le Registre HKCU sans droits administrateur.

Documentation: [mode miroir](docs/mirror-mode.md).


## Demarrage automatique du miroir (0.12)

Pour chaque profil de miroir, cocher **Start at Windows login** puis
enregistrer (Save). Windows lancera alors ce miroir dans la zone de
notification a l'ouverture de session, sans relancer les profils
bidirectionnels. Plusieurs profils miroirs peuvent demarrer en parallele ;
un verrou evite de lancer deux copies du meme couple source/cible.

Pour les cibles FTP et SFTP, les identifiants doivent etre deja sauvegardes
avec **Remember logins**. Le Registre ne contient ni mot de passe, ni URI
avec des identifiants : uniquement l'executable et le nom du profil.
Le format de fichier des profils devient **SYNCHROREP-PROFILES5**.
Les profils existants conservent le demarrage automatique desactive.

Le miroir s'arrête lorsque la session Windows se ferme. Un echec de
connexion ou un journal de reprise en attente exige une intervention,
sans reprise destructive automatique.

Voir [demarrage automatique](docs/automatic-startup.md).


## Windows 0.12.2 — version installable Lite

La CI genere maintenant un MSI x64 installable avec la compilation MinGW
\`synchrorep-windows-0.12.2-lite-x64.msi\`. Le MSI inclut l'application
et la DLL d'integration Explorer ; il n'enregistre **pas** automatiquement
un profil miroir au demarrage. Chaque activation doit etre explicite
dans l'interface (**Start at Windows login** puis **Save**).

**Important : cette edition Lite synchronise uniquement les dossiers
locaux et WSL. Elle n'inclut pas FTP/SFTP.** Pour ces transports,
le job Windows manuel \`windows-msvc-full\` construit l'edition complete
avec libcurl/vcpkg et WiX 5. Le menu moderne Windows 11 exige
toujours un package MSIX sparse signe, independant du MSI.

Telechargement : [artefacts de la compilation Windows](https://git.small-project.dev/pub/synchrorep-windows/-/jobs/artifacts/main/browse/build-win?job=windows-mingw).

Le correctif 0.12.1 ferme le fichier archive apres sa verification SHA-256
avant de le renommer, une exigence Windows que Linux ne faisait pas apparaitre.
La generation et l'installation du MSI ainsi que le test du miroir
automatique ont ete executes sous Wine, en plus des tests C++.
La validation dans un vrai environnement Windows 11 reste a realiser.


Le MSI Lite 0.12.1 utilise un ProductCode et des Component GUID stables
pour une même version. L'installation, la synchronisation automatique,
la désinstallation et la disparition de l'exécutable ont été validées dans
un préfixe Wine neuf. Un test sur Windows 11 réel reste nécessaire.


## Protection des destinations multi-miroirs (0.12.2)

Deux profils de demarrage automatique ne peuvent plus cibler le meme
dossier, ni des destinations imbriquees. La validation tient compte des
variantes de chemins Windows (casse, slash et anti-slash), des adresses
WSL/UNC et des ports FTP/SFTP par defaut. Les miroirs vers des dossiers
independants restent utilisables simultanement.

Un verrou Windows nomme par destination, plutot que par couple
source/cible, empeche deux sources concurrentes de modifier une meme
destination. Une ancienne configuration de demarrage automatique
contradictoire est refusee avant le lancement des mirroirs, et le profil
ne peut pas etre sauvegarde tant que ses cibles se chevauchent.

Le MSI Lite x64 est disponible en version 0.12.2. Pendant la mise
a niveau depuis la version 0.12.1, Synchrorep conserve aussi le
verrou historique du couple source/cible pour prevenir un double
demarrage entre les deux versions.
