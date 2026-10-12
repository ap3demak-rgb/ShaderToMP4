<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

<p align="center">
  <img src="docs/banner.svg" alt="ShaderToMP4 : transforme un shader Shadertoy en vidéo, image par image" width="100%">
</p>

<p align="center"><a href="README.md">English</a> · <b>Français</b></p>

<p align="center">
  <a href="https://github.com/ap3demak-rgb/ShaderToMP4/releases/latest"><img alt="Télécharger" src="https://img.shields.io/badge/T%C3%A9l%C3%A9charger-Windows%20%C2%B7%20Linux%20%C2%B7%20macOS-465AFF?style=for-the-badge"></a>
  &nbsp;
  <a href="https://ap3demak-rgb.github.io/ShaderToMP4"><img alt="Site web" src="https://img.shields.io/badge/Site%20web-ShaderToMP4-FF5A96?style=for-the-badge"></a>
</p>

<p align="center">
  <img alt="Version" src="https://img.shields.io/badge/version-1.8.0-FF5A96?style=flat-square">
  <img alt="Plateforme" src="https://img.shields.io/badge/plateforme-Windows%20%C2%B7%20Linux%20%C2%B7%20macOS-0078D4?style=flat-square">
  <img alt="Portable" src="https://img.shields.io/badge/portable-sans%20installeur-3DDC97?style=flat-square">
  <img alt="Langues" src="https://img.shields.io/badge/langues-6-7C6CFF?style=flat-square">
  <img alt="C++" src="https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white">
  <a href="LICENSE"><img alt="Licence" src="https://img.shields.io/badge/licence-GPL--3.0--or--later-3DDC97?style=flat-square"></a>
</p>

<p align="center">
  <img src="docs/screenshots/demo.gif" alt="Rendu d'un shader plasma" width="520">
</p>

**ShaderToMP4** convertit un shader [Shadertoy](https://www.shadertoy.com) (onglet *Image*, avec ses passes Buffer A–D, Common, Cubemap A et Sound) en vidéo, **image par image**, avec un temps exact (`iTime = image / fps`). Pas de capture d'écran, donc pas de saccades, pas de perte, quelle que soit la puissance de ta carte graphique : un shader qui tourne à 5 images par seconde en direct donne une vidéo parfaitement fluide.

Un seul programme portable pour **Windows, Linux et macOS**, **sans installeur**, avec une interface sombre moderne en français et en anglais.

---

## Pourquoi ShaderToMP4 ?

| | |
|---|---|
| **Temps exact** | Chaque image est rendue au temps `n / fps`, puis envoyée directement à ffmpeg par un tube : aucun fichier intermédiaire. |
| **Qualité maximale** | Jusqu'à 16384 pixels de large, supersampling ×4, flou de mouvement (×4, ×8, ×16), conversion de couleur BT.709 correcte, sortie 10 bits, x264 / x265 sans perte si vous le voulez. |
| **Nombreux formats** | MP4, MOV, MKV, WebM (H.264, H.265, VP9, AV1), ProRes et DNxHR pour le montage, GIF et WebP animés, séquences d'images PNG / TIFF / EXR. |
| **Fonctions Shadertoy** | Textures, cubemaps, textures 3D, canaux audio et vidéo (`iChannel`), shaders à passes multiples (Buffer A–D, Common), souris et date fixes, GLSL de style WebGL 2, et rapport de compatibilité avant chaque rendu. |
| **Rapide** | Rendu et encodage en parallèle, lecture GPU asynchrone, encodage matériel (NVENC, AMF, QuickSync, VAAPI, VideoToolbox) en option. |
| **Fiable** | Les longs rendus se font par parties : si quelque chose plante, tu reprends exactement où tu t'étais arrêté. |
| **Pratique** | Aperçu intégré, préréglages (YouTube, 4K, vertical 9:16), audio avec fondus, boucle parfaite, file d'attente de plusieurs shaders, pause et annulation. |
| **Portable** | Rien à installer, quel que soit le système. Réglages, langues et `ffmpeg` restent dans le dossier de l'application. |

## L'interface

<p align="center">
  <img src="docs/screenshots/fr/done.png" alt="Interface de ShaderToMP4 après un rendu" width="860">
</p>

<table>
  <tr>
    <td width="50%"><img src="docs/screenshots/fr/empty.png" alt="Zone de dépôt illustrée"></td>
    <td width="50%"><img src="docs/screenshots/fr/running.png" alt="Rendu en cours"></td>
  </tr>
  <tr>
    <td align="center"><sub><b>Glisse tes shaders</b> dans la zone de dépôt, ou clique sur « Ajouter ».</sub></td>
    <td align="center"><sub><b>Rendu en cours</b> : vitesse, temps restant, pause et annulation. La progression s'affiche aussi dans la barre des tâches.</sub></td>
  </tr>
</table>

<p align="center">
  <img src="docs/screenshots/fr/settings.png" alt="Réglages vidéo" width="860"><br>
  <sub>Tous les réglages vidéo au même endroit, avec des <b>préréglages en un clic</b> (Brouillon, YouTube, 4K 60, Vertical) et une info-bulle sur chaque champ.</sub>
</p>

<p align="center">
  <img src="docs/screenshots/fr/list-preview.png" alt="Liste des shaders et aperçu" width="860"><br>
  <sub>La file d'attente avec le statut de chaque shader, et un <b>aperçu</b> à l'instant de ton choix, sans lancer ffmpeg.</sub>
</p>

<p align="center">
  <img src="docs/screenshots/fr/about.png" alt="Fenêtre À propos" width="420"><br>
  <sub>La fenêtre « À propos » (<code>F1</code>) : version, copyright, licence, contact et site web. Le bouton globe change la langue instantanément.</sub>
</p>

### Sous Linux et macOS

Windows garde son interface native Direct2D. Linux et macOS utilisent une seconde interface au même aspect (cartes sombres, accent en dégradé, mêmes icônes SVG), fondée sur GLFW et Dear ImGui et dessinée en OpenGL ; les deux interfaces partagent le même moteur de rendu, les mêmes réglages et les mêmes traductions.

<p align="center">
  <img src="docs/screenshots/linux/fr/main.png" alt="ShaderToMP4 sous Linux" width="860"><br>
  <sub>L'interface Linux / macOS (capture produite par l'intégration continue sous Linux).</sub>
</p>

## Exemples

Le dossier [`examples`](examples) contient quatre shaders prêts à convertir : plasma, anneaux, tunnel et aurore.

<p align="center">
  <img src="docs/screenshots/gallery.png" alt="Galerie : plasma, anneaux, tunnel, aurore" width="860">
</p>

## Démarrage rapide

1. **Télécharge** l'archive portable de ton système depuis la page [Releases](https://github.com/ap3demak-rgb/ShaderToMP4/releases/latest) et décompresse-la où tu veux (ou [compile le projet](#compiler)).

   | Système | Archive | Lancer |
   |---|---|---|
   | Windows 10 / 11, 64 bits | `ShaderToMP4-<version>-win64.zip` | `ShaderToMP4.exe` |
   | Linux x86-64 | `ShaderToMP4-<version>-linux-x86_64.tar.gz` | `./ShaderToMP4` |
   | Linux ARM64 | `ShaderToMP4-<version>-linux-aarch64.tar.gz` | `./ShaderToMP4` |
   | macOS 10.13+, Apple Silicon et Intel | `ShaderToMP4-<version>-macos-universal.zip` | `ShaderToMP4.app` |

2. Si `ffmpeg` est absent, le programme propose de le télécharger (Windows : environ 80 Mo depuis [gyan.dev](https://www.gyan.dev/ffmpeg/builds/) ; Linux : un build statique de [BtbN](https://github.com/BtbN/FFmpeg-Builds) ; vérifié par SHA-256) et l'installe à côté de lui. Sous macOS, installe-le avec Homebrew (`brew install ffmpeg`) ou place le binaire `ffmpeg` à côté du `.app` : aucune source ne publie d'empreinte SHA-256 vérifiable pour macOS, il n'y a donc pas de téléchargement automatique.
3. **Glisse** un ou plusieurs fichiers shader (`.glsl` ou `.txt`) dans la fenêtre, ou clique sur **Ajouter**.
4. Règle la vidéo (ou choisis un préréglage) puis clique sur **Démarrer**.
5. La vidéo est créée à côté du shader, ou dans le dossier choisi dans **Sortie**.

> Les fichiers passés en ligne de commande (ou glissés sur l'exécutable sous Windows) sont ajoutés et rendus tout de suite.

**Prérequis** : une carte graphique (ou un pilote) compatible OpenGL 3.3, et
- **Windows** : Windows 10 (version 1703 ou plus récente) ou Windows 11.
- **Linux** : un bureau X11 ou Wayland (XWayland), glibc 2.35 ou plus récente, le pilote OpenGL de ta carte graphique (Mesa convient), et `curl` ainsi que `tar` avec `xz` pour le téléchargement automatique de ffmpeg. Sur un portable à deux GPU, mets `gpu=high` dans `config.ini` (ou `--gpu=high` avec `--render`) pour utiliser le GPU dédié, `gpu=low` pour l'intégré (le programme fixe les variables PRIME de Mesa et du pilote NVIDIA avant d'ouvrir OpenGL ; relance-le après un changement ; `--diagnostic` montre le GPU utilisé).
- **macOS** : 10.13 ou plus récent. L'archive est signée « ad hoc », non notariée : au premier lancement, clic droit sur `ShaderToMP4.app` puis *Ouvrir* (ou `xattr -dr com.apple.quarantine ShaderToMP4.app`). Les réglages sont écrits dans le dossier qui contient le `.app` : garde-le dans un dossier où tu peux écrire (pas `/Applications`).

## Fonctions

<details>
<summary><b>Les boutons et les raccourcis</b></summary>

| Bouton | Raccourci | Rôle |
|---|---|---|
| **Ajouter** | `Ctrl+O` | Ajouter des shaders à la file |
| **Démarrer** | `F5` | Rend tous les fichiers de la liste qui ne sont pas déjà terminés |
| **Pause** / **Reprendre** | `F6` | Suspend le rendu (le temps de pause n'est pas compté dans la vitesse) |
| **Annuler** | `Échap` | Arrête le rendu ; les parties déjà terminées sont conservées |
| **Image PNG** | | Rend une seule image, au temps « Début », et l'ouvre |
| **Aperçu** et curseur | `Ctrl+P` | Affiche une image du shader à un instant choisi, sans ffmpeg |
| **Lecture** / **Arrêt** | | Joue le shader en temps réel à la taille de l'aperçu (640 px au plus), en boucle sur la plage, saut par le curseur ; l'aperçu se recharge tout seul quand le shader ou un de ses fichiers de passes change sur le disque |
| **Retirer** | `Suppr` (dans la liste) | Retire les shaders sélectionnés |
| **Dossier** | | Ouvre l'explorateur sur la vidéo du shader sélectionné |
| **Profil / Enregistrer** | | Charge ou enregistre tous les réglages dans un fichier `.ini` |
| **Paramètres** (icône à curseurs) | | Paramètres du shader et post-traitement (effets, LUT couleur) ; sous Linux et macOS ils sont en bas de la carte des réglages |
| **À propos** | `F1` | Version, copyright, licence, contact, site web |
| **EN / FR** | | Change la langue de l'interface |

`Tab` passe d'un réglage au suivant. Tous les textes de l'interface ont un contraste d'au moins 4,5:1 avec leur fond (WCAG AA), contrôlé automatiquement pour les deux interfaces.

**File d'attente conservée entre deux sessions** : les shaders pas encore rendus (en attente, en échec, annulés ou interrompus) sont notés dans `queue.txt` à côté du programme et remis dans la liste à la prochaine ouverture sans fichier (le journal l'indique ; appuyez sur Démarrer). Les shaders terminés sont oubliés, tout comme un fichier qui n'existe plus.

</details>

<details>
<summary><b>Tous les réglages</b> (fenêtre, <code>config.ini</code> ou commentaires du shader)</summary>

Les réglages sont enregistrés dans `config.ini` à côté du `.exe`, et peuvent être imposés **par shader** avec un commentaire (prioritaire sur la fenêtre ; le journal le signale, et la carte des réglages affiche « Imposé par le shader : … » quand un tel shader est sélectionné) :

```glsl
// fps=30
// duration=20
// width=3840
// height=2160
void mainImage(out vec4 fragColor, in vec2 fragCoord) { ... }
```

| Réglage (fenêtre) | Clé | Valeurs |
|---|---|---|
| Résolution | `width`, `height` | 16 à 16384 (arrondi au nombre pair) |
| FPS | `fps` | 1 à 240 |
| Durée, Début | `duration`, `start` | secondes (`90`) ou `mm:ss` dans la fenêtre |
| Codec | `codec` | `h264`, `h265`, `vp9`, `av1`, `prores_*`, `dnxhr_*` (voir « Formats de sortie ») |
| Preset | `preset` | `ultrafast` ... `veryslow` |
| Qualité (CRF) | `crf` | 0 (sans perte), 12 (quasi parfait), 18 (très bon) |
| Débit | `bitrate` | kb/s ; 0 = qualité constante (CRF) |
| Conteneur | `container` | `mp4`, `mov`, `mkv`, `webm`, `gif`, `webp`, `png`, `tiff`, `exr` (WebM : VP9/AV1 seulement ; MOV : pas VP9/AV1 ; MP4 : pas ProRes/DNxHR) |
| Bits | `depth` | 8 ou 10 bits par canal (10 : H.265, AV1, VP9, ProRes, PNG/TIFF) |
| Encodeur | `encoder` | `cpu`, `auto`, `nvenc`, `amf`, `qsv` (repli sur le CPU si indisponible) |
| Lissage | `ss` | 1 à 4 : rendu à N× la taille puis réduction (supersampling) |
| Couleur | `color` | `bt709`, `bt709full` (pleine plage), `srgb`, `bt601` |
| Flou | `blur`, `shutter` | 1, 4, 8, 16 sous-images par image ; obturateur (0,5 = 180°) |
| Boucle (s) | `loop` | période du shader : la durée devient un multiple entier, la dernière image précède exactement la première |
| Audio | `audio` | fichier son, calé sur le temps « Début », complété par du silence s'il est plus court |
| Fondu entrée / sortie | `fadein`, `fadeout` | secondes, image et son |
| Sortie | `outdir` | dossier de sortie (vide = à côté du shader) |
| (commentaires du shader) | `iChannel0`…`iChannel3`, `bufferA`…`bufferD` (`.scale`, `.size`), `mouse`, `key`, `date` | textures, souris, clavier et date simulés (voir « Compatibilité Shadertoy ») |
| (commentaires du shader, `--variant=`) | `variant` | rendus supplémentaires du même shader, une ligne par variante : `// variant=720p:width=1280\|height=720\|crf=23`. Le rendu de base passe d'abord, puis chaque variante avec ses réglages ; son nom s'ajoute au nom du fichier (`shader_720p.mp4`) sauf si la variante fixe `name` |
| (config.ini seulement) | `name`, `exists` | modèle du nom de sortie et conduite si le fichier existe (voir « Formats de sortie ») |
| (config.ini seulement) | `gpu` | carte graphique sur un PC à deux GPU, Linux seulement : `default`, `high` (la dédiée) ou `low` (l'intégrée). Windows demande déjà le GPU puissant ; macOS choisit seul |
| (config.ini seulement) | `endaction` | après un lot réussi : `none` (défaut), `folder` (montrer le dernier fichier dans le gestionnaire de fichiers) ou `sleep` (mise en veille après 15 s ; fermer le programme l'annule). À la fin d'un lot le bouton de la barre des tâches / du Dock clignote toujours |
| (config.ini seulement) | `chunk` | rendu par parties de N secondes (30 par défaut, 0 = jamais) |
| (config.ini seulement) | `metadata` | 1 = écrire titre (nom du shader) et commentaire (taille, cadence, version) dans le fichier, 0 = aucun (1 par défaut) |
| (commentaires du shader, config.ini) | `alpha` | `1` conserve le canal alpha du shader (ProRes 4444, VP9 WebM, WebP, PNG / TIFF / EXR) ; voir « Sortie » |
| (commentaires du shader, config.ini) | `hdr`, `hdrpeak`, `hdrfall`, `hdrwhite` | `pq` (HDR10) ou `hlg` ; MaxCLL et MaxFALL déclarés en cd/m² (1000, 400), blanc du shader en cd/m² (203) |
| (commentaires du shader, config.ini) | `scale`, `scalefilter` | taille de sortie (`1920x1080`, `2`, `0.5x`) et son filtre (`lanczos`, `bicubic`, `bilinear`, `area`, `neighbor`) |
| (commentaires du shader, config.ini) | `interp`, `loudnorm` | cadence de sortie par interpolation ; normalisation du son en LUFS (`-16`) |
| (commentaires du shader, config.ini) | `title`, `credit`, `titledur` | texte écrit sur les premières secondes de la vidéo (titre au milieu, crédit en bas à gauche) |
| (commentaires du shader, config.ini) | `post`, `lut` | effets de post-traitement (`post=bloom:0.4,grain,vignette`) et LUT couleur (`lut=etalonnage.cube`) ; voir « Création » |
| (commentaires du shader, `--param=`) | `key nom=…`, `param` | paramètres animés : `// key vitesse=0:1.0 ; 4:3.0`, `--param=vitesse=2.5` ; voir « Création » |
| (commentaires du shader, config.ini) | `vr`, `vripd` | shader `mainVR` en vidéo 360° : `auto` (défaut), `off`, `mono`, `stereo` ; distance entre les yeux du rendu stéréo (voir « Compatibilité Shadertoy ») |
| (commentaires du shader, config.ini) | `warmup` | images simulées avant la première (0 à 3600, 0 par défaut) : les tampons (Buffer A–D, Cubemap A) sont chauffés, et `iFrame` démarre à N (l'image 0 a `iFrame` = N, `iTime` ne change pas). Utile pour les effets à rétroaction qui mettent du temps à se stabiliser ; `iTime` des images de chauffe vaut `start − N/fps`, il peut donc être négatif |
| (config.ini seulement) | `pbo` | profondeur de la file de lecture GPU (2 à 8, 3 par défaut) |

</details>

<details>
<summary><b>Formats de sortie, 10 bits, noms de fichiers et préréglages</b></summary>

| Format | `codec` | `container` | Remarques |
|---|---|---|---|
| H.264, H.265 | `h264`, `h265` | `mp4`, `mov`, `mkv` | encodeurs matériels : NVENC, AMF, QuickSync, VAAPI, VideoToolbox |
| VP9, AV1 | `vp9`, `av1` | `webm`, `mp4`, `mkv` | VP9 sur le CPU (libvpx-vp9) ; AV1 avec SVT-AV1 (libaom en secours), et essayé d'abord sur NVENC / AMF / QuickSync / VAAPI quand un encodeur est choisi (repli sur le CPU) ; le son WebM est en Opus |
| ProRes | `prores_proxy`, `prores_lt`, `prores_422`, `prores_hq`, `prores_4444` | `mov`, `mkv` | format de montage : 10 bits 4:2:2 (4:4:4 pour le 4444), très gros fichiers, CRF et débit ignorés |
| DNxHR | `dnxhr_sq`, `dnxhr_hq`, `dnxhr_hqx`, `dnxhr_444` | `mov`, `mkv` | format de montage, au moins 256×120 |
| GIF animé | (ignoré) | `gif` | palette optimisée sur toute l'animation, pas de son ; 15 à 25 images/s conseillé et une limite de taille protège la mémoire |
| WebP animé | (ignoré) | `webp` | qualité tirée du curseur CRF, pas de son |
| Séquence d'images | (ignoré) | `png`, `tiff`, `exr` | dossier `<nom>_frames/` contenant `<nom>_000000.png`… ; RVB pleine plage, sans conversion YUV ; PNG / TIFF en 16 bits avec 10 bits ; EXR en demi-flottant |

- **Bits** (`depth`, 8 ou 10) : à 10, le shader est rendu en 16 bits par canal et encodé en H.265 Main10, AV1, VP9 profil 2, ProRes / DNxHR (toujours 10 bits) ou PNG / TIFF 16 bits, ce qui supprime les bandes des dégradés doux. Les encodeurs matériels sont en 8 bits seulement : le CPU est alors utilisé (le journal l'indique).
- Un codec ou un conteneur que le ffmpeg installé ne sait pas produire est marqué « absent de ce ffmpeg » dans les listes ; installez une version complète de ffmpeg (le téléchargement automatique en est une). Une combinaison impossible (WebM avec H.264, MOV avec VP9, MP4 avec ProRes) est refusée avec une explication avant le début du rendu.
- **Taille estimée** : affichée sous les réglages (approximative, elle dépend du shader), avec un avertissement si le disque de destination est trop petit.
- **Nom du fichier** (config.ini ou commentaire du shader) : `name` est un modèle, `{shader}` par défaut ; `{w}`, `{h}`, `{fps}`, `{date}` et `{codec}` sont remplacés (`// name={shader}_{w}x{h}`). `exists` décide ce qui se passe si le résultat existe déjà : `overwrite` (par défaut), `number` (`nom-2`, `nom-3`…) ou `skip`.
- **Préréglages** : les pastilles (Brouillon, YouTube, 4K 60, Vertical) et le menu « Plus » (X 720p30, GIF léger, ProRes pour le montage, WebM VP9) ne changent que la taille, la cadence, le codec, le conteneur et la qualité. Vos propres préréglages sont des fichiers `.ini` dans un dossier `presets` à côté du programme : ils apparaissent dans le menu « Plus » (même format qu'un profil enregistré avec « Enregistrer »).

</details>

<details>
<summary><b>Compatibilité Shadertoy</b> (canaux, passes, son, souris, date)</summary>

L'onglet **Image** et les passes **Buffer A–D**, **Common**, **Cubemap A** et **Sound** sont rendus, et `mainVR` (shaders VR) donne une vidéo 360°. Ce qui fonctionne :

| Fonction | État |
|---|---|
| `mainImage`, `iResolution`, `iTime`, `iTimeDelta`, `iFrame`, `iFrameRate`, `iSampleRate` (44100) | pris en charge, exacts à chaque image |
| `iChannel0` à `iChannel3` en textures 2D | pris en charge : `// iChannel0=image.png` (chemin relatif au shader ; PNG, JPG, HDR, EXR… tout ce que ffmpeg décode), `noise` ou `grid` (générés, identiques à chaque exécution) |
| options des textures | `// iChannel0.filter=nearest\|linear\|mipmap` (mipmap par défaut), `.wrap=clamp\|repeat` (repeat par défaut), `.vflip=0\|1` (1 par défaut, comme Shadertoy) |
| `iChannelResolution`, `iChannelTime` | pris en charge : la résolution de chaque canal (256×3 pour le clavier, 512×2 pour l'audio) ; le temps est le temps du rendu `iTime` pour les canaux audio et vidéo, 0 pour les autres |
| `iMouse` | simulé et déterministe. Fixe : `// mouse=0.5,0.5` (fractions de la taille, origine en bas à gauche), `// mouse=0.5,0.5,down` pour un bouton enfoncé. Animé, par images clés (temps en secondes, `t:x,y[,down]`, séparées par `;`) : `// mouse=0:0.2,0.2 ; 5:0.8,0.6,down ; 8:0.8,0.6`. La position est interpolée linéairement entre deux clés et reste constante avant la première et après la dernière. `iMouse.zw` suit Shadertoy : position du dernier clic, `z` > 0 tant que le bouton est enfoncé, `w` > 0 seulement sur l'image où le clic commence ; le bouton est enfoncé à partir d'une clé `down` jusqu'à la clé suivante sans `down`. Sans réglage, 0 |
| `iDate` | date réelle par défaut ; `// date=2026-03-14T15:09:26` fixe la date de départ pour un rendu reproductible. La date avance avec le temps du rendu (le jour, le mois et l'année changent à minuit) ; `+rate=R` règle les secondes de date par seconde de vidéo : `// date=2026-03-14T15:09:26+rate=60` fait passer une minute par seconde, `+rate=0` la fige (1 par défaut) |
| Buffer A–D, Common | pris en charge sous forme de fichiers à côté du shader : `nom.bufferA.glsl` … `nom.bufferD.glsl` et `nom.common.glsl` (n'ajoutez que `nom.glsl` à la liste). Chaque passe lit ses entrées par ses propres commentaires `// iChannel0=bufferA` (`bufferA` à `bufferD`, images, `noise`, `grid`). Ordre : A, B, C, D, puis Image ; un tampon lu après sa passe dans la même image donne l'image courante, lu avant (ou par lui-même) il donne la précédente, comme sur Shadertoy. Les tampons sont en flottant 32 bits, démarrent à zéro, sont rendus une fois par image (sans flou de mouvement), avec filtrage linéaire et bords étirés par défaut. Un tampon peut avoir sa propre taille, réglée dans le shader principal (ou `config.ini`) : `// bufferA.scale=0.5` (fraction de la taille du rendu, supersampling compris) ou `// bufferA.size=512x512` (pixels, `512` seul pour un carré) ; la passe la voit dans `iResolution`, et les passes qui le lisent (et l'image) dans `iChannelResolution`. La lecture d'un tampon plus petit ou plus grand est bilinéaire, sauf avec `.filter=nearest` |
| Historique des tampons | un rendu qui démarre plus loin (reprise des parties, aperçu à un instant donné) rejoue d'abord les images précédentes sans les encoder ; le journal l'indique. L'aperçu rejoue 600 images au plus |
| GLSL écrit pour WebGL 2 | accepté : les lignes `#version` et `#extension` sont ignorées, les `precision`, `texture2D`, `texture2DLod` ainsi que `texelFetch`, `textureLod`, `dFdx`… fonctionnent (le programme compile en GLSL 3.30 core). Une erreur de compilation nomme la passe (`[Buffer A]`) et garde les numéros de ligne de son fichier |
| Erreurs de compilation | affichées « fichier:ligne: message » (comme un compilateur, le fichier étant le shader ou la passe) ; **double-cliquez** la ligne du journal pour ouvrir le fichier à cette ligne (Notepad++, Sublime Text ou VS Code s'ils sont installés, sinon l'application par défaut) |
| Rapport de compatibilité | avant chaque rendu le journal liste ce qui est ignoré ou figé (et, pour un shader VR, le mode 360° et la taille attendue) : `mainSound` écrit dans le fichier principal, cubemaps ou textures 3D lues sans canal, `iMouse` ou `iDate` lus sans valeur fixe, canal clavier sans touche, `iChannelTime` lu sans canal audio ni vidéo, canal réglé mais jamais lu par son code, et tampon ou passe Cubemap A que ni l'image ni une passe lue ne lit |
| Passe Sound | fichier `nom.sound.glsl` avec `vec2 mainSound(int samp, float time)`, rendue par le GPU (512×512 échantillons stéréo par bloc, 44,1 kHz, Common disponible) dans un fichier WAV temporaire qui devient la piste audio de la vidéo, à partir de l'instant `start` ; le réglage `audio`, s'il est renseigné, la remplace. Sorties vidéo seulement (GIF, WebP et séquences n'ont pas de son) ; le fichier temporaire est supprimé à la fin |
| Cubemap A | fichier `nom.cubemapA.glsl` avec `mainCubemap(out vec4 fragColor, in vec2 fragCoord, in vec3 rayOri, in vec3 rayDir)`, rendu sur les six faces (512×512) à chaque image ; lisible avec `// iChannel0=cubeA` |
| Import Shadertoy | glissez, ajoutez ou `--render` le fichier `.json` exporté depuis la page du shader (bouton Export) : passes, Common et commentaires `iChannel` sont écrits dans le dossier `shadertoy/` à côté du programme, puis rendus. Hors ligne, sans compte ni clé d'API. Les médias propres à Shadertoy (textures, cubemaps, volumes) sont remplacés par des équivalents générés (signalé dans le journal) ; la passe Sound est importée et rendue sous la forme `nom.sound.glsl` ; la webcam n'est pas prise en charge ; une passe `mainVR` est rendue en 360° (voir « mainVR ») ; une entrée clavier devient le canal clavier simulé (réglez les touches avec `// key=`). Le shader garde la licence de son auteur |
| canaux cubemap, texture 3D, audio, vidéo | pris en charge : `// iChannel0=cube:sky` (ciel généré), `cube:nom_%d.png` (six faces carrées 0 à 5 = +X −X +Y −Y +Z −Z) ou `cube:croix.png` (croix 4×3) ; `volume:noise` (généré) ou `volume:image.png@N` (N tranches côte à côte) ou `volume:fichier.bin` (format de volume 3D de Shadertoy : en-tête de 20 octets, puis des valeurs sur 8 bits ou en flottants 32 bits, 1 à 4 canaux, 1024 au plus par côté ; un canal donne le rouge seul, les flottants gardent toute leur précision ; les fichiers de volume de Shadertoy ne sont pas fournis, prenez votre propre `.bin`) ; `audio` (la bande son du réglage `audio`) ou `audio:fichier.mp3` donne la texture 512×2 de Shadertoy (ligne 0 spectre, ligne 1 forme d'onde, calculées au temps exact de chaque image, sans le lissage entre images de l'analyseur web) ; `video:fichier.mp4` joue une vidéo (en boucle, décodée par ffmpeg à la cadence du rendu, réduite à 1280×720 au plus, `.vflip` comme pour les images). Les déclarations `samplerCube` / `sampler3D` du shader sont reconnues automatiquement |
| `mainVR` (shaders VR) | rendu en **vidéo 360° équirectangulaire**. Un shader qui a `mainVR` et pas de `mainImage` est reconnu tout seul ; sinon réglez `// vr=mono` ou `// vr=stereo` (`vr=off` ignore `mainVR`). L'origine et la direction du rayon sont donnés pour chaque pixel par `mainVR(out vec4 fragColor, in vec2 fragCoord, in vec3 fragRayOri, in vec3 fragRayDir)` ; l'avant est le centre de l'image (−Z), le haut est +Y. **Mono** : largeur = 2 × hauteur (par ex. 4096×2048 ou 8192×4096), `fragRayOri` est l'origine. **Stéréo** : haut / bas, œil gauche en haut, largeur = hauteur (par ex. 4096×4096), `iResolution` est la taille d'un œil et l'origine se déplace sur un cercle de diamètre `vripd` (0.064 par défaut, en unités du shader), comme en stéréo omnidirectionnelle. Le journal avertit quand la taille ne convient pas. Dans un fichier MP4 ou MOV, les métadonnées de « vidéo sphérique » (équirectangulaire, haut-bas en stéréo) sont ajoutées après le rendu pour que YouTube et les lecteurs VR la lisent en 360° (`metadata=0` les omet ; les autres conteneurs ne peuvent pas les porter). Seule la passe Image est VR : les tampons et Cubemap A sont des passes normales, et la position et l'orientation de la tête d'un casque ne sont pas simulées |
| canal clavier | simulé et déterministe : `// iChannel0=keyboard` donne la texture 256×3 de Shadertoy (ligne 0 touche enfoncée, ligne 1 touche pressée pendant cette image, ligne 2 bascule ; à lire avec `texelFetch(iChannel0, ivec2(codeTouche, ligne), 0).x`), et les touches sont pressées aux instants donnés par `// key=code:début-fin` (code de touche comme en JavaScript, ou une lettre `a`–`z`, ou `space`, `enter`, `left`, `up`, `right`, `down`, `shift`, `ctrl`, `alt`, `esc`, `tab`, `backspace` ; temps en secondes de `iTime`). Plusieurs touches sur une ligne avec `;`, ou plusieurs lignes `// key=` : `// key=space:2-4.5 ; left:1-3`. Le vrai clavier n'est jamais lu |
| canal webcam | non pris en charge (le rendu n'est pas interactif) |
| lignes `// iChannel` inutiles si le shader ne lit pas le canal | un canal lu sans texture reste noir et le journal le signale |

</details>

<details>
<summary><b>Création : paramètres animés et post-traitement</b></summary>

**Paramètres.** Un commentaire déclare un `uniform` réglable, ajouté à toutes les passes du shader (image, tampons, Cubemap A, post) : `// uniform type nom = défaut [min..max]`.

```glsl
// uniform float vitesse = 1.0 [0..5]
// uniform int   nombre = 3 [1..10]
// uniform bool  halo = true
// uniform vec2  centre = 0.5, 0.5 [0..1]
// uniform color teinte = #ff8800      (un vec3 dans le shader)
// uniform float kick = audio.bass     (suit le son, voir plus bas)
void mainImage(out vec4 fragColor, in vec2 fragCoord) { ... vitesse ... }
```

Types : `float`, `int`, `bool`, `vec2`, `vec3`, `vec4`, `color`. Sans intervalle, le curseur va de 0 au double de la valeur par défaut (au moins 1). Un nom déjà pris (une variable Shadertoy comme `iTime`, un mot du GLSL, un uniform écrit dans le shader, un doublon) arrête le rendu avec un message clair, comme une déclaration ou une valeur invalide.

- **Panneau.** Windows : le bouton à curseurs de l'en-tête ouvre « Paramètres et post-traitement » ; Linux et macOS : la section « Paramètres du shader » en bas de la carte des réglages. Un curseur, une case ou un sélecteur de couleur par paramètre du shader sélectionné. Les valeurs sont enregistrées par shader dans le dossier `params/` à côté du programme (rien n'est écrit ailleurs) et reprises tout de suite par l'aperçu en lecture et par tous les rendus. « Rétablir les paramètres » revient aux valeurs écrites dans le shader. Le curseur de temps de l'aperçu montre un trait fin à chaque temps d'image clé du shader sélectionné.
- **Images clés.** `// key nom=0:1.0 ; 4:3.0 ; 8:1.0` (temps en secondes de `iTime`, puis la valeur ; `x,y` pour un vecteur, `#rrggbb` pour une couleur). La valeur est interpolée entre deux clés et reste constante avant la première et après la dernière. Un mot devant choisit l'interpolation : `smooth`, `linear` (par défaut) ou `constant` (un palier à chaque clé) : `// key teinte=smooth 0:1,0,0 ; 2:0,0,1`. Une valeur seule sans temps (`// key halo=false`) fixe le paramètre. Un paramètre à images clés est animé : son curseur est inactif. La même syntaxe marche en ligne de commande : `--param=vitesse=2.5` ou `--param="vitesse=smooth 0:1 ; 4:3"`. Ordre de priorité : la déclaration, puis les valeurs enregistrées par le panneau, puis `--param`, puis les lignes `// key` du shader.
- **Lié au son.** `// uniform float kick = audio.bass` (aussi `audio.level`, `audio.mid`, `audio.treble`) prend sa valeur dans le fichier du réglage `audio`, à l'instant exact de chaque image, avec la même analyse que le canal `audio` : `level` est le niveau sur les 23 dernières ms (1 = un sinus pleine échelle), `bass` la crête de 0 à 250 Hz, `mid` de 250 Hz à 2 kHz, `treble` de 2 à 10 kHz, tous dans 0..1. Avec `[min..max]` la valeur est ramenée à cet intervalle (`// uniform float kick = audio.bass [1..3]`). Seuls `float` et `int` peuvent suivre le son. Sans fichier son, le paramètre garde sa valeur par défaut et le journal le dit.

**Post-traitement.** Appliqué à l'image rendue **en virgule flottante, avant la conversion en 8 ou 10 bits** : il marche donc avec le supersampling, le flou de mouvement et la sortie 10 bits, et les valeurs au-dessus de 1 (lumières fortes) sont encore là pour le bloom. Ordre : la passe `nom.post.glsl`, puis les effets, puis la LUT, puis le fondu.

- **`nom.post.glsl`** à côté du shader : un `mainImage` qui reçoit l'image rendue dans `iChannel0` et a les mêmes entrées qu'un shader d'image (`iTime`, `iFrame`, `iMouse`, paramètres, `// iChannel1=...`, Common, tampons). Les erreurs sont données en `fichier:ligne`.
- **Effets**, dans la fenêtre (case + curseur) ou `// post=bloom:0.4,grain,vignette` (nom, puis la quantité après deux-points ; sans elle, celle par défaut). Ils sont toujours appliqués dans cet ordre :

  | Effet | Quantité (défaut) | Ce qu'il fait |
  |---|---|---|
  | `sharpen` | 0 à 4 (0.5) | netteté (masque flou) |
  | `chroma` | 0 à 0.2 (0.01) | aberration chromatique, croissante vers les bords |
  | `bloom` | 0 à 4 (0.35) | halo des parties lumineuses (flou à trois échelles) |
  | `contrast` | −1 à 2 (0.15) | contraste autour de 0.5 |
  | `saturation` | −1 à 2 (0.25) | −1 donne du gris |
  | `hue` | −0.5 à 0.5 (0.05) | rotation de la teinte, en tours (1 = 360°) |
  | `vignette` | 0 à 2 (0.4) | coins assombris |
  | `grain` | 0 à 1 (0.06) | grain de pellicule, différent à chaque image, identique à chaque rendu |

- **LUT couleur.** `// lut=etalonnage.cube` (chemin relatif au shader) ou le champ « LUT couleur » : un fichier `.cube`, 1D (`LUT_1D_SIZE` 2 à 256) ou 3D (`LUT_3D_SIZE` 2 à 65), avec `DOMAIN_MIN` / `DOMAIN_MAX`. La table 3D est lue par **interpolation tétraédrique**. Le fichier est vérifié à la lecture : mauvaise taille, mauvais nombre de lignes, mot-clé inconnu ou nombre invalide arrêtent le rendu avec le nom du fichier et la ligne.

La fenêtre montre les effets dans l'aperçu en lecture (relancé un instant après un changement) ; `// post=` et `// lut=` écrits dans un shader priment sur la fenêtre. Les réglages `post` et `lut` peuvent aussi s'écrire dans `config.ini`. Pas fait : les effets dans la fenêtre principale de Windows elle-même (ils sont dans la fenêtre « Paramètres »), plusieurs passes `.post.glsl`, et des tailles propres pour la chaîne de post-traitement (elle travaille à la taille du rendu).

</details>

<details>
<summary><b>Sortie : transparence, HDR, taille, son, texte, montage, planches-contact</b></summary>

Tout ceci s'écrit en commentaires `// clé=valeur` dans le shader, dans `config.ini` ou en ligne de commande (`--clé=valeur`) ; la fenêtre de réglages n'a pas encore de contrôles pour cela.

**Transparence** (`alpha=1`). Le `fragColor.a` du shader est conservé (alpha droit, pas prémultiplié ; la plupart des shaders Shadertoy y écrivent n'importe quoi ou 0, c'est donc désactivé par défaut). Possible avec **ProRes 4444** (MOV, MKV), **VP9** (WebM, MKV ; alpha 8 bits), **WebP** et les séquences **PNG / TIFF / EXR** ; toute autre combinaison (H.264, H.265, AV1, GIF, ProRes 422...) est refusée avant le rendu avec un message. Le supersampling, le flou de mouvement et le post-traitement gardent l'alpha (le flou accumule des couleurs prémultipliées, les bords restent exacts) ; les fondus agissent sur l'alpha, l'aperçu en lecture et l'image d'aperçu montrent la transparence sur un damier gris. L'estimation de taille compte le canal alpha. Impossible avec le HDR ou l'interpolation d'images.

**HDR** (`hdr=pq` pour le HDR10, `hdr=hlg` pour le HLG). L'image est rendue en virgule flottante et encodée en **BT.2020** avec le transfert PQ ou HLG, 10 bits (réglé automatiquement), avec les étiquettes de couleur correspondantes. Les valeurs du shader sont lues comme du sRGB : **1.0 est le blanc de référence**, `hdrwhite` cd/m² (203 par défaut, BT.2408), et les valeurs au-dessus de 1.0 deviennent des hautes lumières (une lumière écrite `vec3(4.0)` vaut 4 fois le blanc). En HDR10 le flux déclare ses luminances : `hdrpeak` (MaxCLL et volume d'affichage, 1000 cd/m² par défaut) et `hdrfall` (MaxFALL, 400) ; elles sont déclarées, pas mesurées sur vos images. Codecs : **H.265** (x265, avec le SEI HDR10), **AV1** (SVT-AV1 avec les métadonnées statiques quand ce ffmpeg les accepte, sinon les seules étiquettes de couleur ; le journal le dit) et **VP9** (étiquettes de couleur seulement), en MP4, MKV ou WebM. Les encodeurs matériels sont en 8 bits ici : l'encodeur CPU est utilisé et le journal le dit. Il n'y a pas de conversion HDR vers SDR : l'aperçu et les images fixes (`--png`) restent en SDR.

**Taille, cadence et son.**
- `scale=1920x1080` ou `scale=2` / `scale=0.5x` (facteur) : rend à la taille de la fenêtre, écrit à une autre ; `scalefilter=lanczos` (défaut), `bicubic`, `bilinear`, `area` ou `neighbor` (plus proche voisin, pour le pixel art). Les dimensions sont arrondies au nombre pair.
- `interp=60` : rend à `fps` (par ex. 30) et laisse `minterpolate` de ffmpeg écrire 60 images par seconde. Le rendu natif est toujours meilleur (un shader peut rendre exactement les images en plus) ; c'est pour les shaders trop lents. Ça marche en 8 bits, pas avec l'alpha, et entre les parties d'un long rendu la première image de chaque partie n'a pas d'image précédente à interpoler.
- `loudnorm=-16` : normalisation EBU R128 à ce niveau (LUFS), en une passe, pour le fichier `audio` et pour la passe Sound. Un son court donne un résultat moins exact (le filtre mesure sur environ 3 secondes).

**Texte sur la vidéo** (`title=Mon titre`, `credit=Shader de Nom`, `titledur=3`). Le titre est écrit au milieu et le crédit en bas à gauche pendant les `titledur` premières secondes, en fondu à l'entrée et à la sortie, avec le filtre `drawtext` de ffmpeg et une police du système (Segoe UI sous Windows, DejaVu, Liberation ou Noto sous Linux, Helvetica ou Arial sous macOS) ; le rendu s'arrête avec un message si ce ffmpeg n'a pas `drawtext` ou si aucune police n'est trouvée. Le rendu par parties garde le bon minutage. Quand vous importez un `.json` de Shadertoy, le journal donne la ligne à ajouter pour créditer l'auteur.

**Montage** (`nom.montage`). Un fichier texte enchaîne plusieurs shaders en une vidéo :

```
# commentaire
set codec=h265              (réglages de la vidéo finale, comme config.ini)
duration=6                  (durée par défaut d'un clip, secondes)
transition=crossfade 1.0    (transition par défaut : cut | fade | crossfade | wipe, puis sa durée)
intro.glsl 5
plasma.glsl 8 crossfade 1.5
"mon tunnel.glsl" 6 cut
```

Une ligne de clip est un chemin (relatif au `.montage` ; entre guillemets s'il a des espaces), une durée facultative, puis la transition qui amène *dans* ce clip depuis le précédent (ignorée pour le premier) : `cut`, `fade` (par le noir), `crossfade` ou `wipe`, avec sa durée. Ajoutez le fichier à la liste ou en ligne de commande comme un shader. Chaque clip est rendu comme un shader, **par parties reprenables** (un clip déjà rendu avec les mêmes réglages est réutilisé après une interruption), dans un dossier `nom.clips` à côté de la vidéo, en ProRes HQ (ou en H.264 sans perte si ce ffmpeg n'a pas ProRes) ; puis ffmpeg les joint avec les transitions (`xfade`, `acrossfade`) et écrit la vidéo finale avec votre codec. La taille, la cadence, `scale`, `interp`, `title`, `credit`, les fondus et `loudnorm` s'appliquent au montage entier, pas à chaque clip ; le réglage `audio` est une seule bande-son sur tout le montage, sinon le son de chaque clip (une passe Sound) est joint avec du silence pour les clips qui n'en ont pas. Limites : conteneurs vidéo seulement, ni alpha ni HDR, ni aperçu ni image fixe d'un montage, et un commentaire `// audio=` ou `// duration=` dans un clip est remplacé par le montage.

**Planche-contact et vignette de couverture** (ligne de commande). `--sheet=4x3` écrit `nom_sheet.png`, une grille de 4 × 3 vignettes tirées au milieu de 12 intervalles égaux de la plage rendue (100 vignettes au plus, 1920 pixels de large au total) ; `--thumb` écrit `nom_thumb.png`, une image du milieu de la plage, ou `--thumb=2.5` à la seconde 2,5, 1280 pixels de large au plus. Les deux marchent avec les tampons (l'historique est rejoué) et avec le post-traitement.

</details>

<details>
<summary><b>Reprise après interruption</b></summary>

Les vidéos plus longues que `chunk` secondes sont rendues par parties, dans un dossier `<nom>.parts` à côté de la vidéo. Si le rendu est annulé, plante ou échoue, les parties terminées restent : relance le même rendu **avec les mêmes réglages** et il reprend où il s'était arrêté, puis assemble les parties (copie des flux, sans réencodage). Si le shader ou un réglage a changé, les anciennes parties sont jetées.

</details>

<details>
<summary><b>Ligne de commande</b></summary>

| Commande | Effet |
|---|---|
| `ShaderToMP4 a.glsl b.glsl` | Ajoute les fichiers et lance le rendu |
| `ShaderToMP4 --render [options] a.glsl b.glsl` | Rendre sans fenêtre, identique sous Windows, Linux et macOS (voir plus bas) |
| `ShaderToMP4 --diagnostic` | Affiche système, carte graphique, OpenGL, ffmpeg et encodeurs trouvés (sans chemin ni donnée personnelle) : à coller dans un rapport de bug |
| `ShaderToMP4 --benchmark [--seconds=N] [--quick] [--json]` | Mesure les images par seconde en 720p, 1080p et 4K avec l'encodeur CPU, et avec le matériel s'il existe |
| `ShaderToMP4 --selftest [--update] [--quiet]` | Auto-test (voir plus bas) |
| `ShaderToMP4 --get-ffmpeg [--quiet]` | Télécharge et installe `ffmpeg` sans ouvrir la fenêtre (Windows et Linux) |
| `ShaderToMP4 --version` | Affiche la version, le copyright et la licence |
| `ShaderToMP4 --help` | Affiche les options |

**Rendu sans fenêtre** (`--render`) : les options sont les clés du tableau des réglages écrites `--clé=valeur` (`--width=1280 --height=720 --fps=30 --duration=10 --codec=h265 --container=mp4 --crf=18 --outdir=sortie`), appliquées après `config.ini` et après `--profile=fichier.ini` ; les commentaires `// clé=valeur` d'un shader restent prioritaires, comme dans la fenêtre. `--png` rend une seule image au temps de départ. `--thumb[=secondes]` écrit une vignette de couverture et `--sheet=4x3` une planche-contact (voir « Sortie »). `--param=nom=valeur` (répétable) règle un paramètre déclaré dans le shader par `// uniform`, ou lui donne des images clés : `--param="vitesse=smooth 0:1 ; 4:3"` (voir « Création »). `--watch=dossier` laisse le programme actif et rend chaque `.glsl` / `.txt` qui apparaît dans le dossier (une fois que sa taille ne change plus ; les fichiers de passes comme `nom.bufferA.glsl` ne sont pas des shaders à part ; arrêt par `Ctrl+C`, ou `--watch-count=N` pour s'arrêter après N rendus). La surveillance n'est jamais active par défaut. `--progress=json` écrit un objet JSON par ligne sur la sortie standard : `start`, `progress` (`frame`, `frames`, `percent`, `fps`, `eta`), `log`, `done` (avec le fichier `output`), `error`, puis `end` (`ok`, `failed`) ; `--progress=text` écrit une ligne tous les 10 %. Codes de sortie : `0` tout est rendu, `1` un rendu a échoué, `2` ligne de commande invalide, `3` pas d'OpenGL 3.3, `4` ffmpeg introuvable. Sous Windows le programme est une application à fenêtre : utilisez `start /wait` dans `cmd`, ou `Start-Process -Wait -NoNewWindow` dans PowerShell, pour l'attendre et lire le code de sortie.

```
ShaderToMP4 --render --progress=json --container=webm --codec=vp9 --width=1280 --height=720 --outdir=sortie shader.glsl
```

</details>

<details>
<summary><b>Langues</b></summary>

Tous les textes de l'interface viennent de `lang/en.json` et `lang/fr.json`, embarqués dans l'exécutable, et des autres fichiers du dossier `lang` livré à côté : allemand (`de`), espagnol (`es`), italien (`it`) et portugais (`pt`). Ces quatre traductions sont récentes et n'ont pas été relues par des locuteurs natifs : les corrections sont bienvenues. La langue suit le système par défaut (quand un fichier existe pour elle), se change avec le bouton globe et est mémorisée dans `window.ini`. Pour ajouter ou corriger une langue sans recompiler, place un fichier `lang/<code>.json` à côté de l'exécutable (les clés qui y manquent retombent sur l'anglais) ; le bouton globe passe alors en revue toutes les langues disponibles.

</details>

## Questions fréquentes

**Quels shaders Shadertoy sont supportés ?**
La plupart. L'onglet *Image* seul suffit : copie le code dans un fichier `.glsl` (la fonction `mainImage` suffit). Les onglets Buffer A–D, Common, Cubemap A et Sound vont dans des fichiers à côté (`nom.bufferA.glsl`, `nom.common.glsl`, `nom.cubemapA.glsl`, `nom.sound.glsl`), ou sont écrits pour toi quand tu importes le fichier `.json` exporté depuis la page du shader. Non pris en charge : le canal webcam (les shaders `mainVR` sont rendus en vidéos 360°, voir « Compatibilité Shadertoy » ; le clavier est simulé par `// key=`, le micro lit le fichier son du réglage `audio`).

**Et les textures, les buffers, le son ou la souris ?**
Les textures, cubemaps, textures 3D, canaux audio et vidéo (`iChannel0` à `iChannel3`), les buffers (multipasse) et la passe Sound sont pris en charge. La souris est simulée, fixe (`// mouse=0.5,0.5[,down]`) ou animée par images clés (`// mouse=0:0.2,0.2 ; 5:0.8,0.6,down`), sinon 0, et `iDate` peut être fixée par `// date=`. Une entrée lue sans être configurée (un canal sans texture, un `iMouse` non réglé) est signalée dans le rapport de compatibilité et reste noire ou à 0. Voir « Compatibilité Shadertoy » plus haut.

**Pourquoi pas une simple capture d'écran ?**
Une capture dépend de la vitesse de ton PC : si le shader est lourd, la vidéo saccade. Ici chaque image est rendue au temps exact, aussi lentement que nécessaire.

**Mon rendu est long, que faire ?**
Essaie l'encodeur matériel (`Encodeur : Auto`), un preset plus rapide, ou le préréglage *Brouillon* pour tester. Le journal indique si le goulot est le GPU ou l'encodeur.

**Où est `ffmpeg` ?**
Sous Windows et Linux, il est téléchargé à la demande à côté du programme. Tu peux aussi le placer toi-même dans le même dossier, ou le garder dans ton `PATH` (sous macOS, les dossiers Homebrew sont aussi parcourus).

**Quels encodeurs matériels sont pris en charge ?**

| Système | Encodeurs (réglage `Encodeur`) |
|---|---|
| Windows | NVENC (NVIDIA), AMF (AMD), QuickSync (Intel) |
| Linux | NVENC, VAAPI (AMD / Intel), QuickSync |
| macOS | VideoToolbox |

`Auto` les essaie dans cet ordre par un encodage de test de 0,2 s et retombe sur le CPU si aucun ne fonctionne.

**Est-ce que ça installe quelque chose ou écrit ailleurs sur mon PC ?**
Non. Le programme est portable : réglages (`config.ini`, `window.ini`), langues et `ffmpeg` vivent dans son propre dossier. Le programme lui-même n'écrit ni dans le registre, ni dans ton profil utilisateur, ni dans un dossier système (les pilotes graphiques peuvent garder leurs propres caches).

## Compiler

### Windows

Avec les Build Tools de Visual Studio (le chemin de `vcvars64.bat` est écrit dans `scripts\build.bat`, à adapter si besoin) :

```
.\scripts\build.bat
```

Cela compile les ressources (manifeste, icône, icônes SVG, fichiers de langue, informations de version) puis tous les fichiers de `src` vers `bin\ShaderToMP4.exe`, lié statiquement (aucun runtime à installer), et copie les fichiers de licence et `lang/` à côté. CMake fonctionne aussi (`cmake -S . -B build-cmake` puis `cmake --build build-cmake --config Release`), et `scripts\package-windows.ps1` fabrique le zip portable.

### Linux et macOS

Il faut CMake 3.20+, un compilateur C++17 et, sous Linux, les paquets de développement X11 / Wayland (sous Debian et Ubuntu : `sudo apt install cmake ninja-build xorg-dev libgl1-mesa-dev libxkbcommon-dev libwayland-dev wayland-protocols`).

```
./scripts/build.sh          # configure, télécharge GLFW, Dear ImGui et NanoSVG (versions figées) et compile bin/ShaderToMP4
./scripts/test.sh           # auto-test
./scripts/package-linux.sh x86_64      # ou aarch64 : dist/ShaderToMP4-<version>-linux-<arch>.tar.gz
./scripts/package-macos.sh             # dist/ShaderToMP4-<version>-macos-universal.zip (compiler avec -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64")
```

La première configuration demande une connexion internet pour récupérer les trois bibliothèques.

<details>
<summary>MinGW-w64 (non testé)</summary>

Le rendu SVG de Direct2D demande des en-têtes MinGW récents.

```
g++ -std=c++17 -O2 -finput-charset=UTF-8 -municode -mwindows -static -I src -o bin/ShaderToMP4.exe src/main.cpp src/core/*.cpp src/gui/*.cpp -lopengl32 -lgdi32 -luser32 -lcomctl32 -lcomdlg32 -lshell32 -ladvapi32 -lwininet -ld2d1 -ld3d11 -ldxgi -ldwrite -lshlwapi -luxtheme -ldwmapi -lole32
```

</details>

### Organisation du projet

```
src/        code source : core/ (moteur de rendu, sans interface), platform/ (seul code qui appelle le système : windows/, posix/),
            gui/ (interface Windows, Direct2D), ui_imgui/ (interface Linux et macOS, GLFW + Dear ImGui), main.cpp, main_posix.cpp
assets/     polices embarquées dans l'interface Linux / macOS (Inter)
cmake/      embarquement des ressources pour les compilations CMake
lang/       textes de l'interface : en, fr, de, es, it, pt (.json)
res/        manifeste (DPI, thème), icône, informations de version, icônes SVG (icons/)
scripts/    build.bat / build.sh (compiler), test.bat / test.sh (auto-test), package-*.{ps1,sh} (archives portables), check-repo.ps1 (contrôle du dépôt)
tests/      shaders de référence et leurs images .ref
examples/   shaders d'exemple
docs/       bannière, captures d'écran, site web
bin/        dossier de travail : l'exécutable (généré), ffmpeg, config.ini
```

### Auto-test et contrôle du dépôt

`scripts\test.bat` (`./scripts/test.sh` sous Linux et macOS) rend les shaders du dossier `tests` en 64×64 (à t = 1 s) et les compare à leurs images de référence `.ref` (écart moyen toléré : 2/255). Le rapport est dans `tests\report.txt` et le code de sortie vaut 0 si tout passe. Les références dépendent du GPU et du pilote : après un changement volontaire du rendu ou de matériel, régénère-les avec `scripts\test.bat --update`.

`scripts\check-repo.ps1` contrôle le dépôt avant chaque commit : en-têtes de licence, fichiers de langue concordants, aucun texte d'interface écrit en dur, documentation bilingue, règles de portabilité.

### Publier une version

1. Mettez le nouveau numéro dans `src/app_info.h` (`APP_VER_MAJOR`, `APP_VER_MINOR`, `APP_VER_PATCH`, `APP_VER_STR`).
2. Dans `CHANGELOG.md` et `CHANGELOG.fr.md`, renommez la section « Non publié » avec la version (`## 1.4.0 — date`).
3. Validez, puis `git tag v1.4.0 && git push --tags`. L'intégration continue fabrique les quatre archives, écrit `SHA256SUMS.txt` et publie la version avec la section du journal des modifications ; elle refuse une étiquette différente de `APP_VER_STR` ou un journal sans section pour cette version.

### Limites connues

- L'interface Windows s'adapte au DPI de l'écran. Sa mise en page a été vérifiée à 100 %, 125 %, 150 % et 200 % avec `--dpi=120|144|192` (option de test qui force l'échelle ; le comportement d'une fenêtre déplacée entre deux vrais écrans d'échelles différentes n'a pas été testé). Lecteurs d'écran : les champs, les listes déroulantes et les curseurs portent leur libellé comme nom pour l'automatisation de l'interface de Windows (UI Automation, vérifié avec un client UI Automation) ; Narrator et NVDA eux-mêmes n'ont pas été essayés.
- Les encodeurs matériels AMD et Intel (AMF, QuickSync, VAAPI), et l'AV1 matériel, n'ont pas été testés sur du matériel réel.
- macOS : le programme est compilé et empaqueté par l'intégration continue, mais les machines macOS hébergées n'ont pas de GPU : la fenêtre n'a donc pas été lancée sur un vrai Mac. Signale tout problème.
- Linux et macOS : les boîtes de dialogue de fichiers sont intégrées au programme (pas de boîte système), et la progression du rendu s'affiche dans le Dock de macOS (écrite d'après la documentation du système et compilée par l'intégration continue, mais jamais exécutée sur un vrai Mac) ; sous Linux il n'y a pas de progression sur le bouton de la barre des tâches (l'API des lanceurs passe par D-Bus, une bibliothèque que le programme ne livre pas), la fenêtre demande seulement l'attention à la fin d'un lot.

## Informations sur le projet

- **Copyright** © 2026 Patrick JAILLET
- **Licence** : distribué sous licence GPL-3.0-or-later, voir [LICENSE](LICENSE) et [COPYING](COPYING). Composants tiers : [THIRD_PARTY_NOTICES.fr.md](THIRD_PARTY_NOTICES.fr.md). `ffmpeg` est un programme séparé, non inclus dans ce dépôt : il est téléchargé à la demande depuis [gyan.dev](https://www.gyan.dev/ffmpeg/builds/) (Windows) ou [BtbN](https://github.com/BtbN/FFmpeg-Builds) (Linux) et reste soumis à sa propre licence.
- **Contact** : [sandefjord.development@proton.me](mailto:sandefjord.development@proton.me)
- **Site web** : <https://ap3demak-rgb.github.io/ShaderToMP4>
