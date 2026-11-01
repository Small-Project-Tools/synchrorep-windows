# Interface Windows — Synchrorep

## Interface sombre compacte (0.13.8)

La fenêtre principale utilise un thème graphite/bleu, une disposition qui
s'adapte à la largeur disponible et quatre onglets accessibles au clavier :

- **Configuration** : sélection des profils, nom, source et destination
  (local, WSL, FTP, SFTP), choix entre synchronisation bidirectionnelle et
  miroir continu, options de sécurité (suppression, archives, Corbeille, hash).
- **Preview** : liste des décisions du moteur et arbitrage des conflits
  Keep left / Keep right / Skip avant exécution.
- **History** : catalogue de restauration et journaux d'exécution.
- **Settings** : connexions et secrets d'un côté, politique de conflits,
  lancement automatique et menu Explorateur de l'autre.

Le bandeau inférieur conserve Save profile, Preview, Start sync et Stop
pendant la navigation. Le résumé affiche uniquement des décisions réelles
issues de Preview ; les nombres « Updates » non déterminables sont laissés
vides, sans simulation. Les options FTP/SFTP restent stockées dans Windows
Credential Manager comme auparavant.

La mise en page est testée sur 880×520, 1024×600 et d'autres résolutions.
En dessous de 1100 px, le panneau récapitulatif est replié et l'état est
affiché dans le bandeau inférieur. Aucun contrôle opérationnel n'est perdu.
Les onglets sont des boutons natifs à rendu personnalisé : il n'y a pas
de moteur Web ni de dépendance WebView.

Les seuls modes implémentés par le moteur sont **bidirectionnel** et
**miroir continu**. L'interface n'affiche pas un faux mode « one-way »,
une planification ni une liste d'exclusions qui ne seraient pas exécutables.

Les tests automatiques comprennent la géométrie (Linux/Windows),
la navigation Win32 réelle en session isolée, la compilation MinGW,
les tests sous Wine et les tests MSVC GitHub Actions. En session CI non
interactive, le test de navigation vérifie les onglets et le mode ; le
scénario de prévisualisation réelle s'exécute dans la session X de Wine
et sur Windows 11 interactif pour éviter les dialogues modaux bloquants.
L'environnement
de test ne modifie pas les profils personnels ni l'autostart Windows.

Le contrôleur Win32 `windows/main.cpp` délègue la mise en page et le thème
à `windows/ui_layout.cpp` et `windows/ui_theme.cpp`. Les comportements de
synchronisation et les règles de sécurité du moteur restent inchangés.

## Configuration

Une fiche comprend son nom, deux emplacements, un fichier `known_hosts` (SFTP), la stratégie générale en cas de conflit et l'option de propagation des suppressions. Le fichier `profiles.dat` conserve ces informations sans identifiants.

La liste à gauche permet de sélectionner, créer, enregistrer ou effacer une fiche. Le bouton **Preview** affiche les opérations prévues (copies, suppressions, conflits). Le bouton **Start sync** confirme les suppressions et bloque les conflits non résolus, conformément au moteur.

## Connexions

Deux jeux de champs de connexion (utilisateur et mot de passe) sont disponibles, indépendamment du type d'endpoint. Les commandes **Remember logins** et **Load saved logins** s'adressent à Windows Credential Manager et indexent les identifiants par protocole/hôte/port. Un fichier SFTP `known_hosts` est obligatoire et doit être préalablement validé par l'utilisateur.

Les mots de passe ne figurent pas dans les URI des profils, l'historique ou les journaux de test.

## Explorateur Windows

La case **Explorer context menu** enregistre ou retire une clé dans `HKCU\Software\Classes\Directory\shell\Synchrorep`. Aucun privilège administrateur n'est requis. Avec Windows 11, l'option est dans le menu classique (Afficher d'autres options). Lorsqu'un répertoire est fourni en paramètre, il sert de point de départ et l'application tente de retrouver une fiche existante qui l'utilise.

## Limitations

La synchronisation est maintenant lancee sur un thread de travail et la fenetre reste utilisable. Une barre indique l'avancement par phase; Cancel operation interrompt les requetes libcurl FTP/SFTP (les fichiers locaux sont controles entre fichiers). Keep left / Keep right / Skip file permettent de resoudre individuellement les conflits listes. Les groupes, le suivi d'octets, les dialogues GTK exacts et le menu moderne Windows 11 restent manquants. Voir docs/execution.md. Les principaux contrôles et onglets disposent désormais d'un test Win32
isolé exécuté sur les runners Windows.

## Recuperation

En cas de synchronisation interrompue, cocher Resume interrupted run apres examen de Preview pour autoriser une nouvelle analyse.


## Versions conservees et journaux

Une fiche de synchronisation contient maintenant la preference **Preserve old
versions** (activee par defaut). Les fichiers remplaces ou supprimes sur les
endpoints locaux, WSL, FTP ou SFTP sont sauvegardes dans l'archive locale avant
leur modification. Les boutons **Browse / restore archives** et **Open execution
logs** ouvrent les emplacements locaux dans l'Explorateur.

Les profils des formats SYNCHROREP-PROFILES1 et SYNCHROREP-PROFILES2
sont lus automatiquement. Les sauvegardes utilisent maintenant le
format SYNCHROREP-PROFILES3, ajoutant la preference de Corbeille locale.

Voir [le guide de restauration](recovery-archive.md).


## Navigateur des archives (0.8)

**Browse / restore archives** ouvre une fenetre listant les anciennes versions
et leur execution. **Export selected** demande le dossier de destination puis
controle l'integrite avant copie. Il n'ecrase jamais un fichier deja present.
Cette operation n'est pas une restauration directe dans les dossiers synchronises
et ne peut pas encore etre automatisee en lot.


## Corbeille locale Windows (0.9)

La nouvelle case **Use Windows Recycle Bin (local)** est active par defaut.
Le moteur utilise IFileOperation uniquement pour les disques fixes Windows
locaux. Les partages UNC, WSL et endpoints distants restent proteges par
les archives applicatives lorsque cette option est activee.

Le bilan de synchronisation distingue les fichiers recycles du total de
suppressions. En cas d'echec de la Corbeille Windows, l'operation s'arrete
sans effacement definitif de secours. Le fonctionnement a ete compile et
teste par simulation, mais pas encore execute sur Windows 11.


## Extension moderne Explorer Windows 11 (0.10)

Une bibliothèque native IExplorerCommand est créée en complément du menu
classique. Elle nécessite un MSIX sparse signé et installé ; ni la simple
DLL ni l’installeur MSI seul ne suffisent. Les sources, le manifeste, les
assets et les tests de contrats sont disponibles dans le projet.
Voir [le guide Windows 11](windows11-explorer.md).


## Mode miroir continu (0.11)

La case Continuous local -> target mirror active un suivi source locale vers
cible uniquement, y compris cible WSL, FTP et SFTP. Les profils precedents
restent bidirectionnels. La surveillance continue lorsque la fenetre est
masquee en zone de notification. Voir [mode miroir](mirror-mode.md).


## Demarrage automatique par profil (0.12)

**Start at Windows login** est accessible uniquement pour un profil de
miroir. La preference est conservee dans le format V5, les anciens
profils restant desactives par defaut. Le bouton Save enregistre ou retire
la valeur correspondante de HKCU Run. Le miroir se lance dans la zone
de notification lors de la connexion Windows ; plusieurs miroirs
peuvent fonctionner dans des processus independants. Voir
[demarrage automatique](automatic-startup.md).


## Édition Windows unique (0.13.0)

Le MSI \`synchrorep-windows-0.13.0-x64.msi\` contient tous les transports :
local, WSL, FTP et SFTP. Les anciennes déclinaisons Lite/Full ne sont
plus distribuées ; les anciennes installations Windows Installer sont
mises à niveau via le même \`UpgradeCode\`.

Les DLL curl, libssh2 et les autres dépendances sont embarquées
avec leurs notices de licence. La sécurité TLS de curl repose sur
OpenSSL et libssh2.


## Identite visuelle (0.12.3)

La nouvelle icone apparait dans la fenetre principale, la fenetre de
restauration, le menu contextuel et la zone de notification. Le
raccourci du menu Demarrer et l'entree Applications installees
sont egalement associes au pictogramme. Test Win32 programme en CI.

## Selection native WSL et fenetre d'execution (0.13.5)

Le dialogue IFileOpenDialog n'exige plus FOS_FORCEFILESYSTEM sur
la navigation intermediaire : Windows peut presenter les emplacements
Linux/WSL. L'acces \wsl.localhost est ajoute en raccourci quand le Shell
le resout, mais la selection finale doit etre un dossier de systeme de
fichiers effectivement accessible.

La progression s'affiche dans une fenetre de synchronisation distincte.
La configuration reste dans sa propre fenetre, et fermer celle-ci durant
l'execution ne detruit pas le processus de synchronisation.

Le SHA-256 est optionnel apres la premiere comparaison des fichiers communs,
et la date la plus recente determine la version a copier en cas de divergence.
