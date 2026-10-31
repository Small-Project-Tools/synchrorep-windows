# Mode miroir monodirectionnel continu (Synchrorep 0.11)

## Principe

Le mode **Continuous local → target mirror** fait du dossier local la seule
source de vérité. À chaque synchronisation, Synchrorep garantit que la cible
reflète les fichiers et sous-dossiers présents dans la source.

- Nouveau fichier/dossier local → création sur la cible.
- Modification locale → remplacement sur la cible.
- Renommage → suppression de l'ancien chemin cible puis copie du nouveau.
- Suppression locale → suppression de la cible, si **Propagate deletions** activé.
- Fichier supplémentaire sur la cible → supprimé, si propagation activée.
- Modification faite directement sur la cible → écrasée par le contenu local.
- **Jamais de copie depuis la cible vers le dossier local**.

L'endpoint source doit être un dossier local Windows sur un volume fixe ou
amovible. Les emplacements WSL, UNC, FTP ou SFTP ne peuvent pas servir de
source de miroir. Ils restent disponibles comme **cible** (selon les
transports inclus dans la compilation).

## Utilisation dans l'interface Windows

1. Créer ou sélectionner une fiche.
2. Choisir le dossier local **Left** (source) et la destination **Right**.
3. Cocher **Continuous local → target mirror**.
4. Laisser **Preserve old versions** activé (recommandé) et sélectionner
   **Propagate deletions** selon le comportement voulu.
5. Enregistrer le profil (format `SYNCHROREP-PROFILES4`).
6. Cliquer **Preview** pour connaître les opérations qui seraient exécutées.
7. Cliquer **Synchronize** et confirmer la mise en miroir. La première passe
   recopie les données vers la cible ; Synchrorep surveille ensuite la source.

Pendant le miroir, Synchrorep reste actif en zone de notification Windows.
Réduire ou fermer la fenêtre masque l'application et **n'arrête pas** le
miroir. Le menu de la zone de notification permet de rouvrir l'interface,
d'arrêter le miroir ou de quitter complètement le programme.

Le bouton **Cancel operation** arrête la surveillance de manière coopérative.
Il n'y a pas encore de lancement automatique à la connexion Windows :
Synchrorep doit être démarré et le mode miroir activé après chaque redémarrage.

## Surveillance et réconciliation

La surveillance utilise un contrôle léger des métadonnées du dossier local
toutes les **500 ms** (et attend une deuxième observation stable avant
synchronisation). Chaque changement déclenche une comparaison intégrale des
deux endpoints et les opérations nécessaires. Toutes les **60 secondes**,
Synchrorep effectue également une comparaison complète, même sans
changement des métadonnées locales, afin de détecter :

- les modifications de même taille et date ;
- les modifications ou suppressions effectuées directement sur la cible ;
- les changements manqués par les contrôles rapides.

Il s'agit actuellement d'une **surveillance par interrogation périodique**
(polling), pas encore de `ReadDirectoryChangesW`. Un très gros dossier local
peut entraîner des analyses fréquentes et les vérifications distantes exigent
encore le téléchargement pour calculer les empreintes SHA-256.

## Protections

- À chaque cycle, la source est vérifiée avant toute opération. Une source
  manquante ou inaccessible arrête le miroir : elle ne doit pas être
  assimilée à un dossier vide.
- Si un profil miroir n'a jamais connu de source non vide, une première
  synchronisation **source vide → cible non vide** est refusée lorsqu'elle
  entraînerait une suppression. Le contrôle protège des erreurs initiales.
  Une suppression intentionnelle du dernier élément d'une source déjà
  synchronisée peut être propagée.
- Une opposition **fichier ↔ dossier** arrête le cycle, quel que soit le
  réglage général des conflits.
- Les anciennes versions de la cible sont archivées par défaut et contrôlées
  par SHA-256 avant remplacement ou suppression.
- Le journal de reprise existant bloque une reprise incontrôlée après erreur.
  Il faut examiner **Preview** puis cocher **Resume interrupted run** pour
  autoriser la reprise explicite.
- Une perte de connexion FTP/SFTP ou une erreur de lecture ne provoque pas
  de suppression de secours. La surveillance s'arrête et signale l'erreur.
- Les cycles sans modification ne réécrivent pas le journal, ni les archives
  après la première synchronisation initiale.
- L'absence de support transactionnel multi-fichiers demeure une limite.

## CLI Linux (tests et automatisation)

Le binaire CLI Linux expose le même moteur. Les chemins des deux endpoints
et le fichier d'historique sont toujours des arguments explicites :

```bash
./build-remote/synchrorep /tmp/source /tmp/target /tmp/mirror.state --mirror --dry-run
./build-remote/synchrorep /tmp/source /tmp/target /tmp/mirror.state --mirror
./build-remote/synchrorep /tmp/source sftp://host:22/target/ /tmp/mirror.state --mirror --watch
```

En mode `--watch`, la commande reste au premier plan jusqu'à Ctrl+C
(SIGINT) ou SIGTERM. La source est locale ; les mêmes variables d'environnement
provisoires servent aux identifiants FTP/SFTP. L'option `--resume` nécessite
toujours une décision explicite après une interruption.

Le mode bidirectionnel par défaut reste inchangé. Les anciens profils V1–V3
sont chargés en mode bidirectionnel, sans activation implicite du miroir.

## Tests

`mirror_tests` vérifie le sens unique, la propagation de modifications,
renommages et suppressions, la correction d'une dérive de la cible, le
refus d'une source dangereusement vide, les conflits de types, l'absence
de retour vers la source, la surveillance active, l'arrêt sur perte de
source et les changements sans variation d'horodatage.

`tests/integration/run.sh` teste également le miroir sur de véritables
serveurs FTP et SFTP isolés.

La compilation MinGW vérifie l'interface Windows, mais les interactions
graphiques et le fonctionnement 24 h/24 sur Windows 11 réel restent à tester.
