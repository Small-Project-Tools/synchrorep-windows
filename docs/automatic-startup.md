# Démarrage automatique des miroirs sous Windows — 0.12

## Activer

1. Ouvrir Synchrorep, sélectionner un profil enregistré (ou en créer un).
2. Cocher **Continuous local -> target mirror**.
3. Vérifier la source locale à gauche, la cible à droite et les suppressions dans **Preview**.
4. Conserver de préférence **Preserve old versions** activé.
5. Cocher **Start at Windows login** et cliquer **Save**. Confirmer
   explicitement l’autorisation de mise à jour automatique de la cible.
6. À la prochaine ouverture de session Windows, Synchrorep démarre ce miroir
   en arrière-plan, dans la zone de notification, sans confirmation répétée.

Il est possible d’activer plusieurs profils miroirs : un processus
Synchrorep indépendant est lancé pour chacun. Un verrou nommé par
couple source/cible empêche qu’une seconde instance identique fonctionne
simultanément. Les profils bidirectionnels n’ont pas cette option.

## Authentification FTP et SFTP

Pour une cible FTP ou SFTP, cliquer au préalable sur **Remember logins**
et renseigner le fichier de confiance `known_hosts` pour SFTP.
L’application exige la présence d’identifiants dans Windows Credential
Manager avant d’autoriser le démarrage automatique du profil.

**Aucun mot de passe n’est stocké dans les profils, la ligne de commande
ou les clés de démarrage du Registre.** Seul le nom du profil est passé
à `synchrorep.exe --auto-mirror "<nom>"`, puis les identifiants sont
chargés du Gestionnaire d’identifiants Windows au lancement.

## Implémentation

L’inscription se fait pour l’utilisateur courant, sans droits administrateur,
dans :

`HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`

Une valeur `SynchrorepMirror-<SHA256 du nom de profil>` est créée pour
chaque profil coché. Chaque entrée contient le chemin exécutable et le
nom du profil convenablement échappés pour Windows. Les entrées sont
synchronisées à l’enregistrement, à la suppression des profils et à
l’ouverture normale de l’application : renommer ou désactiver un miroir
supprime l’ancienne valeur au prochain **Save**.

Le profil est rechargé depuis
`%LOCALAPPDATA%\Synchrorep\profiles.dat` ; le fichier passe
au format `SYNCHROREP-PROFILES5`. Les formats V1 à V4 sont acceptés,
avec `auto_start=false` par défaut.

## Arrêt, erreurs et limitations

- **Stop continuous mirror** dans le menu de la zone de notification
  interrompt le miroir courant, mais ne désactive pas le lancement à la
  prochaine connexion Windows. Pour cela, décocher **Start at Windows login**
  et enregistrer le profil.
- En cas d’erreur (source absente, authentification refusée, journal de
  reprise en attente, etc.), la surveillance s’arrête et la fenêtre
  s’affiche pour signaler l’erreur. **Aucune reprise automatique d’un
  journal interrompu** n’est autorisée.
- Le moteur n’est actif que pendant **la session utilisateur connectée**.
  Il ne s’agit pas d’un service Windows : une déconnexion/arrêt du poste
  termine les miroirs.
- Le répertoire de l’application doit rester accessible à son emplacement
  enregistré dans le Registre. Une mise à jour manuelle du chemin est
  répercutée à la prochaine ouverture normale de Synchrorep.
- Il n’y a pas de planification quotidienne ni de lancement sans ouverture
  de session.
- La compilation MinGW et les tests sont exécutés dans la sandbox.
  Le test Win32 du Registre `startup_registry_tests` sera exécuté par
  le job manuel Windows MSVC ; une connexion interactive à un véritable
  Windows 11 reste nécessaire pour valider l’affichage et le démarrage
  réel dans la zone de notification.

## Tests

`startup_tests` vérifie l’éligibilité des profils, la migration et les
noms de valeurs de démarrage stables.

`startup_registry_tests` vérifie sur Windows la création, la mise à jour et
la suppression des seules entrées Synchrorep dans une **clé temporaire de
test HKCU** ; les entrées des autres applications sont préservées et aucune
inscription au véritable démarrage n’est modifiée par ce test.


## Validation du paquet Lite 0.12.1

Une installation MSI x64 generee par \`wixl\` et le lancement
\`synchrorep.exe --auto-mirror AutoSmokeCI\` ont ete executes sous Wine
avec un profil V5 dans un prefixe jetable. Le scenario a couvert :

- copie et suppression initiales vers la cible ;
- archivage SHA-256 avant suppression ;
- creation d'un nouveau fichier, propagation des suppressions ;
- correction d'une modification faite directement sur la cible ;
- presence d'un journal de synchronisation et absence de journal
  d'interruption lors de la fin d'un cycle.

Le test \`tests/wine_autostart_smoke.sh\` sert de verification reproductible.
Il suppose un prefixe Wine de test isole, **ne doit jamais etre execute
avec les profils d'un utilisateur reel**, et coupe la Corbeille native car
Wine ne fournit pas l'integration Windows Shell correspondante.

Le test de la Corbeille native et de la connexion a Windows 11 exige
toujours une session Windows reelle. L'edition Lite ne contient pas
les transports FTP/SFTP.


Le test final de packaging a également vérifié le désinstallateur MSI :
le fichier exécutable installé a été retiré correctement dans un nouveau
préfixe Wine. L'utilisateur doit décocher le lancement automatique
avant une désinstallation sur Windows pour éviter de conserver des valeurs
HKCU Run pointant vers un exécutable supprimé ; le MSI ne supprime pas
arbitrairement les profils ni les valeurs dynamiques de chaque utilisateur.
