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

La synchronisation fonctionne sur le thread UI et bloque temporairement la fenêtre. La progression, l'annulation, la résolution individuelle des conflits, les groupes et le support du menu moderne Windows 11 ne sont pas encore réalisés. Les contrôles ne sont pas encore soumis à des tests UI Windows automatisés.
