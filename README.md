# Synchrorep Windows

Portage Windows de [Synchrorep](https://github.com/sebk69/Synchrorep) (Sébastien Kus, 2009–2019, GPL-3.0).

**Prototype en développement : ne pas synchroniser de données irremplaçables.**

## Fonctionnalités

- Moteur C++20 commun à tous les protocoles, synchronisation bidirectionnelle, propagation des créations/suppressions, historique par paire d'emplacements et gestion des conflits.
- Endpoints : local Windows et chemins UNC, WSL via `wsl://Ubuntu/home/user`, FTP et SFTP via libcurl.
- FTP testé en intégration sur serveur isolé (MLSD, écriture, lecture, sous-dossiers, suppressions).
- SFTP testé en intégration sur serveur SSH isolé (clé d'hôte contrôlée, écriture, lecture, sous-dossiers, suppressions).
- Interface Win32 initiale avec prévisualisation et bouton Synchroniser ; entrée dans le menu contextuel classique de l'Explorateur.

## Compilation et tests Linux

Nécessite CMake, C++20, libcurl-dev ; les tests réseau nécessitent Python 3, pyftpdlib et Paramiko.

```bash
cmake -S . -B build-remote -DSYNCHROREP_REMOTE=ON
cmake --build build-remote --parallel
ctest --test-dir build-remote --output-on-failure
bash tests/integration/run.sh
```

Le script démarre deux serveurs loopback temporaires, sans utiliser de serveurs externes.

## Compilation Windows avec FTP/SFTP

Sous Windows, utiliser Visual Studio C++, CMake et [vcpkg](https://github.com/microsoft/vcpkg). Le manifeste vcpkg inclut curl avec libssh2, SSL et protocoles non HTTP.

```powershell
cmake -S . -B build-win-full -A x64 -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DSYNCHROREP_REMOTE=ON
cmake --build build-win-full --config Release
```

**À valider sous Windows réel** : cette recette MSVC/vcpkg n'a pas encore été exécutée sur un runner Windows. La pipeline MinGW produit volontairement un exécutable **local/WSL uniquement** avec `SYNCHROREP_REMOTE=OFF`.

### Connexions distantes

Les URL sont de la forme `ftp://host:21/path/` ou `sftp://host:22/path/`. Ne pas inclure d'identifiants dans l'URL.

| Protocole | Variables d'environnement |
| --- | --- |
| FTP | `SYNCHROREP_FTP_USER`, `SYNCHROREP_FTP_PASSWORD` |
| SFTP | `SYNCHROREP_SFTP_USER`, `SYNCHROREP_SFTP_PASSWORD`, `SYNCHROREP_SFTP_KNOWN_HOSTS` |
| SFTP (optionnel) | `SYNCHROREP_SFTP_PRIVATE_KEY` |

L'intégration du Gestionnaire d'identifiants Windows et les écrans de gestion de connexion restent à développer.

Pour installer le menu classique Explorateur (Windows 11 : « Afficher d'autres options ») :

```powershell
.\windows\install-context-menu.ps1 -ExePath "C:\path\to\synchrorep.exe"
```

## Limites

L'historique texte `SYNCHROREP3` n'est pas encore compatible avec SQLite d'origine. FTP nécessite un listing MLSD exploitable. SFTP nécessite un listing POSIX accepté et un fichier known_hosts. Les suppressions et lots ne sont pas transactionnels. Les modifications de même taille effectuées dans la même seconde peuvent ne pas être détectées avant ajout d'un historique de hachage.

Le portage fidèle des écrans GTK, des règles MD5, des questions par fichier, des groupes, des journaux et de la Corbeille reste à terminer. Voir [compatibilité](docs/compatibility.md).

## Licence

GPL-3.0 ; licence originale conservée dans [LICENSE](LICENSE). Les dépendances conservent leurs propres licences.
