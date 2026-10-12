<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

[English](CHANGELOG.md) · **Français**

# Journal des modifications

Format : une section par version, la plus récente en premier. La section d'une version étiquetée est reprise dans les notes de version.

## Non publié

### Ajouts

- Boucles sans couture sans toucher au shader : `loopmode=crossfade` (le début de la vidéo se mélange avec le shader une durée plus loin, `loopfade`) et `loopmode=pingpong` (le temps monte puis revient) ; contrôle de la couture après le rendu (dernière image contre première, dans le journal et le rapport de rendu).
- Courbe de temps `timemap=0:0;4:1;8:5` (lisse, avec paliers et marche arrière) et `speed` constante : vrai ralenti, chaque image calculée à son instant exact du shader, flou de mouvement qui suit la vitesse ; le son n'est pas modifié.
- Recherche de la période d'une boucle : bouton **Chercher la boucle** et `--find-loop[=secondes]` (sans rendu), la période remplit le champ **Boucle**.
- Vignette intelligente (menu Images, `--png=best`) : les trois images les plus riches en `_thumb.png`, `_thumb2.png`, `_thumb3.png`.
- Image fixe moyennée : `samples=N` (`--png --samples=N`) moyenne N échantillons indépendants d'un instant en virgule flottante ; une annulation garde les échantillons déjà calculés.
- Section « Temps et boucle » de la fenêtre Sortie (Windows) et de la carte des réglages (Linux et macOS) ; sept nouveaux cas d'auto-test.
- Affiches par tuiles avec des passes Buffer / Cubemap (tampons calculés une fois à pleine taille, jusqu'à 8192 × 8192 pixels) ; `stillformat=tiff` (8 / 16 bits, aussi par tuiles) ; images fixes PNG étiquetées sRGB ; la carte du coût s'affiche aussi dans l'aperçu ; temps GPU de chaque passe (Buffer A à D, Cubemap A) dans le rapport et moyennes en cours dans la barre d'état ; `vr=fulldome` pour un shader `mainImage` ordinaire (domemaster).
- Passes Buffer / Cubemap avec un temps qui recule (aller-retour, courbe de temps descendante) : l'état est rejoué depuis le début de la plage à chaque image (coût limité à 400000 pas de simulation, annoncé dans le journal) ; `audiofollow=1` fait suivre une vitesse constante au son (`atempo`, hauteur conservée) ; la fenêtre Sortie a une troisième page, « Temps et boucle », avec un graphique en direct de la courbe de temps ; les trois vignettes intelligentes s'ouvrent ensemble.
- Affiche par tuiles : une image fixe de plus de 8192 pixels (ou de plus que ce que le GPU permet, jusqu'à 65536) est calculée tuile par tuile avec le `iResolution` complet et un `fragCoord` décalé, identique à un rendu d'un seul tenant ; bandes PNG gardées pour reprendre, sur-échantillonnage, 16 bits (`depth=10`), `samples`, `printdpi=300` ; `tile=` pour choisir la taille des tuiles.
- Carte du coût GPU (`--heatmap[=16x9]`, menu Images) : l'image teintée du bleu au rouge selon le temps GPU de chaque zone, avec les chiffres dans le journal ; temps GPU moyen par image de l'image, des tampons et du post-traitement dans le journal et dans le rapport de rendu (`gpu_ms`).
- Projections VR pour `mainVR` : `vr=sbs` (côte à côte), `vr=anaglyph` (rouge-cyan), `vr=fulldome` (carré fisheye 180°), métadonnées sphériques des modes stéréo ; `--cubefaces[=taille]` écrit les six faces de cubemap à l'endroit.

## 1.9.0 — 2026-10-12

### Ajouts

- Transparence (`alpha=1`) : le canal alpha du shader en ProRes 4444, VP9 WebM, WebP et séquences PNG / TIFF / EXR (alpha droit, exact avec le supersampling et le flou de mouvement, damier dans les aperçus, autres combinaisons refusées avant le rendu).
- Sortie HDR (`hdr=pq` pour le HDR10, `hdr=hlg`) : BT.2020, 10 bits, conversion en virgule flottante, MaxCLL / MaxFALL et volume d'affichage déclarés (`hdrpeak`, `hdrfall`, `hdrwhite`), en H.265, AV1 et VP9.
- Taille et cadence de sortie : `scale` avec choix du filtre (`scalefilter`), interpolation d'images `interp` (rendu à 30, écriture à 60), normalisation du son `loudnorm` en LUFS.
- Texte sur la vidéo : `title`, `credit`, `titledur` avec drawtext de ffmpeg ; le journal d'un import Shadertoy donne la ligne de crédit à ajouter.
- Montage : un fichier `nom.montage` enchaîne plusieurs shaders avec des transitions coupe, fondu, fondu enchaîné ou balayage (clips rendus par parties reprenables, puis joints par ffmpeg).
- Ligne de commande : `--sheet=4x3` (planche-contact) et `--thumb[=secondes]` (vignette de couverture).
- Éditeur du shader sélectionné et de ses passes (Buffer A–D, Common, Cubemap A, Sound, Post) : onglets, création en un clic d'une passe absente, numéros de ligne, coloration GLSL sous Windows, erreurs du compilateur marquées d'après le journal, `Ctrl+S` écrit le fichier et recharge l'aperçu.
- Réglages d'un seul shader (« Ce shader seul », gardés dans `jobs/`, éventuellement écrits dans le shader en commentaires `// clé=valeur`) ; champs grisés avec la valeur du shader quand un commentaire `// clé=valeur` impose le réglage.
- File d'attente : monter / descendre / passer en tête (`Alt+Haut` / `Alt+Bas`), rendre seulement la sélection, remettre en attente, menu du clic droit dans les deux interfaces.
- Déposer une image, un son ou une vidéo sur la fenêtre écrit `// iChannelN=…` (ou la bande-son) dans le shader sélectionné.
- Aperçu : numéro de l'image et cadence réelle pendant la lecture ; image PNG au temps de l'aperçu.
- Page « Avancé » de la fenêtre Sortie : modèle du nom de fichier, règle du fichier existant, métadonnées, action de fin de lot, parties, file de lecture GPU, obturateur du flou.
- Rapport de rendu (`report=json|text`) : durée, cadence, goulot, taille et avertissements à côté de chaque vidéo.
- Ligne de commande : `--watch` regarde les sous-dossiers avec un `profile.ini` par dossier, `--watch-move` (done/ et failed/), `--log=fichier` ; `--benchmark --compare=precedent.json --max-drop=N` (code de sortie 5) ; l'intégration continue compare le benchmark au passage précédent.
- Linux : rendu sans fenêtre par EGL quand il n'y a pas de serveur d'affichage (ferme de rendu, conteneur, CI), avec une étape de CI qui y passe l'auto-test.
- Accessibilité vérifiée avec NVDA : noms, rôles et valeurs sont maintenant aussi donnés par MSAA (boutons à icône seule, champs largeur et FPS, liste des shaders, curseurs avec leur valeur, barre de progression) ; l'éditeur change d'onglet avec `Ctrl+Tab` et `Ctrl+1..9` ; les fenêtres déplacées sur un moniteur d'une autre échelle sont remises en page et gardées dans l'écran.
- Interfaces en néerlandais, japonais et chinois simplifié (`lang/nl.json`, `ja.json`, `zh.json`) ; sous Linux et macOS une police CJK du système est chargée pour le japonais et le chinois.
- Lots : numéroter des shaders de 1 à 4 et rendre un lot ; les réglages d'un seul shader couvrent maintenant la fenêtre Sortie et les variantes ; coloration GLSL dans l'éditeur Linux / macOS ; noms accessibles pour les boutons à icône et les nouveaux champs ; l'éditeur suit un changement d'échelle d'écran.
- Fenêtre « Sortie » (Windows, bouton de l'en-tête) et section correspondante de la carte des réglages (Linux et macOS) pour la transparence, le HDR, la taille et le filtre de sortie, l'interpolation, le volume sonore, le titre et le crédit ; le bouton « Image PNG » devient « Images » : image PNG, vignette de couverture ou planche-contact (3×2, 4×3, 5×4, 6×5).

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
