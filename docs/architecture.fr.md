<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

**Français** · [English](architecture.md)

# Architecture : un moteur, deux interfaces

*Décision d'architecture pour le travail multiplateforme (Windows, Linux, macOS). Écrit pour les contributeurs.*

## 1. Objectif et contraintes

ShaderToMP4 doit fonctionner sur **Windows, Linux et macOS**, comme un programme **portable** (aucun installeur, rien d'écrit en dehors de son dossier), distribué sous **GPL-3.0-or-later**, avec toutes les dépendances tierces compatibles GPL et listées dans [THIRD_PARTY_NOTICES.fr.md](../THIRD_PARTY_NOTICES.fr.md). Tous les textes de l'interface viennent de `lang/*.json`.

Le code est aujourd'hui découpé en deux couches :

- `src/core/` : le moteur de rendu (OpenGL 3.3 core, tube vers ffmpeg, parties et reprise, réglages, i18n, auto-test). Il est indépendant de l'interface et n'appelle aucune API du système : tout passe par `src/platform/`.
- `src/gui/` : l'interface Windows (Win32 + Direct2D/DirectWrite, SVG via Direct2D).

Depuis la version 1.8 le moteur contient aussi le code que les deux interfaces partagent pour les fonctions qui ne sont pas le rendu lui-même, afin qu'une fonction ne soit écrite qu'une fois : `outputopts` (champs et validation de la fenêtre Sortie), `filesettings` (réglages d'un seul shader, dans `jobs/`, et écriture de réglages dans un shader), `batches`, `channeldrop` (quoi écrire quand un fichier est déposé), `passfiles` et `glslsyntax` (onglets, squelettes et coloration de l'éditeur), `report` (rapport de rendu). Le code propre à Windows (`src/gui/editor.cpp`, éditeur RichEdit, fenêtres) et celui propre à ImGui (`src/ui_imgui/editor_imgui.cpp`) ne font que dessiner et appeler ces modules. `src/platform/posix/egl_headless.cpp` crée le contexte OpenGL par EGL quand il n'y a pas de serveur d'affichage.

## 2. Options étudiées pour l'interface

| Option | Pour | Contre |
|---|---|---|
| **Garder Direct2D partout** | Déjà fait | API propre à Windows : impossible sur Linux et macOS. |
| **Qt (statique)** | Widgets natifs, SVG intégré | Exécutable de plusieurs dizaines de Mo, compilation lourde, obligations LGPL/GPL à suivre, apparence sombre à refaire en feuilles de style. |
| **wxWidgets** | Apparence native | L'apparence diffère selon le système, pas de thème sombre cohérent ni de SVG sans travail supplémentaire. |
| **GLFW + Dear ImGui + NanoSVG** | Léger, même rendu partout via OpenGL (déjà requis), récupérés à la configuration avec des versions figées, licences MIT / zlib (compatibles GPL), dépôt de fichiers et DPI gérés par GLFW | Il faut recréer l'apparence actuelle avec le style d'ImGui ; pas de widgets natifs. |

## 3. Décision

- **Windows garde l'interface actuelle** (Direct2D/DirectWrite, icônes SVG, thème sombre, glisser-déposer OLE, progression dans la barre des tâches). C'est l'expérience de référence, elle reste intacte.
- **Linux et macOS utilisent une nouvelle interface** fondée sur **GLFW** (fenêtre, entrées, contexte OpenGL, dépôt de fichiers), **Dear ImGui** (widgets, stylés pour ressembler à la version Windows : cartes sombres, coins arrondis, accent en dégradé) et **NanoSVG** (rastérise les mêmes fichiers `res/icons/*.svg` en textures). Une police est embarquée dans l'exécutable pour que le texte soit rendu de la même façon sur les deux systèmes.
- **Les deux interfaces partagent tout ce qui est en dessous** : le moteur, les traductions, les réglages et les préréglages. Chaque interface garde son propre état de fenêtre (`g` dans `src/gui`, `Model` dans `src/ui_imgui`) et parle au moteur par `Hooks`.

```
 +--------------------------+     +--------------------------+
 |  Interface Windows       |     |  Interface Linux / macOS |
 |  src/gui (Direct2D)      |     |  src/ui_imgui (GLFW+ImGui)|
 +------------+-------------+     +------------+-------------+
              |                                |
              +---------------+----------------+
                              |
                  +-----------v-----------+
                  |  src/core (moteur)    |   rendu, encodeur, réglages, formats, passes,
                  |                       |   canaux, file, cli, i18n, ...
                  +-----------+-----------+
                              |
                  +-----------v-----------+
                  |  src/platform         |   seul endroit avec du code propre à un système
                  |  windows/ posix/      |
                  +-----------------------+
```

## 4. La couche plateforme (`src/platform/`)

Le moteur n'appelle jamais directement une API du système d'exploitation. Il utilise des fonctions `platform::`, écrites une fois pour Windows (`windows/`) et une fois pour Linux et macOS (`posix/`) :

| Domaine | Fonctions |
|---|---|
| Fichiers et chemins | dossier de l'application, existence, lecture/écriture, création/suppression de dossiers, renommage/déplacement, espace disque libre, liste d'un dossier, fichiers temporaires (tous les chemins sont en UTF-8 en interne) |
| Processus | lancer un processus caché avec des tubes (stdin pour les images, stdout/stderr fusionnés), attendre, terminer |
| Réseau et empreintes | téléchargement HTTPS avec annulation (WinInet sous Windows, `curl` du système ailleurs), SHA-256 (CryptoAPI sous Windows, code intégré ailleurs), extraction d'archives par le `tar` du système |
| OpenGL | créer un contexte caché, charger les pointeurs de fonctions, le rendre courant dans un thread |
| Système | ouvrir un fichier, un dossier ou une adresse avec l'application par défaut, langue de l'utilisateur, horloge |
| Données embarquées | lire une ressource embarquée (fichiers de langue, icônes) |

Le moteur garde `std::wstring` pour les textes et les chemins ; la couche plateforme convertit vers l'encodage natif à la frontière (UTF-16 sous Windows, UTF-8 sous Linux et macOS).

## 5. Threads et OpenGL

- L'interface tourne dans le **thread principal** (exigence du fenêtrage macOS).
- Le rendu tourne dans un **thread de travail** avec son propre contexte OpenGL caché, créé par la couche plateforme. Sous Linux et macOS, la fenêtre cachée est créée dans le thread principal au démarrage puis rendue courante dans le thread de travail.
- Les threads de travail ne parlent à l'interface que par la couche applicative commune (messages / variables atomiques), comme aujourd'hui sous Windows.

## 6. ffmpeg

`ffmpeg` n'est jamais embarqué. Selon le système, le programme cherche d'abord une copie à côté de lui, puis une copie adaptée dans le `PATH`, sinon propose de télécharger une version statique (empreinte SHA-256 vérifiée) dans son propre dossier :

| Système | Source |
|---|---|
| Windows | build « essentials » de gyan.dev (comportement actuel) |
| Linux (x86-64, ARM64) | build statique GPL de BtbN (SHA-256 pris dans le fichier de sommes publié avec lui) |
| macOS (Intel, Apple Silicon) | pas de téléchargement automatique : aucune source ne publie d'empreinte SHA-256 vérifiable pour macOS. Le programme cherche à côté de l'application, dans le `PATH` et dans les dossiers Homebrew. |

Les encodeurs matériels suivent le système : NVENC (Windows, Linux), AMF et QuickSync (Windows), VAAPI (Linux), VideoToolbox (macOS), avec la même sonde et le même repli sur le CPU.

## 7. Compilation, tests et distribution

- **CMake** compile Linux et macOS (et peut compiler Windows aussi) ; `scripts/build.bat` reste la voie la plus simple sous Windows.
- **GitHub Actions** compile et teste sur Windows, Linux et macOS : compilation, auto-test (rendu logiciel Mesa sous Linux), `scripts/check-repo.ps1`, archives portables, publication d'une version à partir d'une étiquette.
- **Archives portables uniquement** : `win64.zip`, `linux-x86_64.tar.gz`, `linux-aarch64.tar.gz`, `macos-universal.zip` (une `.app` dans l'archive, ni `.dmg` ni `.pkg`), avec sommes SHA-256.

## 8. Conséquences et risques

- Deux interfaces à maintenir : la couche applicative commune garde la logique partagée, seuls le dessin et la gestion des événements diffèrent.
- L'apparence Linux/macOS est une recréation en ImGui de celle de Windows ; de petites différences visuelles sont attendues et acceptables.
- macOS plafonne OpenGL à 4.1 core (le 3.3 core suffit) et le déconseille ; un chemin Metal est hors périmètre.
- Linux et macOS ne peuvent pas être lancés sur la machine du mainteneur : l'intégration continue sert de banc d'essai. Sous Linux (x86-64 et ARM64) elle compile, lance l'auto-test avec le rendu logiciel de Mesa sous un affichage virtuel et produit des captures de la fenêtre. Les machines macOS hébergées n'ont pas de GPU (pas de format de pixels OpenGL 3.3) : macOS y est seulement compilé, empaqueté et testé sommairement (`--version`) ; la fenêtre macOS n'a pas été lancée sur du vrai matériel.
- L'encodage VAAPI et VideoToolbox suit les options ffmpeg documentées mais n'a pas été testé sur du vrai matériel ; la sonde au démarrage retombe sur l'encodeur CPU en cas d'échec.
