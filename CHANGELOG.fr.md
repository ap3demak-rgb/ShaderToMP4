<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

[English](CHANGELOG.md) · **Français**

# Journal des modifications

Format : une section par version, la plus récente en premier. La section d'une version étiquetée est reprise dans les notes de version.

## Non publié

### Ajouts

- Paramètres animés : `// uniform float vitesse = 1.0 [0..5]` (types float, int, bool, vec2, vec3, vec4, color) déclare un uniform réglable, disponible dans toutes les passes ; valeurs enregistrées par shader dans `params/`, images clés `// key vitesse=0:1.0 ; 4:3.0` (linéaire, lissée ou constante), `--param=nom=valeur` en ligne de commande, et paramètres qui suivent le son (`audio.level`, `audio.bass`, `audio.mid`, `audio.treble`).
- Fenêtre « Paramètres et post-traitement » sous Windows (bouton de l'en-tête) et les mêmes contrôles en bas de la carte des réglages sous Linux et macOS ; l'aperçu en lecture reprend les valeurs tout de suite.
- Post-traitement en virgule flottante, avant la conversion en 8 / 10 bits (donc avec le supersampling, le flou de mouvement et la sortie 10 bits) : une passe `nom.post.glsl` qui reçoit l'image dans `iChannel0`, huit effets (`sharpen`, `chroma`, `bloom`, `contrast`, `saturation`, `hue`, `vignette`, `grain`, `// post=bloom:0.4,grain`) et une LUT couleur au format `.cube`, 1D ou 3D avec interpolation tétraédrique (`// lut=etalonnage.cube`).

## 1.8.0 — 2026-10-12

### Ajouts

- `iMouse` animé : trajectoire par images clés `// mouse=0:0.2,0.2 ; 5:0.8,0.6,down` (interpolation linéaire, la forme fixe `// mouse=x,y[,down]` reste valable) ; `iMouse.zw` suit Shadertoy (position du dernier clic, `w` > 0 seulement sur l'image du clic). Valable pour les rendus, l'aperçu et toutes les passes.
- `iDate` animé : `// date=2026-03-14T15:09:26+rate=60` règle les secondes de date par seconde de vidéo (1 par défaut, 0 la fige) ; le jour, le mois et l'année changent maintenant à minuit.
- Canal clavier simulé : `// iChannel0=keyboard` donne la texture de touches 256×3 de Shadertoy (enfoncée, pressée, bascule), avec des touches pressées à des instants donnés par `// key=space:2-4.5 ; left:1-3` (le vrai clavier n'est jamais lu). Les entrées clavier des shaders Shadertoy importés deviennent ce canal.
- Tampons à taille propre : `// bufferA.scale=0.5` ou `// bufferA.size=512x512` (aussi pour B, C, D) ; la passe la voit dans `iResolution`, ceux qui la lisent dans `iChannelResolution`.
- Les shaders `mainVR` (VR) sont rendus en vidéos 360° équirectangulaires, mono ou stéréo haut / bas (`// vr=mono|stereo`, `vripd`), avec les métadonnées de vidéo sphérique ajoutées aux fichiers MP4 et MOV pour que YouTube et les lecteurs VR les traitent comme du 360°.
- Rapport de compatibilité : liste les canaux réglés mais jamais lus et les passes que rien ne lit (elles s'exécutent pour rien).
- Fichiers de volume Shadertoy : `// iChannel0=volume:fichier.bin` lit le format de volume 3D (8 bits ou flottants, 1 à 4 canaux).
- `warmup=N` (commentaire du shader ou config.ini) : simule N images avant la première pour que les tampons à rétroaction se stabilisent ; `iFrame` démarre à N. Vaut aussi pour le rendu par parties, la reprise et l'aperçu.
- `iChannelTime` vaut maintenant le temps du rendu pour les canaux audio et vidéo (il valait toujours 0).

### Modifications

- Documentation : la FAQ et le tableau de compatibilité Shadertoy des README (anglais et français) décrivent maintenant ce qui est pris en charge (canaux, passes, passe Sound, import JSON).
- Rapport de compatibilité : les messages sur les cubemaps et les textures 3D indiquent qu'aucun canal n'est réglé, au lieu de « pas encore pris en charge ».

## 1.7.0 — 2026-10-12

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
