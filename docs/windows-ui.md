# Interface Windows — Synchrorep

L'interface `windows/main.cpp` est une première reproduction **fonctionnelle** des fiches de configuration GNOME dans une fenêtre Win32. Elle ne prétend pas respecter encore la présentation visuelle de chaque dialogue GTK 2.

## Configuration

Une fiche comprend son nom, deux emplacements, un fichier `known_hosts` (SFTP), la stratégie générale en cas de conflit et l'option de propagation des suppressions. Le fichier `profiles.dat` conserve ces informations sans identifiants.

La liste à gauche permet de sélectionner, créer, enregistrer ou effacer une fiche. Le bouton **Preview** affiche les opérations prévues (copies, suppressions, conflits). Le bouton **Synchronize** confirme les suppressions et bloque les conflits non résolus, conformément au moteur.

## Connexions

Deux jeux de champs de connexion (utilisateur et mot de passe) sont disponibles, indépendamment du type d'endpoint. Les commandes **Remember logins** et **Load saved logins** s'adressent à Windows Credential Manager et indexent les identifiants par protocole/hôte/port. Un fichier SFTP `known_hosts` est obligatoire et doit être préalablement validé par l'utilisateur.

Les mots de passe ne figurent pas dans les URI des profils, l'historique ou les journaux de test.

## Explorateur Windows

La case **Explorer context menu** enregistre ou retire une clé dans `HKCU\Software\Classes\Directory\shell\Synchrorep`. Aucun privilège administrateur n'est requis. Avec Windows 11, l'option est dans le menu classique (Afficher d'autres options). Lorsqu'un répertoire est fourni en paramètre, il sert de point de départ et l'application tente de retrouver une fiche existante qui l'utilise.

## Limitations

La synchronisation est maintenant lancee sur un thread de travail et la fenetre reste utilisable. Une barre indique l'avancement par phase; Cancel operation interrompt les requetes libcurl FTP/SFTP (les fichiers locaux sont controles entre fichiers). Keep left / Keep right / Skip file permettent de resoudre individuellement les conflits listes. Les groupes, le suivi d'octets, les dialogues GTK exacts et le menu moderne Windows 11 restent manquants. Voir docs/execution.md. Les contrôles ne sont pas encore soumis à des tests UI Windows automatisés.

## Recuperation

En cas de synchronisation interrompue, cocher Resume interrupted run apres examen de Preview pour autoriser une nouvelle analyse.


## Versions conservees et journaux

Une fiche de synchronisation contient maintenant la preference **Preserve old
versions** (activee par defaut). Les fichiers remplaces ou supprimes sur les
endpoints locaux, WSL, FTP ou SFTP sont sauvegardes dans l'archive locale avant
leur modification. Les boutons **Browse / restore archives** et **Open execution
logs** ouvrent les emplacements locaux dans l'Explorateur.

Les profils au format SYNCHROREP-PROFILES1 sont lus automatiquement en activant
la protection ; toute nouvelle sauvegarde de profil utilise le format
SYNCHROREP-PROFILES2.

Voir [le guide de restauration](recovery-archive.md).


## Navigateur des archives (0.8)

**Browse / restore archives** ouvre une fenetre listant les anciennes versions
et leur execution. **Export selected** demande le dossier de destination puis
controle l'integrite avant copie. Il n'ecrase jamais un fichier deja present.
Cette operation n'est pas une restauration directe dans les dossiers synchronises
et ne peut pas encore etre automatisee en lot.
