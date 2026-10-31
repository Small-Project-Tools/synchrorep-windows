# Intégration de Synchrorep à l’Explorateur Windows

## Menu classique

Synchrorep peut enregistrer, pour l’utilisateur courant, sa commande
dans le menu classique de l’Explorateur via HKCU. L'icône est chargée
depuis `synchrorep.exe`.

## Menu moderne Windows 11

Le projet fournit `SynchrorepExplorer.dll` avec `IExplorerCommand`,
un identifiant COM fixe et un manifeste MSIX *sparse*. L'extension
ouvre l'application pour le dossier sélectionné et ne lance pas de
transfert dans le processus Explorer.

L'enregistrement dans le nouveau menu Windows 11 nécessite un package
MSIX sparse signé et associé à l’installation de l’application.
L'installation MSI seule ne suffit pas.

### Compiler une seule application

Le job `windows-mingw` construit l'unique application contenant local,
WSL, FTP et SFTP. Les fichiers `synchrorep.exe` et
`SynchrorepExplorer.dll` figurent ensemble dans le MSI et dans les
artefacts Windows.

Le script `windows/sparse/package.ps1` utilise ce même build :

```powershell
.\windows\sparse\package.ps1 -ApplicationDirectory .\build-win `
  -Output Synchrorep.Windows.0.13.0.sparse-unsigned.msix
```

La création exige Windows SDK `MakeAppx.exe`.
**Le résultat est non signé et ne doit pas être déployé tel quel.**

La valeur `Publisher` de `windows/sparse/AppxManifest.xml` doit
correspondre exactement au certificat utilisé pour signer le MSIX.
Les clés privées ne doivent jamais être stockées dans GitLab.

Une fois le MSIX signé avec un certificat approuvé :

```powershell
Add-AppxPackage -Path .\Synchrorep.Windows.0.13.0.sparse.msix `
  -ExternalLocation 'C:\Program Files\Synchrorep'
```

Le dossier externe doit correspondre au chemin réel de l'installation MSI.

## Icône

L’icône originale de Synchrorep est embarquée comme ressource Win32
`ICON 101`. Le menu moderne expose son index 0 via
`IExplorerCommand::GetIcon`, et le menu classique utilise la même
ressource. Les logos du MSIX dérivent de la même image.

## Vérifications et limites

La compilation MinGW et les tests sous Wine valident le contrat COM et
la présence de l'icône. Le chargement réel par Windows Explorer 11 et
la signature du MSIX restent à tester sur un poste Windows natif.


## Enregistrement automatique du menu classique (0.13.1)

À partir de 0.13.1, le MSI enregistre et supprime avec Windows Installer
les commandes sous HKLM\Software\Classes pour les dossiers sélectionnés,
le fond d’un dossier et les lecteurs. Il transmet le chemin du dossier à
l’exécutable en paramètre entre guillemets (%1 ou %V). L'icône correspond au
binaire installé. Ces clés sont supprimées à la désinstallation du MSI.

Sur Windows 11, accéder à la commande via **Afficher d’autres options**.
Le menu compact moderne ne sera disponible qu’après déploiement et signature
du package MSIX sparse, distinct du MSI. Un redémarrage de l’Explorateur peut
être nécessaire pour actualiser un menu déjà ouvert.


## Flux d’exécution depuis l’Explorateur (0.13.3)

Comme dans la version GNOME, la commande de menu **agit immédiatement**
sur le dossier sélectionné. Elle retrouve un profil existant et
propose sa synchronisation, ou ouvre la boîte native
\`IFileOpenDialog\` avec \`FOS_PICKFOLDERS\` pour choisir le second dossier.

Pour une nouvelle configuration, Synchrorep enregistre automatiquement
les réglages sécurisés par défaut puis demande une confirmation avant
la première exécution. Un refus de la boîte de sélection annule l’opération.
Les dossiers identiques, imbriqués ou déjà enregistrés ne sont pas
acceptés. Le programme sans argument continue d’ouvrir les paramètres.

La fenêtre de sélection et l'enchaînement des boutons seront aussi
vérifiés visuellement sur Windows 11 réel.
