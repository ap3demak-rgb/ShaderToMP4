<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

<p align="center">
  <img src="docs/banner.svg" alt="ShaderToMP4 : transforme un shader Shadertoy en vidéo, image par image" width="100%">
</p>

<p align="center"><a href="README.md">English</a> · <b>Français</b></p>

<p align="center">
  <a href="https://github.com/ap3demak-rgb/ShaderToMP4/releases/latest"><img alt="Télécharger" src="https://img.shields.io/badge/T%C3%A9l%C3%A9charger-Windows%2064%20bits-465AFF?style=for-the-badge&logo=windows&logoColor=white"></a>
  &nbsp;
  <a href="https://ap3demak-rgb.github.io/ShaderToMP4"><img alt="Site web" src="https://img.shields.io/badge/Site%20web-ShaderToMP4-FF5A96?style=for-the-badge"></a>
</p>

<p align="center">
  <img alt="Version" src="https://img.shields.io/badge/version-1.2.0-FF5A96?style=flat-square">
  <img alt="Plateforme" src="https://img.shields.io/badge/plateforme-Windows%2010%20%2F%2011-0078D4?style=flat-square&logo=windows&logoColor=white">
  <img alt="Portable" src="https://img.shields.io/badge/portable-sans%20installeur-3DDC97?style=flat-square">
  <img alt="Langues" src="https://img.shields.io/badge/langues-EN%20%C2%B7%20FR-7C6CFF?style=flat-square">
  <img alt="C++" src="https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white">
  <a href="LICENSE"><img alt="Licence" src="https://img.shields.io/badge/licence-GPL--3.0--or--later-3DDC97?style=flat-square"></a>
</p>

<p align="center">
  <img src="docs/screenshots/demo.gif" alt="Rendu d'un shader plasma" width="520">
</p>

**ShaderToMP4** convertit un shader [Shadertoy](https://www.shadertoy.com) (onglet *Image*) en vidéo, **image par image**, avec un temps exact (`iTime = image / fps`). Pas de capture d'écran, donc pas de saccades, pas de perte, quelle que soit la puissance de ta carte graphique : un shader qui tourne à 5 images par seconde en direct donne une vidéo parfaitement fluide.

Un seul exécutable Windows portable, **sans installeur**, avec une interface sombre moderne en français et en anglais.

---

## Pourquoi ShaderToMP4 ?

| | |
|---|---|
| **Temps exact** | Chaque image est rendue au temps `n / fps`, puis envoyée directement à ffmpeg par un tube : aucun fichier intermédiaire. |
| **Qualité maximale** | Jusqu'à 16384 pixels de large, supersampling ×4, flou de mouvement (×4, ×8, ×16), conversion colorimétrique BT.709 correcte, encodage x264 / x265 sans perte si tu veux. |
| **Rapide** | Rendu et encodage en parallèle, lecture GPU asynchrone, encodage matériel NVENC, AMF ou QuickSync en option. |
| **Fiable** | Les longs rendus se font par parties : si quelque chose plante, tu reprends exactement où tu t'étais arrêté. |
| **Pratique** | Aperçu intégré, préréglages (YouTube, 4K, vertical 9:16), audio avec fondus, boucle parfaite, file d'attente de plusieurs shaders, pause et annulation. |
| **Portable** | Rien à installer : il n'utilise que des composants de Windows. Réglages, langues et `ffmpeg.exe` restent dans le dossier de l'application. |

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

## Exemples

Le dossier [`examples`](examples) contient quatre shaders prêts à convertir : plasma, anneaux, tunnel et aurore.

<p align="center">
  <img src="docs/screenshots/gallery.png" alt="Galerie : plasma, anneaux, tunnel, aurore" width="860">
</p>

## Démarrage rapide

1. **Télécharge** la dernière archive portable depuis la page [Releases](https://github.com/ap3demak-rgb/ShaderToMP4/releases/latest) et décompresse-la où tu veux (ou [compile le projet](#compiler)).
2. Lance `ShaderToMP4.exe`. Si `ffmpeg.exe` est absent, le programme propose de le télécharger (environ 80 Mo depuis [gyan.dev](https://www.gyan.dev/ffmpeg/builds/), vérifié par SHA-256) et l'installe à côté de lui.
3. **Glisse** un ou plusieurs fichiers shader (`.glsl` ou `.txt`) dans la fenêtre, ou clique sur **Ajouter**.
4. Règle la vidéo (ou choisis un préréglage) puis clique sur **Démarrer**.
5. La vidéo est créée à côté du shader, ou dans le dossier choisi dans **Sortie**. Double-clic sur un fichier terminé pour la lire.

> Glisser des fichiers directement sur `ShaderToMP4.exe` les ajoute et lance le rendu tout de suite.

**Prérequis** : Windows 10 (version 1703 ou plus récente) ou Windows 11, et une carte graphique compatible OpenGL 3.3.

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
| **Retirer** | `Suppr` (dans la liste) | Retire les shaders sélectionnés |
| **Dossier** | | Ouvre l'explorateur sur la vidéo du shader sélectionné |
| **Profil / Enregistrer** | | Charge ou enregistre tous les réglages dans un fichier `.ini` |
| **À propos** | `F1` | Version, copyright, licence, contact, site web |
| **EN / FR** | | Change la langue de l'interface |

`Tab` passe d'un réglage au suivant.

</details>

<details>
<summary><b>Tous les réglages</b> (fenêtre, <code>config.ini</code> ou commentaires du shader)</summary>

Les réglages sont enregistrés dans `config.ini` à côté du `.exe`, et peuvent être imposés **par shader** avec un commentaire (prioritaire sur la fenêtre ; le journal le signale) :

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
| Codec | `codec` | `h264`, `h265` |
| Preset | `preset` | `ultrafast` ... `veryslow` |
| Qualité (CRF) | `crf` | 0 (sans perte), 12 (quasi parfait), 18 (très bon) |
| Débit | `bitrate` | kb/s ; 0 = qualité constante (CRF) |
| Conteneur | `container` | `mp4`, `mov`, `mkv` |
| Encodeur | `encoder` | `cpu`, `auto`, `nvenc`, `amf`, `qsv` (repli sur le CPU si indisponible) |
| Lissage | `ss` | 1 à 4 : rendu à N× la taille puis réduction (supersampling) |
| Couleur | `color` | `bt709`, `bt709full` (pleine plage), `srgb`, `bt601` |
| Flou | `blur`, `shutter` | 1, 4, 8, 16 sous-images par image ; obturateur (0,5 = 180°) |
| Boucle (s) | `loop` | période du shader : la durée devient un multiple entier, la dernière image précède exactement la première |
| Audio | `audio` | fichier son, calé sur le temps « Début », complété par du silence s'il est plus court |
| Fondu entrée / sortie | `fadein`, `fadeout` | secondes, image et son |
| Sortie | `outdir` | dossier de sortie (vide = à côté du shader) |
| (config.ini seulement) | `chunk` | rendu par parties de N secondes (30 par défaut, 0 = jamais) |
| (config.ini seulement) | `pbo` | profondeur de la file de lecture GPU (2 à 8, 3 par défaut) |

</details>

<details>
<summary><b>Reprise après interruption</b></summary>

Les vidéos plus longues que `chunk` secondes sont rendues par parties, dans un dossier `<nom>.parts` à côté de la vidéo. Si le rendu est annulé, plante ou échoue, les parties terminées restent : relance le même rendu **avec les mêmes réglages** et il reprend où il s'était arrêté, puis assemble les parties (copie des flux, sans réencodage). Si le shader ou un réglage a changé, les anciennes parties sont jetées.

</details>

<details>
<summary><b>Ligne de commande</b></summary>

| Commande | Effet |
|---|---|
| `ShaderToMP4.exe a.glsl b.glsl` | Ajoute les fichiers et lance le rendu |
| `ShaderToMP4.exe --selftest [--update] [--quiet]` | Auto-test (voir plus bas) |
| `ShaderToMP4.exe --get-ffmpeg [--quiet]` | Télécharge et installe `ffmpeg.exe` sans ouvrir la fenêtre |
| `ShaderToMP4.exe --version` | Affiche la version, le copyright et la licence |
| `ShaderToMP4.exe --help` | Affiche les options |

</details>

<details>
<summary><b>Langues</b></summary>

Tous les textes de l'interface viennent de `lang/en.json` et `lang/fr.json`, embarqués dans l'exécutable. La langue suit Windows par défaut, se change avec le bouton globe et est mémorisée dans `window.ini`. Pour ajouter ou corriger une langue sans recompiler, place un fichier `lang/<code>.json` à côté de l'exécutable (les clés qui y manquent retombent sur l'anglais) ; le bouton globe passe alors en revue toutes les langues disponibles.

</details>

## Questions fréquentes

**Quels shaders Shadertoy sont supportés ?**
Ceux qui n'utilisent que l'onglet *Image* et les entrées `iResolution`, `iTime`, `iTimeDelta`, `iFrame`, `iFrameRate`, `iDate`. Copie le code dans un fichier `.glsl` : la fonction `mainImage` suffit.

**Et les textures, les buffers, le son ou la souris ?**
Pas encore : `iChannel0` à `iChannel3` (textures, buffers, multipasse), `iMouse` (fixe à 0) et le son Shadertoy ne sont pas supportés. Un shader qui les utilise s'affiche avec un avertissement et un rendu noir pour ces entrées.

**Pourquoi pas une simple capture d'écran ?**
Une capture dépend de la vitesse de ton PC : si le shader est lourd, la vidéo saccade. Ici chaque image est rendue au temps exact, aussi lentement que nécessaire.

**Mon rendu est long, que faire ?**
Essaie l'encodeur matériel (`Encodeur : Auto`), un preset plus rapide, ou le préréglage *Brouillon* pour tester. Le journal indique si le goulot est le GPU ou l'encodeur.

**Où est `ffmpeg.exe` ?**
Il est téléchargé au premier lancement à côté de `ShaderToMP4.exe`. Tu peux aussi le placer toi-même dans le même dossier, ou le garder dans ton `PATH`.

**Est-ce que ça installe quelque chose ou écrit ailleurs sur mon PC ?**
Non. Le programme est portable : réglages (`config.ini`, `window.ini`), langues et `ffmpeg.exe` vivent dans son propre dossier. Le programme lui-même n'écrit ni dans le registre ni dans ton profil utilisateur (les pilotes graphiques peuvent garder leurs propres caches).

## Compiler

Avec les Build Tools de Visual Studio (le chemin de `vcvars64.bat` est écrit dans `scripts\build.bat`, à adapter si besoin) :

```
.\scripts\build.bat
```

Cela compile les ressources (manifeste, icône, icônes SVG, fichiers de langue, informations de version) puis tous les fichiers de `src` vers `bin\ShaderToMP4.exe`, lié statiquement (aucun runtime à installer), et copie les fichiers de licence et `lang/` à côté.

<details>
<summary>MinGW-w64 (non testé)</summary>

Le rendu SVG de Direct2D demande des en-têtes MinGW récents.

```
g++ -std=c++17 -O2 -finput-charset=UTF-8 -municode -mwindows -static -I src -o bin/ShaderToMP4.exe src/main.cpp src/core/*.cpp src/gui/*.cpp -lopengl32 -lgdi32 -luser32 -lcomctl32 -lcomdlg32 -lshell32 -ladvapi32 -lwininet -ld2d1 -ld3d11 -ldxgi -ldwrite -lshlwapi -luxtheme -ldwmapi -lole32
```

</details>

### Organisation du projet

```
src/        code source : main.cpp, core/ (moteur de rendu, sans interface), gui/ (fenêtre et dessin Direct2D)
lang/       textes de l'interface : en.json, fr.json
res/        manifeste (DPI, thème), icône, informations de version, icônes SVG (icons/)
scripts/    build.bat (compiler), test.bat (auto-test), check-repo.ps1 (contrôle du dépôt)
tests/      shaders de référence et leurs images .ref
examples/   shaders d'exemple
docs/       bannière, captures d'écran, site web
bin/        dossier de travail : ShaderToMP4.exe (généré), ffmpeg.exe, config.ini
```

### Auto-test et contrôle du dépôt

`scripts\test.bat` rend les shaders du dossier `tests` en 64×64 (à t = 1 s) et les compare à leurs images de référence `.ref` (écart moyen toléré : 2/255). Le rapport est dans `tests\report.txt` et le code de sortie vaut 0 si tout passe. Les références dépendent du GPU et du pilote : après un changement volontaire du rendu ou de matériel, régénère-les avec `scripts\test.bat --update`.

`scripts\check-repo.ps1` contrôle le dépôt avant chaque commit : en-têtes de licence, fichiers de langue concordants, aucun texte d'interface écrit en dur, documentation bilingue, règles de portabilité.

### Limites connues

- L'interface s'adapte au DPI de l'écran mais n'a été vérifiée qu'à 100 %.
- Les encodeurs matériels AMD et Intel n'ont pas été testés sur du matériel réel.

## Informations sur le projet

- **Copyright** © 2026 Patrick JAILLET
- **Licence** : distribué sous licence GPL-3.0-or-later, voir [LICENSE](LICENSE) et [COPYING](COPYING). Composants tiers : [THIRD_PARTY_NOTICES.fr.md](THIRD_PARTY_NOTICES.fr.md). `ffmpeg.exe` est un programme séparé, non inclus dans ce dépôt : il est téléchargé à la demande depuis [gyan.dev](https://www.gyan.dev/ffmpeg/builds/) et reste soumis à sa propre licence.
- **Contact** : [sandefjord.development@proton.me](mailto:sandefjord.development@proton.me)
- **Site web** : <https://ap3demak-rgb.github.io/ShaderToMP4>
