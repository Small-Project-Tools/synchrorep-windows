# Intégration native au menu Windows 11 — Synchrorep 0.10

## Deux niveaux de menu

**Menu classique (Windows 10 et « Afficher d'autres options » sous Windows 11)**

Il est activé/désactivé par la case **Explorer context menu** dans
l'application, via les clés de registre HKCU de l'utilisateur courant.
Il reste disponible sans identité de package MSIX.

**Menu moderne de Windows 11**

Le projet dispose d'une DLL COM `SynchrorepExplorer.dll` implémentant
`IExplorerCommand`. Elle s'enregistre avec
`windows.fileExplorerContextMenus` et `windows.comServer` via le manifeste
`windows/sparse/AppxManifest.xml`.

Ce menu moderne exige une **identité d'application** et un package MSIX
*sparse* **signé et installé**. La compilation de la DLL ou l'installation
du MSI classique ne suffit pas à afficher cette commande dans le menu
principal Windows 11.

Référence Microsoft :
https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/integrate-packaged-app-with-file-explorer

## Architecture

La DLL reste volontairement petite. Elle fournit le texte de la commande,
la limite aux dossiers sélectionnés et lance
`synchrorep.exe "<répertoire>"`. Elle n'analyse pas les fichiers ni le
réseau dans le processus Explorateur. Les opérations longues restent dans
l'application principale.

Le CLSID est défini dans `windows/explorer_clsid.hpp` et doit correspondre
exactement à celui du manifeste. Le test statique
`tests/sparse_manifest_test.py` vérifie cette identité.

## Compilation

Le job GitLab Linux/MinGW compile une version **locale/WSL seulement** :

```text
build-win/synchrorep.exe
build-win/SynchrorepExplorer.dll
```

La DLL et l'exécutable doivent se trouver **dans le même dossier**.
Pour FTP/SFTP, compiler avec MSVC + vcpkg (`SYNCHROREP_REMOTE=ON`) :

```powershell
cmake -S . -B build-msvc -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static -DSYNCHROREP_REMOTE=ON
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

## Création du package sparse

Sur Windows, avec le **Windows SDK** et `MakeAppx.exe` accessibles :

```powershell
.\windows\sparse\package.ps1 -ApplicationDirectory .\build-msvc\Release `
  -Output Synchrorep.Windows.0.10.0.sparse-unsigned.msix
```

Le script ne signe **pas** le package. Le champ `Publisher` de
`windows/sparse/AppxManifest.xml` doit être remplacé par le sujet exact
du certificat utilisé pour signer la version distribuée.

Signer avec `SignTool.exe`, et un certificat de signature de code approuvé
sur la machine cible. Ne pas stocker de clé privée dans GitLab.
Exemple de validation d'un package préalablement signé :

```powershell
Get-AuthenticodeSignature .\Synchrorep.Windows.0.10.0.sparse.msix
```

Après installation du MSI **et** installation du package sparse signé,
enregistrer le package en indiquant le chemin réel où se trouvent
`synchrorep.exe` et `SynchrorepExplorer.dll` :

```powershell
Add-AppxPackage -Path .\Synchrorep.Windows.0.10.0.sparse.msix `
  -ExternalLocation 'C:\Program Files\Synchrorep'
```

Ne pas essayer d'enregistrer le package non signé en production.
Tester les règles de confiance du certificat, les chemins d'installation,
la visibilité du menu et la désinstallation sur une machine Windows 11
avant publication.

## Validation disponible et limites

- DLL COM compilée avec MinGW POSIX (x64).
- `DllGetClassObject` et `DllCanUnloadNow` exportés.
- Tests C++ de citation des arguments Windows, y compris les chemins
  avec espaces et antislash final.
- Test de contrat COM natif préparé : `explorer_command_tests.exe`.
- Manifestes WiX et MSIX contrôlés statiquement.
- **Non validés sur un véritable poste Windows :** chargement effectif de
  l'extension par Explorer, installation/signature du sparse MSIX,
  fonctionnement FTP/SFTP natif, création effective et installation du MSI.

Le job manuel `windows-msvc-full` produit le MSI avec les deux binaires.
Le job `windows-sparse-unsigned` prépare un package MSIX non signé à
signer et tester séparément. Ces jobs nécessitent un runner Windows.


## Identite visuelle de l'extension (0.12.3)

La commande moderne IExplorerCommand::GetIcon renvoie le chemin de
l'executable et l'index d'icone 0. Le menu classique utilise la valeur
Icon enregistre dans HKCU. Le MSI associe la meme icone au raccourci du
menu Demarrer et a l'entree Applications installees.

Le paquet MSIX sparse utilise egalement les logos PNG generes depuis
le meme visuel. Les essais natifs sous Windows 11 restent necessaires
pour valider le rendu de ces integrations.
