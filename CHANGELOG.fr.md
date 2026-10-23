<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

[English](CHANGELOG.md) · **Français**

# Journal des modifications

Format : une section par version, la plus récente en premier. La section d'une version étiquetée est reprise dans les notes de version.

## Non publié

### Modifié

- **Linux et macOS sont gelés** (2026-10-22). La version 2.1.1 est la dernière publiée avec des archives Linux (x86-64, ARM64) et macOS, qui restent disponibles sous GPL-3.0-or-later. Leur code reste dans le dépôt, marqué figé : pas de correctif, pas de nouveau test, pas d'intégration continue ; les nouvelles fonctions visent Windows seulement. L'intégration continue ne compile, ne teste et n'empaquette plus que Windows, et la publication porte l'archive et l'installeur Windows.

### Ajouté

- **Aide intégrée** (`F1`, bouton `?`) : rubriques de chaque fenêtre, glossaire et page de dépannage, dans les neuf langues ; `F1` dans les fenêtres Sortie, Paramètres, Éditeur et Exemples ouvre la rubrique de cette fenêtre. Les info-bulles de douze champs se terminent par un exemple de valeur.
- **Galerie d'exemples** : soixante shaders originaux en dix catégories (`examples/gallery`), chacun avec une vignette et une explication en français et en anglais ; le bouton **Exemples** les ouvre, un cas d'auto-test les rend tous.
- **Visite guidée** au premier lancement (cinq étapes, jamais bloquante ; `tour=0` dans `window.ini`) ; on peut la revoir depuis l'aide.
- **Explication des erreurs** : les erreurs de compilation GLSL courantes (formulations NVIDIA, Mesa, AMD) reçoivent une phrase d'aide et une correction proposée sous la ligne `fichier:ligne:` du journal ; le shader n'est jamais modifié. Le rapport de NaN du contrôle qualité reçoit une piste.
- Édition Pro : **scènes 3D** (voir « Scènes 3D (édition Pro) » dans le README) : maillages OBJ, PLY et glTF 2.0 lus par le programme, étage des sommets `nom.vert.glsl` devant le même fragment shader, caméra (fixe, orbite, trajectoire), transformation de l'objet au cours du temps, tampon de profondeur 24 bits ou flottant, instances, carte d'ombre `nom.shadow.glsl`, cubemap de réflexion dynamique `envmap=`, passe de calcul GPU `nom.compute.glsl` (OpenGL 4.3, état rejoué pour que les parties et les reprises donnent la même vidéo), aperçu en temps réel avec orbite et zoom à la souris, erreurs localisées en `fichier:ligne:`, cinq scènes d'exemple, dix-neuf cas d'auto-test.
- Points d'accroche de l'édition Pro dans le moteur libre, inertes dans l'édition libre : réglages Pro génériques (`Settings::pro`), interface de scène (`proext.h`), fonctions OpenGL de maillage et de calcul, étage des sommets dans le programme principal, onglets Vertex / Shadow / Compute de l'éditeur, export HTML qui refuse une scène. Le contexte OpenGL est maintenant demandé en 4.3 core d'abord, puis en 3.3 core.
- Infrastructure de l'édition Pro (rien n'est verrouillé dans l'édition libre) : fichier de licence signé `ShaderToMP4.license` à côté du programme, vérifié hors ligne (Ed25519, écrit pour le programme, comparé aux vecteurs de la RFC 8032) ; filigrane d'essai sur la couleur d'une sortie qui utilise une fonction Pro sans licence valide (jamais sur les passes de données), aucun sinon ; état de la licence dans « À propos », bouton et fenêtre « Acheter Pro » (5 €, 10 € ou 15 € par PayPal, ouverts sur clic seulement) qui disparaissent avec une licence valide ; `--license-info` ; contrat d'utilisation `EULA.fr.md` ; quatre cas d'auto-test.

## 2.1.1 — 2026-10-20

### Ajouté

- Mode effet : `source=` (vidéo, image, séquence d'images ou dossier) devient `iChannel0` ; la taille, la cadence et la durée de l'image la suivent (`sourcematch`), `iTime` / `iFrame` sont ceux de la source, son son est gardé (`sourceaudio`), ses sous-titres et chapitres sont recopiés (MP4, MOV, MKV). Les valeurs écrites dans le shader ou en ligne de commande l'emportent.
- Mode effet : plage de la source (`start`, `duration`), saut ou répétition d'images sans interpolation (`fps`), `reverse=1` (image et son), images fixes tenues pendant tout le rendu, séquences d'images (`sourcefps`).
- Dossier de sources : un rendu par fichier, fichiers terminés sautés quand on recommence, un échec n'arrête pas les autres, compte rendu `_batch.json`, jeton `{source}` pour le nom de fichier.
- Transitions : `source2=` et `iProgress` (0 à 1) avec `transitionduration` et `transitionoffset` ; les sons sont enchaînés en fondu ; 25 shaders dans le nouveau dossier `transitions/`.
- Shaders d'exemple de clé chroma, de clé de luminance et de clé par différence avec une sortie alpha (`examples/effects/`).
- Page « Source (mode effet) » de la fenêtre Sortie (Windows) et section de la carte des réglages (Linux et macOS) ; sept nouveaux cas d'autotest ; les paquets et l'installeur contiennent `transitions/` et `examples/effects/`.
- OpenEXR multicouche écrit par le programme lui-même, sans bibliothèque : couleur HDR en virgule flottante, passes de sortie déclarées dans le shader (`// aov depth=bufferB.r`, `normal`, `id`, `alpha`, `motion`), compression ZIP / ZIPS / aucune, demi-flottant ou flottant 32 bits, en-tête avec le shader, l'auteur, la licence et le mode d'alpha ; lecteur du programme pour l'autotest ; `--exr-info`.
- Flou de mouvement vectoriel de la sortie EXR (`vectorblur`), le long de la passe de mouvement estimée ou calculée par le shader.
- Vecteurs de mouvement estimés entre deux images consécutives (correspondance de blocs, présentés comme une estimation) ; bornes de profondeur `depthrange` déclarées dans le fichier ; carte de profondeur 16 bits `--depthmap`.
- Alpha droit ou prémultiplié (`alphamode`) pour l'EXR, le PNG 16 bits, le TIFF et le DPX ; séquences TIFF flottantes 32 bits (`tifffloat=1`) ; séquences DPX (`container=dpx`, 10 bits, BT.709 et cadence dans l'en-tête) ; page « Compositing (EXR) » de la fenêtre Sortie et section de la carte des réglages ; huit nouveaux cas d'autotest.

### Corrigé

- Normalisation du son (`loudnorm`) : le volume de la musique ne monte plus ni ne descend pendant la vidéo. Le filtre `loudnorm` de ffmpeg en une passe changeait son gain au fil du temps ; le son est maintenant mesuré d'abord, puis un gain constant est appliqué (rendus simples et montages). Un limiteur de crête n'intervient que si les crêtes dépassaient -1,5 dBTP. Nouveau cas d'auto-test.

## 2.1.0 — 2026-10-16

### Ajouté

- Installeur Windows (`ShaderToMP4-<version>-win64-setup.exe`, NSIS), en plus de l'archive portable, qui reste la référence. Par utilisateur, sans droits d'administrateur (Program Files refusé), avec `ffmpeg.exe` 9.0.2 (build GPL, épinglé et SHA-256 vérifié, jamais nonfree), interface dans les neuf langues, raccourci du menu Démarrer et du bureau facultatif, « Ouvrir avec » facultatif pour `.glsl` et `.montage`, entrée dans Applications et fonctionnalités, mise à jour sur place qui garde les réglages, refus d'une version plus ancienne, `ffmpeg.exe` de l'utilisateur jamais remplacé, désinstallation qui garde les réglages sauf demande (`/DELETE_SETTINGS`). `scripts/package-installer.ps1` le fabrique, `scripts/verify-installer.ps1` l'essaie. Non signé.

## 2.0.0 — 2026-10-16

### Ajouts

- Boucles sans couture sans toucher au shader : `loopmode=crossfade` (le début de la vidéo se mélange avec le shader une durée plus loin, `loopfade`) et `loopmode=pingpong` (le temps monte puis revient) ; contrôle de la couture après le rendu (dernière image contre première, dans le journal et le rapport de rendu).
- Courbe de temps `timemap=0:0;4:1;8:5` (lisse, avec paliers et marche arrière) et `speed` constante : vrai ralenti, chaque image calculée à son instant exact du shader, flou de mouvement qui suit la vitesse ; le son n'est pas modifié.
- Recherche de la période d'une boucle : bouton **Chercher la boucle** et `--find-loop[=secondes]` (sans rendu), la période remplit le champ **Boucle**.
- Vignette intelligente (menu Images, `--png=best`) : les trois images les plus riches en `_thumb.png`, `_thumb2.png`, `_thumb3.png`.
- Image fixe moyennée : `samples=N` (`--png --samples=N`) moyenne N échantillons indépendants d'un instant en virgule flottante ; une annulation garde les échantillons déjà calculés.
- Section « Temps et boucle » de la fenêtre Sortie (Windows) et de la carte des réglages (Linux et macOS) ; sept nouveaux cas d'auto-test.
- Contrôle qualité avant publication (`--check`, menu Images, `precheck=1`) : flashs et flashs de rouge saturé (indicateur de type Harding), valeurs invalides avec les lignes suspectes et une carte des pixels, images noires / blanches / brûlées, scintillement, aliasing, précision aux grands temps ; rapport à côté de la vidéo, code de sortie 6.
- Contrôle qualité : motifs réguliers de bandes, et simulation en demi-précision (`iTime` et `iResolution` arrondis, image comparée à l'image exacte) ; l'estimation du menu Images couvre toute la file et en donne le total.
- `--compare a b` (écart moyen et maximal, PSNR, image de différence, vidéo de comparaison ; code de sortie 7), images de référence de ses propres shaders (`--golden=record|verify`), comparaison avec la version avant l'édition dans l'éditeur avec un rideau à glisser dans l'aperçu.
- Estimation du temps de rendu (menu Images, `--dry-run`, file entière) : mesurée sur quelques secondes de vrai rendu avec ses bornes, et alerte de lenteur qui propose de baisser le sur-échantillonnage, les sous-images du flou ou la taille.
- Affiches par tuiles avec des passes Buffer / Cubemap (tampons calculés une fois à pleine taille, jusqu'à 8192 × 8192 pixels) ; `stillformat=tiff` (8 / 16 bits, aussi par tuiles) ; images fixes PNG étiquetées sRGB ; la carte du coût s'affiche aussi dans l'aperçu ; temps GPU de chaque passe (Buffer A à D, Cubemap A) dans le rapport et moyennes en cours dans la barre d'état ; `vr=fulldome` pour un shader `mainImage` ordinaire (domemaster).
- Passes Buffer / Cubemap avec un temps qui recule (aller-retour, courbe de temps descendante) : l'état est rejoué depuis le début de la plage à chaque image (coût limité à 400000 pas de simulation, annoncé dans le journal) ; `audiofollow=1` fait suivre une vitesse constante au son (`atempo`, hauteur conservée) ; la fenêtre Sortie a une troisième page, « Temps et boucle », avec un graphique en direct de la courbe de temps ; les trois vignettes intelligentes s'ouvrent ensemble.
- Affiche par tuiles : une image fixe de plus de 8192 pixels (ou de plus que ce que le GPU permet, jusqu'à 65536) est calculée tuile par tuile avec le `iResolution` complet et un `fragCoord` décalé, identique à un rendu d'un seul tenant ; bandes PNG gardées pour reprendre, sur-échantillonnage, 16 bits (`depth=10`), `samples`, `printdpi=300` ; `tile=` pour choisir la taille des tuiles.
- Carte du coût GPU (`--heatmap[=16x9]`, menu Images) : l'image teintée du bleu au rouge selon le temps GPU de chaque zone, avec les chiffres dans le journal ; temps GPU moyen par image de l'image, des tampons et du post-traitement dans le journal et dans le rapport de rendu (`gpu_ms`).
- Projections VR pour `mainVR` : `vr=sbs` (côte à côte), `vr=anaglyph` (rouge-cyan), `vr=fulldome` (carré fisheye 180°), métadonnées sphériques des modes stéréo ; `--cubefaces[=taille]` écrit les six faces de cubemap à l'endroit.
- Destinations de publication (`target=tiktok`, `youtube-shorts`, `instagram-reels`, `discord-8mb`… et les vôtres dans `presets/targets.ini`) : taille, cadence, codec et poids de la plate-forme, avec un avertissement quand la durée ou le shader dépasse ; poids cible du fichier (`targetsize=8MB`, deux passes, corrigé jusqu'à ce que le fichier tienne) ; recadrage centré (`crop=9:16`).
- Cache des maîtres (`mastercache=1`) : un seul rendu sans perte, chaque format en est tiré par ffmpeg sans le GPU, borné par `cachemax` Go, vidé par `--cache-purge` ou un bouton.
- Vidéo reproductible (`embed=1`, `--reproduce`), licence et crédit lus dans l'en-tête du shader (`autocredit=1`, avertissement pour les licences non commerciales), contrôle d'intégrité (`--verify`, menu Images) contre la somme SHA-256 écrite dans le rapport de rendu, aperçu animé GIF / WebP (`animpreview`).
- Page HTML autonome (WebGL 2) à partir d'un shader et de ses passes (`--export-html`, menu Images), comparée au rendu du programme dans un navigateur sans fenêtre.
- Page « Publication » de la fenêtre Sortie (Windows) et section de la carte des réglages (Linux et macOS) ; huit nouveaux cas d'auto-test.
- Son et rythme : le fichier son est analysé (tempo, temps, mesures, attaques par bande, énergie lissée, sections) et gardé dans `<son>.beats.json` ; tout shader lit `iBeat`, `iBeatCount`, `iBPM`, `iBass`, `iMid`, `iTreble`, `iEnergy` et `iOnset` sans les déclarer, échantillonnés à l'instant exact de chaque image ; les paramètres les suivent par une expression (`// uniform float k = iBass*0.5+0.2 attack=0.02 release=0.3`, `--param=`, avec `threshold`) ; `bpm` impose le tempo, `beatsperbar` règle les mesures ; `--beats[=list|clear]`.
- Repères (`markers=chapters|file|all`) : les sections en chapitres de la vidéo, chaque temps, mesure et section dans `.markers.txt` et `.markers.csv` ; montage dont les coupes tombent sur les temps, les mesures ou les sections de la musique (`beatsync`).
- Éditeur de temps (**Son et rythme…** sur la page Publication de la fenêtre Sortie, et dans la carte des réglages sous Linux et macOS) : la forme d'onde avec les temps, mesures et sections détectés, un clic supprime ou ajoute un temps ; sept nouveaux cas d'auto-test (piste de clics à 120 BPM).
- Parties rendues en même temps (`parallel=N`) : N contextes OpenGL rendent ensemble les parties d'une vidéo, mêmes images que le rendu des parties l'une après l'autre ; un contexte qui tombe en panne est retiré avec un message et son travail passe aux autres ; une partie lente est aussi confiée à un contexte libre (le premier arrivé gagne) ; en série, avec un message, pour les shaders à passes à historique et sous Linux et macOS. `--benchmark --parallel` mesure le gain (faible sur le PC d'essai).
- Ferme de rendu sans serveur (`farm=dossier`, `--farm=`) : plusieurs ordinateurs rendent les parties d'une vidéo sur un dossier partagé, chaque partie réservée par un fichier de verrou à création atomique, verrous d'ordinateurs éteints repris après `farmtimeout`, parties terminées jamais refaites, assemblage par le premier ordinateur qui voit toutes les parties ; un autre travail, une autre version, un autre ffmpeg ou encodeur est refusé avec un message précis ; `--farm-status dossier` et `farm-report.json`. Aucune connexion réseau n'est ouverte.

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
