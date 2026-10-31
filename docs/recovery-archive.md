# Archives de récupération et journaux — Synchrorep 0.7

## Objectif et état

Le moteur Synchrorep conserve maintenant **une copie locale vérifiée** de chaque
ancienne version avant de remplacer ou de supprimer un fichier. Ce mécanisme
fonctionne sur les endpoints locaux, WSL, FTP et SFTP : l'ancienne version
distante est téléchargée dans l'archive locale avant toute mutation.

Cette fonctionnalité est une **quarantaine applicative**, et non la Corbeille
native de Windows. La restauration est actuellement manuelle. Le moteur n'offre
pas encore une transaction atomique globale ni un bouton « Restaurer ».

## Où trouver les sauvegardes ?

Sous Windows, dans `%LOCALAPPDATA%\Synchrorep\recovery\`. Les journaux sont
dans `%LOCALAPPDATA%\Synchrorep\logs\`.

Sous Linux, les répertoires `recovery/` et `logs/` se trouvent à côté du
fichier d'historique fourni au moteur.

Chaque synchronisation produit un répertoire distinct :

```text
recovery/
  <nom-du-fichier-etat>/
    <identifiant-unique>/
      left/
        manifest.tsv
        data/...
      right/
        manifest.tsv
        data/...
```

Les dossiers `left` et `right` désignent l'endpoint dont le fichier a été
supprimé ou remplacé (et non le sens de la copie). Les noms relatifs sont
conservés sous `data/`.

`manifest.tsv` est un format délimité par des espaces avec des champs entre
guillemets (style `std::quoted`) : raison, identité de l'endpoint, chemin
relatif, répertoire (0/1), taille, empreinte SHA-256 et chemin dans l'archive.

## Restauration manuelle

1. Arrêter toute synchronisation de cette paire d'emplacements.
2. Dans Synchrorep Windows, utiliser **Open recovery archive**.
3. Identifier la bonne synchronisation et le bon côté grâce à
   `manifest.tsv`. Vérifier le contenu et l'empreinte avant restauration.
4. Copier manuellement le fichier depuis `data/<chemin>` vers son emplacement
   original (local, WSL ou serveur distant via un outil approprié).
5. Relancer **Preview** avant toute nouvelle synchronisation. Les divergences
   peuvent être classées en conflit et nécessiter une résolution explicite.

Ne pas renommer ni supprimer les archives lorsque la restauration n'est pas
encore terminée. Aucun mécanisme de purge automatique n'est implémenté.

## Sécurité, stockage et coûts

- La politique **Preserve old versions** est activée par défaut et conservée
  dans les profils. Une ancienne fiche de format V1 est lue avec cette option
  activée.
- La copie est vérifiée par SHA-256 contre la photographie de l'endpoint.
  Toute erreur de lecture, de disque plein ou d'intégrité bloque la
  suppression/le remplacement concerné.
- Les sauvegardes distantes sont téléchargées sur le poste qui exécute
  Synchrorep. Prévoir suffisamment d'espace disque local, y compris pour
  les volumes SFTP importants.
- Les copies archivées ne sont **pas chiffrées** au repos. Les protéger
  avec les permissions du profil utilisateur et le chiffrement de disque.
- Une copie ou suppression effectuée avant une erreur ultérieure n'est pas
  annulée automatiquement. Le journal de récupération `.journal` exige une
  reprise explicite.
- L'archive ne supprime pas la nécessité d'autres sauvegardes, et n'empêche
  pas toutes les modifications concurrentes sans verrouillage des endpoints.

## Journal d'exécution

Chaque paire d'emplacements a un journal JSONL sous `logs/`, ouvert via
**Open execution logs** dans l'application. Il contient une ligne JSON par
événement horodaté : démarrage, création de sauvegarde, copie, suppression,
annulation, erreur et validation de l'historique.

Les mots de passe ne sont pas enregistrés dans le journal. Les chemins des
fichiers et les adresses des serveurs peuvent cependant y figurer : traiter
les journaux comme des données privées.

## Tests

La suite automatique vérifie :
- Copies précédant un écrasement local, suppression de fichier et de répertoire
- Copies de versions FTP et SFTP avant remplacement ou suppression
- Rejet d'une sauvegarde dont le SHA-256 diffère
- Gestion des doublons et chemins de traversée
- Relecture des profils V1 et V2, avec la protection par défaut
- Résultats de journalisation, annulation et reprise
