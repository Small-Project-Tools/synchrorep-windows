# Synchrorep : état de compatibilité

Référence d'origine : [sebk69/Synchrorep](https://github.com/sebk69/Synchrorep), GPL-3.0.

## États du moteur d'origine

| État original | Comportement actuel |
| --- | --- |
| _files_not_modified | None |
| _from_modified / _to_modified | CopyLeftToRight / CopyRightToLeft |
| _from_deleted / _to_deleted | DeleteRight / DeleteLeft si activé |
| _both_deleted | None, suppression de l'historique |
| _both_modified_from_recent / _both_modified_to_recent | Conflict ; options Newest/Oldest |
| _both_modified_equal | None uniquement si le contenu est égal |
| _comparison_error | Erreur bloquante, pas de mise à jour de l'historique |

## Interfaces d'accès

| Support | État |
| --- | --- |
| Local | Tests automatiques Linux passés |
| WSL | Mapping Windows UNC, non encore testé sur Windows |
| FTP | libcurl, MLSD, transferts temporaires : intégration locale testée |
| SFTP | libcurl SSH, vérification known_hosts : intégration locale testée |

La synchronisation est bloquée en cas de listing incomplet, de liens symboliques ou de métadonnées insuffisantes. FTP n'est pas chiffré : préférer SFTP.

## Éléments restant à porter

- Options originales d'erreurs, de conflits et de confirmations par fichier ; modes expert et groupes.
- Comparaison MD5 configurable et détection de modifications de même taille/horodatage.
- Historique SQLite original, journal de récupération après interruption.
- Interface GTK reconstituée sous Windows (fiches, préférences, progression, logs, dialogues).
- Mise à la Corbeille ; extension moderne Explorateur Windows 11 ; installeur MSI.
- Gestion des identifiants par Windows Credential Manager.
- Validation du build Windows complet MSVC/vcpkg.

## Garanties et limites

Le nouveau format d'historique est SYNCHROREP3, associé au couple ordonné d'endpoints. L'exécution bloque par défaut sur les conflits simultanés. Les transferts utilisent des fichiers temporaires, mais une interruption au milieu d'un lot peut laisser une synchronisation partielle. Les suppressions ne disposent pas encore d'un journal transactionnel. Ne pas utiliser sur des données irremplaçables.

La CI Linux compile avec SYNCHROREP_REMOTE=ON et teste FTP/SFTP sur des serveurs temporaires. La CI MinGW compile un exécutable local/WSL seulement ; aucune validation Windows native de FTP/SFTP n'a été réalisée.
