<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

[English](CHANGELOG.md) · **Français**

# Journal des modifications

Format : une section par version, la plus récente en premier. La section d'une version étiquetée est reprise dans les notes de version.

## Non publié

### Ajouts

- Passe Sound (`nom.sound.glsl`, `mainSound`) : rendue par le GPU dans un WAV temporaire qui devient la piste audio de la vidéo ; aussi importée depuis les exports JSON de Shadertoy.

## 1.6.1 — 2026-10-11

### Modifié

- Allemand, espagnol, italien et portugais : tous les textes de l'interface et de la ligne de commande sont traduits (les fonctions récentes retombaient sur l'anglais).

## 1.6.0 — 2026-10-11

### Ajouts

- Canaux Shadertoy : cubemaps (`cube:`), textures 3D (`volume:`), spectre et forme d'onde du son (`audio`), vidéo `video:`
- Passe Cubemap A (`nom.cubemapA.glsl`, canal `cubeA`).
- Import hors ligne d'un export `.json` de Shadertoy (interface, ligne de commande), sans compte ni clé d'API.

## 1.5.1 — 2026-10-11

### Modifications

- Accessibilité (Windows) : les champs, listes déroulantes et curseurs portent leur libellé comme nom pour UI Automation, les lecteurs d'écran annoncent donc « Durée (s) » et non la valeur.
- Captures d'écran des README rafraîchies (Windows et Linux, anglais et français).

## 1.5.0 — 2026-10-11

### Ajouts

- Variantes : `// variant=720p:width=1280|height=720` (ou `--variant=`) rend à nouveau le même shader avec d'autres réglages en une fois.
- macOS : la progression du rendu s'affiche dans l'icône du Dock.
- Linux : `gpu=high|low` (config.ini ou `--gpu=`) choisit le GPU dédié ou intégré sur un PC à deux GPU.
- Aperçu en temps réel : un bouton Lecture joue le shader à la taille de l'aperçu, en boucle, suit le curseur et se recharge quand le fichier du shader change sur le disque.
- Windows : `--dpi=N` force l'échelle de l'interface (sert à vérifier la mise en page de 125 % à 200 %).

## 1.4.0 — 2026-10-11

### Ajouts
- Formats de sortie : VP9 et AV1 (SVT-AV1, libaom en secours ; AV1 matériel essayé sur NVENC, AMF, QuickSync et VAAPI), WebM avec son Opus, ProRes (Proxy à 4444) et DNxHR, GIF animé (palette optimisée) et WebP, séquences d'images PNG / TIFF / EXR numérotées.
- Sortie 10 bits : rendu en 16 bits, H.265 Main10, AV1, VP9, ProRes / DNxHR, PNG / TIFF 16 bits.
- Taille estimée du fichier sous les réglages, avec avertissement d'espace disque ; codecs et conteneurs absents du ffmpeg installé signalés dans les listes.
- Modèle de nom de fichier (`name`) et règle pour un fichier existant (`exists` : écraser, numéroter, ignorer) ; métadonnées du conteneur (titre, commentaire) et `+faststart`.
- Menu de préréglages « Plus » (X 720p30, GIF léger, ProRes pour le montage, WebM VP9) et vos propres préréglages dans un dossier `presets`.
- Compatibilité Shadertoy : textures `iChannel0-3` (PNG, JPG, HDR, EXR, `noise` et `grid` générés, options de filtre / répétition / retournement), `iMouse` et `iDate` fixes, passes Buffer A–D et Common avec tampons flottants persistants (aussi à la reprise d'un rendu et dans l'aperçu), `#version` / `#extension` ignorés, `texture2D` et autres noms GLSL ES acceptés, et rapport de compatibilité dans le journal avant chaque rendu.
- Ligne de commande : `--render` avec tous les réglages en `--clé=valeur`, `--profile=`, `--png`, progression JSON (`--progress=json`), codes de sortie documentés, identique sous Windows, Linux et macOS.
- La carte des réglages affiche « Imposé par le shader : fps=30, … » pour le shader sélectionné (les commentaires `// clé=valeur` priment sur la fenêtre).
- Les erreurs de compilation GLSL s'écrivent « fichier:ligne: message » ; un double-clic sur une telle ligne du journal ouvre le fichier à cette ligne.
- La file d'attente survit à la fermeture du programme : les shaders restant à rendre sont retrouvés dans `queue.txt` à la prochaine ouverture.
- Fin de lot : le bouton de la barre des tâches / du Dock clignote, et une action facultative (`endaction` : montrer le dossier, ou veille après 15 s) se règle dans `config.ini` (aussi avec `--render`).
- Langues de l'interface : allemand, espagnol, italien et portugais (`lang/*.json` livrés à côté du programme ; pas encore relus par des locuteurs natifs), choisies d'après la langue du système quand elles existent.
- Accessibilité : les textes discrets et d'accentuation atteignent un contraste de 4,5:1 (WCAG AA) ; le contrôle du dépôt le vérifie, ainsi que l'identité de la palette entre les deux interfaces.
- `--watch=dossier` : rend chaque shader déposé dans un dossier (explicite, jamais par défaut).
- `--diagnostic` (système, carte graphique, OpenGL, ffmpeg et encodeurs, sans donnée personnelle) et `--benchmark` (images par seconde par résolution et par encodeur).

### Modifications
- L'auto-test rend aussi chaque format de sortie et relit chaque fichier avec ffmpeg ; l'intégration continue l'exécute désormais réellement sous Windows (OpenGL logiciel) comme sous Linux.

## 1.3.0 — 2026-10-10

- Versions Linux (x86-64, ARM64) et macOS (universelle) avec une interface GLFW + Dear ImGui au même aspect que celle de Windows ; couche plateforme ; archives portables pour les trois systèmes fabriquées par l'intégration continue ; téléchargement de ffmpeg vérifié par SHA-256 sous Linux ; encodeurs VAAPI et VideoToolbox ; `--render` sans fenêtre.

## 1.2.0 — 2026-10-10

- Licence GPL-3.0-or-later, interface et documentation bilingues (anglais / français), fenêtre « À propos », site officiel, contrôles du dépôt.

## Avant la 1.2.0

- Première version publique, puis la nouvelle interface sombre (Direct2D, icônes SVG) : cartes, boutons animés, aperçu, raccourcis, glisser-déposer OLE, progression dans la barre des tâches.
