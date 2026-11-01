# Recette Windows 11 — Synchrorep 0.13.8

**Date :** 1er novembre 2026  
**Poste :** PRECISION — Windows 11 Pro, affichage 1536 × 864, utilisateur non administrateur  
**Distribution WSL2 :** Ubuntu  
**Décision :** **recette fonctionnelle validée**, avec les réserves d'installation et d'intégration système décrites ci-dessous.

## Sources et traçabilité

- GitLab : [pipeline #2164](https://git.small-project.dev/pub/desktop/synchrorep-windows/-/pipelines/2164), commit `1fec86880b1a60fb24abb8b6196a7cf9c8970b6b`, job `windows-mingw` #8485.
- GitHub : [Actions #41341589121](https://github.com/Small-Project-Tools/synchrorep-windows/actions/runs/41341589121) — compilation et tests MSVC Windows Server 2022/2025, installation et désinstallation MSI sur Windows Server 2025 : **succès**.
- Exécutable 0.13.8 SHA-256 : `8E70A7B61E1C36C6D7E995A1D4ABB20A279005C29CC7DCCE435C3537F8556011`.
- MSI 0.13.8 SHA-256 : `DED5E4AD1DF8708E1A124ED7B70A831B7020E3E800DAB7923B2AC1E44C9A3271`.
- Traces et scripts d'essai : `%TEMP%\Synchrorep-Recipe-20261101-0138\`. Tous les profils et fichiers testés sont isolés.

## Résultats Windows 11 sur binaires GitLab 0.13.8

| Scénario | Verdict | Vérification |
| --- | --- | --- |
| Fichiers de distribution | **OK** | Artefact GitLab 8485 extrait : MSI 10 245 632 octets, 38 DLL de build, 19 exécutables de test. |
| Extraction administrative du MSI | **OK** | `msiexec /a` en dossier temporaire ; `ProductVersion=0.13.8`, 19 DLL de distribution ; exécutable extrait identique (SHA-256). |
| Installation existante | **Préservée** | Version système 0.13.5 et hash d'exécutable inchangés ; pas de modifications à `Program Files`. |
| Suite native Windows 11 | **18/19 initiaux ; 19e OK au second passage** | Le test `synchrorep_ui_tests` a échoué une fois lors d'un redimensionnement asynchrone, puis réussi en relance directe. Tous les tests du moteur et de l'intégration passent. |
| UI compacte / onglets | **OK après stabilisation** | Les onglets et commandes sont validés en Win32 ; le test automatisé de géométrie a été corrigé pour attendre le traitement de `SWP_ASYNCWINDOWPOS`. |
| Extension COM Explorateur | **OK** | `explorer_command_tests` et `explorer_launch_tests` sur Windows 11. |
| Explorateur : annulation | **OK** | Ouverture du sélecteur natif ; annulation sans création de profil ni mutation de fichiers. |
| Explorateur : première synchronisation | **OK** | Sélection du dossier cible via le sélecteur Windows ; création du profil par défaut ; copie source → destination. |
| Explorateur : relancement | **OK** | Réutilisation du profil ; lancement sans redemander de dossier ; seconde synchronisation. |
| WSL2 bidirectionnel | **OK** | Prévisualisation, Windows → `wsl://Ubuntu/...`, WSL2 → Windows, suppression du profil. |
| Miroir continu vers WSL2 | **OK** | Copie initiale, suppression des fichiers excédentaires, propagation des modifications, correction d'une dérive sur la cible, propagation de suppression, arrêt et suppression du profil. |
| FTP et SFTP natifs | **OK** | `remote_tests.exe` avec véritables serveurs d'essai FTP et SFTP sur WSL2 (`127.0.0.1:2121`, `127.0.0.1:2222`) ; sorties 0. |
| Intégrité du poste | **OK** | Aucune instance de test résiduelle, aucune entrée de démarrage Synchrorep observée après les essais ; installation système inchangée. |

## Observations et réserves

1. **Installation complète du MSI 0.13.8 sur PRECISION non effectuée.** La session n'a pas les droits administrateur et l'installation MSI est définie pour tous les utilisateurs (`ALLUSERS=1`). L'extraction du MSI est validée sur PRECISION ; l'installation et la désinstallation réelles sont validées par GitHub Actions sur Windows Server 2025.
2. **Menu contextuel système de PRECISION toujours lié à la version installée 0.13.5.** La nouvelle extension COM 0.13.8 est testée directement, et le flux complet de lancement « dossier source → sélection de destination → synchronisation » est validé avec le binaire 0.13.8, mais la présence du menu enregistré *après installation de 0.13.8* nécessite une recette avec élévation.
3. **Intermittence du test graphique : corrigée dans `windows/ui_smoke_tests.cpp`.** Le test attend maintenant que le redimensionnement asynchrone et le positionnement des contrôles se terminent, avec un délai borné et des diagnostics de géométrie ; cette amélioration n'altère pas l'application. Le premier échec ne s'est pas reproduit en exécution isolée.
4. **MSI non signé.** La signature et la publication grand public ne font pas partie de cette recette.

## Conclusion

**Synchrorep 0.13.8 passe la recette fonctionnelle sur Windows 11**, incluant les usages locaux, WSL2, FTP/SFTP, miroir continu, la nouvelle interface compacte et le parcours de démarrage depuis l'Explorateur.

**Réserve avant déclaration de conformité complète de l'installation Windows 11 :** effectuer une installation MSI 0.13.8 avec droits administrateur, vérifier l'apparition du menu contextuel Windows 11, puis désinstaller ou réinstaller selon la stratégie de déploiement, sans toucher aux données de profils.
