# Synchrorep for Windows

![Icône Synchrorep](assets/synchrorep-source.png)

Portage natif C++20 de [Synchrorep pour GNOME](https://github.com/sebk69/Synchrorep).
**Un seul exécutable et un seul installateur Windows x64** avec les quatre transports :
**local, WSL, FTP et SFTP**. Aucun mode ou installateur « Lite » distinct.

## Fonctionnalités

**Version 1.1.0-RC1 :** reprise après interruption activée par défaut,
patterns d'exclusion configurables (un glob par ligne) et correctifs WSL
pour les remplacements et la reprise après coupure brutale.


- Synchronisation bidirectionnelle avec prévisualisation, historique, résolution des conflits et annulation.
- **Miroir local → cible** en continu : copie, modification, suppression et réconciliation SHA-256, sans retour vers la source.
- Démarrage automatique par profil miroir à l’ouverture de session Windows (HKCU Run).
- Profils, secrets FTP/SFTP dans Windows Credential Manager, validation SFTP par `known_hosts`.
- Sauvegarde des versions précédentes, journal de reprise et vérification SHA-256 avant les opérations destructives.
- Blocage des profils miroirs qui utilisent des destinations identiques ou imbriquées.
- **Interface Win32 sombre et adaptative** avec onglets Configuration, Preview, History et Settings ; configuration entièrement visible sur un écran 12 pouces, sans défilement.
- Interface Win32, icône fournie, zone de notification, menu contextuel classique de l’Explorateur.
- DLL `IExplorerCommand` et manifeste MSIX *sparse* pour préparer l’intégration au menu moderne Windows 11.

## Code signing policy

Free code signing provided by SignPath.io, certificate by SignPath Foundation.

The project is preparing an application to SignPath Foundation. **Current builds are unsigned; signing has not been approved or enabled.** See the [code signing policy, team roles and privacy policy](docs/code-signing-policy.md), and the [project download page](https://small-project.dev/products/synchrorep#code-signing-policy).

## Installation Windows

Récupérer le fichier `synchrorep-windows-1.1.0-x64.msi` dans les
[artefacts de la dernière compilation Windows](https://github.com/Small-Project-Tools/synchrorep-windows/actions/workflows/distribution.yml).

La CI GitHub Actions Linux/MinGW construit désormais **l’unique édition complète** en utilisant
la variante Windows **libcurl/OpenSSL + libssh2** fournie par MSYS2.
La compilation télécharge les paquets en HTTPS, vérifie leurs empreintes SHA-256
issues des métadonnées du dépôt, détermine les DLL réellement nécessaires,
les embarque dans le MSI et inclut les licences de leurs composants.

Le fichier MSI contient `synchrorep.exe`, `SynchrorepExplorer.dll`,
les DLL nécessaires au réseau, la licence et `THIRD_PARTY_NOTICES.txt`.
Les DLL doivent rester aux côtés de l’exécutable ; ne pas distribuer
`synchrorep.exe` isolément. Installer le MSI pour obtenir l’ensemble.

Les anciennes installations **Synchrorep Lite** sont prises en charge par le
même `UpgradeCode` Windows Installer. Les profils restent dans
`%LOCALAPPDATA%\Synchrorep\profiles.dat` et ne sont pas inclus dans le MSI.

### Créer un miroir continu

1. Sélectionner le dossier local **Left** comme source et l’emplacement **Right** comme cible.
2. Dans l'onglet **Configuration**, sélectionner **Continuous mirror**.
3. Vérifier **Preview**, l’option de suppression et la protection des anciennes versions.
4. Pour le démarrage automatique, cocher **Start at Windows login** et enregistrer.
5. Laisser Synchrorep actif en zone de notification ou utiliser son menu pour arrêter la surveillance.

La source doit être un dossier local sur un lecteur Windows.
La cible peut être locale, WSL, FTP ou SFTP. Pour une cible distante,
enregistrer les identifiants dans Windows Credential Manager ;
SFTP exige un fichier `known_hosts` approuvé.

## Compiler et tester

### Linux

```bash
cmake -S . -B build-remote -DSYNCHROREP_REMOTE=ON
cmake --build build-remote --parallel
ctest --test-dir build-remote --output-on-failure
bash tests/integration/run.sh
```

Prérequis : CMake, compilateur C++20, libcurl-dev, Python, pyftpdlib et Paramiko.

### Windows (cross-compilation sur Linux)

```bash
python3 scripts/fetch_windows_curl.py --output build-msys2/win64 --cache build-msys2/cache
cmake -S . -B build-win -DSYNCHROREP_REMOTE=ON \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++-posix \
  -DCURL_INCLUDE_DIR="$PWD/build-msys2/win64/include" \
  -DCURL_LIBRARY="$PWD/build-msys2/win64/lib/libcurl.dll.a" \
  -DSQLite3_INCLUDE_DIR="$PWD/build-msys2/win64/include" \
  -DSQLite3_LIBRARY="$PWD/build-msys2/win64/lib/libsqlite3.dll.a"
cmake --build build-win --parallel
python3 scripts/stage_windows_runtime.py --prefix build-msys2/win64 --build build-win
python3 scripts/render_windows_installer.py --build build-win --version 1.1.0
python3 tests/windows_installer_test.py --build build-win
```

Prérequis : CMake, MinGW-w64 x64 POSIX, Python 3, zstd et WiX `wixl`.
La CI exécute ces opérations, fabrique le MSI et vérifie les protocoles FTP/SFTP
sous Wine, ainsi que leurs transferts vers des serveurs de test locaux.
Les tests natifs automatisés de compilation MSVC et d'interface Win32 sont
exécutés sur Windows Server 2022/2025 via GitHub Actions.
La validation visuelle et fonctionnelle Windows 11/WSL2 reste distincte.
La [recette Windows 11](docs/windows11-manual-acceptance.md) est obligatoire
avant toute déclaration de conformité Windows 11 ou GNOME.

## Sécurité et limites

- **FTP transmet les données et identifiants en clair** : privilégier SFTP.
- Ne jamais mettre un mot de passe dans une URI. Les secrets ne sont pas stockés dans les profils ou le Registre.
- Les états historiques `SYNCHROREP3` et la base SQLite de Synchrorep GNOME ne sont pas migrés automatiquement.
- Le calcul des empreintes distantes télécharge actuellement les fichiers à chaque réconciliation complète ;
  cela peut consommer beaucoup de bande passante.
- Une synchronisation multifichier n’est pas transactionnelle. Après une interruption,
  examiner le journal de reprise avant toute reprise.
- La Corbeille Windows réelle, la signature du MSIX *sparse* et les interactions
  graphiques de Windows 11 restent à valider sur un véritable poste Windows.

Documentation : [compatibilité](docs/compatibility.md),
[mode miroir](docs/mirror-mode.md),
[démarrage automatique](docs/automatic-startup.md),
[interface Windows](docs/windows-ui.md),
[intégration Explorateur](docs/windows11-explorer.md),
[archives et restauration](docs/recovery-archive.md) et
[exécution](docs/execution.md).

Licence : voir [LICENSE](LICENSE).


## Menu contextuel de l’Explorateur (0.13.1)

L’installation MSI enregistre automatiquement **Synchronize with Synchrorep**
au niveau machine (HKLM) pour les dossiers, les lecteurs et le fond d’un dossier.
Sur Windows 11, utiliser le clic droit → **Afficher d’autres options** pour voir
cette commande classique. Il n’est pas nécessaire de cocher manuellement
l’option dans Synchrorep lorsque l’application est installée par MSI.

Le MSI désenregistre ces commandes à la désinstallation. L’accès par le
nouveau menu compact de Windows 11 reste lié à un package MSIX sparse signé,
qui n’est pas encore distribué. La commande de l’icône de notification prend
en charge les notifications classiques et la version 4 des événements Windows.


## Fichier libcurl-4.dll introuvable (0.13.2)

L'exécutable Synchrorep dépend de DLL réseau, notamment `libcurl-4.dll`,
`libssh2-1.dll` et OpenSSL. Il **ne faut jamais télécharger ou lancer
`synchrorep.exe` seul** depuis les artefacts GitHub Actions.

Deux distributions complètes sont proposées :

- `synchrorep-windows-1.1.0-x64.msi` : installation standard avec
  menu contextuel Explorer et toutes les DLL au même endroit
  (`C:\Program Files\Synchrorep`).
- `synchrorep-windows-1.1.0-x64-portable.zip` : extraire **toute**
  l'archive dans un dossier avant d'exécuter `Synchrorep-1.1.0\synchrorep.exe`.
  Tous les fichiers DLL doivent rester à côté de l'exécutable.

La CI refuse dorénavant de publier une archive portable incomplète.
Elle teste aussi l'exécution réelle de l'application installée et extraite
avec `--check-runtime` pour vérifier libcurl, FTP et SFTP.

Si l'erreur apparaît malgré le MSI, vérifier que `libcurl-4.dll` se
trouve dans le même dossier que **l'exécutable réellement lancé** et
réinstaller le MSI. Les anciens raccourcis ou une copie isolée de
`synchrorep.exe` peuvent encore pointer ailleurs.


## Lancement depuis l’Explorateur — comportement GNOME rétabli (0.13.4)

Le menu contextuel **Synchronize with Synchrorep** déclenche une action :

1. Si le dossier appartient déjà à une configuration, celle-ci est ouverte
   et la synchronisation démarre après confirmation.
2. Si le dossier n'est pas configuré, la boîte native Windows **Choisir un
   dossier** apparaît immédiatement pour sélectionner la destination.
3. Un profil bidirectionnel est enregistré automatiquement avec les valeurs
   par défaut (conflits demandés, sauvegardes et Corbeille activées).
4. La première synchronisation est proposée et démarre après confirmation.

L'annulation de la sélection ne crée aucun profil et ne lance aucun transfert.
Les dossiers identiques ou imbriqués sont refusés, y compris via des
jonctions ou des alias de chemins. Un dossier déjà affecté à un autre
profil ne peut pas être réutilisé par erreur.

Le lancement sans dossier conserve l'interface de configuration avancée.
L'option « miroir continu » reste distincte de la synchronisation
bidirectionnelle par défaut et doit être activée explicitement.


## Windows 1.1.0 : comparaison et fenetre de synchronisation

- Le dialogue natif Windows reste utilise pour choisir une destination. Il
  propose l'emplacement WSL \wsl.localhost lorsque Windows l'expose et
  permet de naviguer dans les distributions depuis le dialogue standard.
- La synchronisation a sa propre fenetre principale (statut, progression,
  operations et annulation), independante de la fenetre des profils.
- La case **Compare content with SHA-256 (slower)** est facultative.
  Pour une paire sans historique, les fichiers presents des deux cotes sont
  compares par SHA-256 quelle que soit la valeur de cette case.
  Ensuite, si elle est desactivee, tailles et dates sont utilisees sans lire
  le contenu pour la comparaison. Les fichiers presents d'un seul cote ne
  sont jamais haches pour cette comparaison.
- En cas de changement detecte, la version avec la date la plus recente
  prevaut. A date identique avec un contenu different prouve par SHA-256,
  le moteur s'arrete sur un conflit a resoudre manuellement.
- Il n'y a aucune verification par checksum apres transfert. Le catalogue
  d'archives de recuperation conserve son mecanisme de protection independant.
- Le format des profils V6 stocke l'option de comparaison. Les anciens
  profils V1 a V5 sont lus avec comparaison SHA-256 optionnelle desactivee.
