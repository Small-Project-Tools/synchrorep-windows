# État de compatibilité — Synchrorep Windows 0.13.0

## Fonctionnalités

| Fonction | État |
|---|---|
| Dossiers locaux Windows | Implémenté et testé via MinGW/Wine |
| WSL (chemins `wsl://` et UNC) | Implémenté, test Windows 11 réel encore nécessaire |
| FTP | Implémenté avec libcurl, tests Linux ; tests Windows/Wine en CI |
| SFTP | Implémenté avec libcurl/libssh2, `known_hosts` obligatoire ; tests Windows/Wine en CI |
| Synchronisation bidirectionnelle | Implémentée (état `SYNCHROREP4`) |
| Miroir continu local → cible | Implémenté, 500 ms de contrôle local et 60 s de réconciliation |
| Démarrage automatique des miroirs | HKCU Run, profil V5, prévention des doublons |
| Historique, archives SHA-256 et reprise | Implémentés, validation de la Corbeille native à compléter |
| Interfaces graphiques Win32 | Implémentées, validation visuelle sur Windows 11 nécessaire |
| Menu contextuel classique | Installé automatiquement par MSI depuis 0.13.1 |
| Menu moderne Windows 11 | DLL IExplorerCommand compilée ; MSIX sparse signé encore nécessaire |
| MSI unique local/WSL/FTP/SFTP | Build MinGW automatisé depuis 0.13.0 |
| MSI signé / authentification Authenticode | Pas encore implémentée |

## Édition unifiée 0.13.0

Il n'existe plus de déclinaisons Lite ou Full. La compilation principale
Windows x64 active toujours `SYNCHROREP_REMOTE=ON`. La variante
`curl` de MSYS2 s'appuie sur OpenSSL pour TLS et libssh2
pour SFTP, sans imposer l'installation de MSVC sur le runner de livraison.

La CI récupère les versions et dépendances du dépôt MSYS2 en HTTPS,
vérifie l'empreinte SHA-256 de chaque archive, contrôle les importations
DLL de l'application et incorpore les binaires nécessaires dans le MSI.
Le paquet contient également `THIRD_PARTY_NOTICES.txt`.

Une compilation MSVC/vcpkg reste disponible comme vérification optionnelle
mais ne produit pas une édition commerciale distincte.

Le même `UpgradeCode` MSI assure la continuité avec les versions
antérieures (y compris la précédente appellation Lite).

## Points nécessitant une validation native Windows

- Installation et mise à niveau sur un véritable Windows 11, notamment
  les comportements de l'Explorateur et de la zone de notification.
- Fonctionnement de la Corbeille du système via Shell IFileOperation.
- Signature Authenticode du MSI et création/signature du MSIX sparse.
- Comportements des comptes FTP/SFTP et des chemins WSL sur des environnements réels.

## Limites du moteur

- Les anciens états SYNCHROREP3 et la base SQLite GNOME ne sont pas
  migrés silencieusement : une migration supervisée reste nécessaire.
- Les analyses complètes calculent SHA-256 en lisant les contenus ;
  un scan FTP/SFTP peut provoquer des transferts coûteux.
- Les copies et suppressions ne sont pas transactionnelles sur tout
  le lot : une reprise peut être nécessaire après interruption.
- Les suppressions et remplacements disposent de sauvegardes et d'une
  vérification SHA-256, mais une perte du stockage de récupération reste
  un risque distinct.
