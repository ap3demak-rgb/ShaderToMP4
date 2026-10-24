<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

[English](BUILD.md) · **Français**

# Compiler, tester et publier ShaderToMP4

Ce fichier s'adresse à ceux qui compilent le programme depuis ses sources. Seule l'édition libre (GPL-3.0-or-later) de ce dépôt y est décrite. Linux et macOS sont gelés depuis le 2026-10-22 : leur code est toujours dans le dépôt mais n'est plus compilé ni testé (voir la fin de ce fichier).

## Ce qu'il faut

| Outil | Sert à |
|---|---|
| Windows 10 ou 11, 64 bits, avec un pilote graphique qui offre **OpenGL 3.3** | lancer le programme et son auto-test |
| Visual Studio Build Tools (charge de travail C++, SDK Windows) | compiler (`cl`, `rc`, `link`) |
| PowerShell 5.1 ou plus récent | les scripts de `scripts\` |
| `ffmpeg.exe` | les tests de formats et tous les rendus ; `ShaderToMP4.exe --get-ffmpeg` le télécharge à côté du programme (SHA-256 vérifié) |
| CMake 3.20 ou plus récent | facultatif, une autre façon de compiler |
| NSIS 3 | seulement pour fabriquer l'installeur Windows (`scripts\package-installer.ps1`) |
| Git | les crochets et les contrôles du numéro de version |

Le chemin de `vcvars64.bat` est écrit en haut de `scripts\build.bat`, `build-asan.bat` et `build-coverage.bat` (Visual Studio Build Tools 18 dans son dossier par défaut). Modifiez-le si le vôtre est ailleurs, ou définissez la variable d'environnement `VCVARSALL` avec le chemin complet de `vcvars64.bat`.

## Compiler

```
.\scripts\build.bat
```

Le script compile les ressources (`res\app.rc` : manifeste, icône, icônes SVG, informations de version) et chaque fichier source de `src\main.cpp`, `src\core`, `src\platform` et `src\gui`, avec `cl /O2 /MT` (le runtime C++ est lié statiquement : rien à installer), puis lie `bin\ShaderToMP4.exe` avec `/Brepro` (pas d'horodatage : deux compilations des mêmes sources donnent les mêmes octets). Il copie aussi `COPYING`, `LICENSE`, les notices tierces et `lang\` dans `bin\`. `build\` contient les fichiers objets.

Avec CMake :

```
cmake -S . -B build-cmake -A x64
cmake --build build-cmake --config Release
```

Le programme est aussi écrit dans `bin\`. Le programme est une application à fenêtre : on journalise par l'interface, jamais avec `printf`.

## Tester

```
.\scripts\test.bat            # équivaut à : bin\ShaderToMP4.exe --selftest --quiet
.\scripts\test.bat --update   # enregistre de nouvelles images de référence (voir plus bas)
```

L'exécutable est une application à fenêtre : un appel direct depuis PowerShell rend la main aussitôt. Il faut l'attendre et lire le code de sortie :

```
$p = Start-Process bin\ShaderToMP4.exe -ArgumentList '--selftest','--quiet' -Wait -PassThru -NoNewWindow
$p.ExitCode      # 0 = tout passe
```

Le résultat est dans `tests\report.txt` (lignes qui commencent par `PASS`, `FAIL`, `DRIFT` ou `UPDATED`). L'auto-test :

- rend les shaders de `tests\*.glsl` en 64×64 (à t = 1 s) et les compare à des images de référence ; un fichier dont le nom a un deuxième point (`nom.bufferA.glsl`) est une passe d'un test, pas un test ;
- rend chaque format de sortie et le relit avec `ffmpeg -i` (sans ffmpeg ces cas affichent `SKIP` : lancez d'abord `--get-ffmpeg`) ;
- rend les soixante shaders d'exemple de `examples\gallery`, contrôle les lecteurs avec des entrées abîmées (fuzzing), l'explication des erreurs, la vérification des mises à jour, le rapport de plantage local, les contrôles de licence et les autres fonctions du moteur.

**Images de référence par pilote.** `tests\ref\<pilote>\<nom>.ref` est cherché d'abord (`nvidia`, `amd`, `intel`, `llvmpipe`, `warp`... ; le nom vient de `GL_RENDERER`), puis `tests\<nom>.ref`. `tests\tolerances.txt` donne à chaque cas sa tolérance avec l'écart mesuré entre les deux jeux gardés dans le dépôt (GeForce avec le pilote NVIDIA, et Mesa llvmpipe). Un écart au-delà de la moitié de la tolérance affiche `DRIFT`. Pour enregistrer le jeu de votre propre carte graphique, lancez `--selftest --update` sur cette machine : il n'écrit que dans `tests\ref\<votre pilote>\`, jamais sur les autres. Ne changez jamais un `.ref` existant pour faire passer un test qui échoue.

Quelques cas dépendent du temps (la carte du coût, l'aperçu en temps réel) : sur une machine occupée par autre chose ils peuvent échouer une fois ; relancez l'auto-test avant de chercher un défaut.

### OpenGL logiciel (sans carte graphique)

Une machine sans OpenGL 3.3 (un exécuteur d'intégration continue, une machine virtuelle) peut utiliser llvmpipe de Mesa : placez `opengl32.dll` et `libgallium_wgl.dll` d'une construction de Mesa pour Windows (par exemple l'archive `mesa3d-<version>-release-msvc.7z` du projet `mesa-dist-win`) à côté de l'exécutable et définissez `GALLIUM_DRIVER=llvmpipe`. Le jeu `tests\ref\llvmpipe` est alors celui qui sert. Mesa ne sert qu'aux tests : ne le livrez jamais.

### Fuzzing

```
bin\ShaderToMP4.exe --fuzz=all --fuzz-count=3000 --fuzz-seed=7
bin\ShaderToMP4.exe --fuzz=json           # une seule cible
```

Chaque lecteur de fichier ou de texte écrit dans le programme (JSON, `.montage`, `.ini`, `.cube`, EXR, TIFF flottant, fiche `.beats.json`, `jobs\*.ini`, `queue.txt`, expressions, courbes de temps...) reçoit des entrées abîmées ; le code de sortie est 0 quand aucune ne plante, ne boucle plus de 8 secondes ni ne lève d'exception (97 et 98 sinon). L'entrée en cours d'essai est toujours dans `fuzz_tmp\fuzz_last_<cible>.*` : après un plantage, c'est le cas fautif. Copiez-la dans `tests\fuzz\<cible>\` pour qu'elle soit rejouée désormais. Un nouveau lecteur doit avoir sa propre cible (`core\fuzz.cpp`, `registerFuzzTarget`).

### Erreurs de mémoire (AddressSanitizer)

```
.\scripts\build-asan.bat      # bin-asan\ShaderToMP4.exe, sans optimisation, pile de 16 Mo
.\scripts\test-asan.bat       # l'auto-test complet avec lui : aucune alerte attendue
```

Il faut le composant « MSVC AddressSanitizer » de Visual Studio. Quelques cas qui comptent sur des délais courts sont sautés sous le détecteur (tout y est plusieurs fois plus lent), ainsi que le plantage volontaire.

### Couverture

`scripts\build-coverage.bat` construit `bin-cov\` avec clang-cl (LLVM, `C:\Program Files\LLVM` ; définissez la variable `LLVM` s'il est ailleurs) et la couverture par source ; `scripts\coverage.ps1` lance ensuite l'auto-test complet dessus, fusionne les profils de chaque processus qu'il démarre et écrit `coverage\report.txt` (par fichier) ainsi que les totaux du moteur, des fenêtres et du reste. Le total du moteur doit rester à 80 % des lignes ou plus (`-Min`) ; les fenêtres ne sont pas pilotées par l'auto-test et comptent 0 %.

## Contrôles du dépôt

```
git config core.hooksPath .githooks     # une fois : lance les contrôles avant chaque commit
.\scripts\check-repo.ps1                # à la main ; il doit afficher « Depot conforme. »
```

Il vérifie les en-têtes de licence (`SPDX-License-Identifier` et la ligne de copyright dans chaque nouveau fichier source), la correspondance des fichiers de langue (`lang\*.json` : mêmes clés dans les neuf langues, chaque clé utilisée, aucun texte d'interface écrit dans le code), la documentation bilingue (chaque `.md` existe en anglais et en français), les règles de portabilité (pas de registre, pas de dossiers utilisateur), le contraste des couleurs de la palette et les règles d'anonymat. Les textes d'interface passent par `TX("clé")` / `TXF("clé", ...)` ; ajoutez chaque clé aux neuf `lang\*.json`.

## Autres contrôles

| Script | Ce qu'il fait |
|---|---|
| `scripts\verify-reproducible.ps1` | compile deux fois avec CMake dans le même dossier et compare le SHA-256 des deux exécutables |
| `scripts\audit-accessibility.ps1` | ouvre la fenêtre principale, À propos, l'Aide et les Exemples et vérifie, par UI Automation, que chaque contrôle a un nom et que Tab les atteint tous |
| `scripts\perf-journal.ps1` | banc d'essai, temps jusqu'à la fenêtre principale, mémoire d'un rendu de référence ; `-Previous <fichier>` compare à un autre journal (une régression de plus de 15 % échoue) |
| `scripts\soak-test.ps1` | un seul processus rend des vidéos en parties et en parallèle pendant `-Minutes` minutes pendant que les descripteurs et la mémoire sont relevés |
| `scripts\make-sbom.ps1` | nomenclature logicielle (SPDX 2.3) d'après `scripts\components.json` |
| `scripts\check-vulnerabilities.ps1` | interroge la base NVD sur les vulnérabilités connues des composants tiers livrés (réseau) |
| `scripts\test-queue.ps1` | pilote la vraie fenêtre : priorité, suspension, échec, annulation, réordonnancement en direct, glisser à la souris, `Alt+Haut`, fichier projet, `keys.ini`, palette de commandes, refaire un rendu depuis l'historique (ne touchez ni à la souris ni au clavier ; deux minutes environ) |
| `scripts\verify-portable.ps1`, `verify-installer.ps1` | le programme ne crée aucun fichier ni clé de registre hors de son dossier pendant une vraie session ; l'installeur est essayé en silence (installation, mise à jour, refus, désinstallation, traces laissées) |

Gardez `scripts\components.json` et `THIRD_PARTY_NOTICES.md` (et `.fr.md`) d'accord quand un composant est ajouté.

## Fabriquer les paquets

```
.\scripts\package-windows.ps1         # dist\ShaderToMP4-<version>-win64.zip (archive portable)
.\scripts\package-installer.ps1       # dist\ShaderToMP4-<version>-win64-setup.exe (NSIS, ffmpeg inclus)
.\scripts\package-manifests.ps1       # dist\manifests : manifestes winget et Scoop de la version
```

À lancer après une compilation. L'installeur embarque une construction de ffmpeg figée dont la version et le SHA-256 sont écrits dans `package-installer.ps1` ; le script refuse toute autre, et refuse une construction non GPL ou « nonfree ». L'archive portable ne contient pas ffmpeg : le programme le télécharge sur demande.

## Publier une version

1. Mettez le numéro dans `src\app_info.h` (`APP_VER_MAJOR`, `APP_VER_MINOR`, `APP_VER_PATCH`, `APP_VER_STR`).
2. Dans `CHANGELOG.md` et `CHANGELOG.fr.md`, renommez « Unreleased » / « Non publié » en `## <version> — <date>` ; mettez à jour l'écusson de version des README et de `docs\index.html`.
3. Lancez l'auto-test, l'auto-test AddressSanitizer et `scripts\check-repo.ps1` ; commitez et poussez ; attendez que l'intégration continue soit verte.
4. `git tag -a v<version> -m "ShaderToMP4 <version>" && git push origin v<version>`.

L'intégration continue (`.github\workflows\build.yml`, Windows seulement) compile, lance l'auto-test (avec Mesa llvmpipe), les tests de formats, la ligne de commande, le banc d'essai, l'auto-test AddressSanitizer, les paquets, les contrôles de l'installeur, la nomenclature logicielle, le rapport de vulnérabilités, les manifestes, le journal des performances et le contrôle de construction reproductible. Sur une étiquette elle publie la version avec `SHA256SUMS.txt` : elle refuse une étiquette différente de `APP_VER_STR`, un journal des modifications sans section pour la version et (sur une étiquette) une régression de plus de 15 % par rapport à la version précédente.

## Plateformes gelées

Le front-end Linux et macOS (`src\ui_imgui`, `src\platform\posix`, `scripts\build.sh`, `test.sh`, `package-linux.sh`, `package-macos.sh`, `CMakeLists.txt` hors `WIN32`) est gardé tel qu'il était dans la version 2.1.1, la dernière publiée avec des archives Linux et macOS. Il demande CMake 3.20+ et un compilateur C++17 (GLFW, Dear ImGui et NanoSVG sont récupérés à des versions figées). Aucun correctif, aucun test et aucune intégration continue ne sont prévus pour lui.

## Problèmes

- **« OpenGL 3.3 est requis »** : mettez à jour le pilote graphique, ou utilisez llvmpipe de Mesa comme ci-dessus. `bin\ShaderToMP4.exe --diagnostic` liste ce qui a été trouvé.
- **`vcvars64.bat` introuvable** : adaptez le chemin en haut des scripts de compilation ou définissez `VCVARSALL`.
- **L'auto-test affiche `SKIP` pour les formats** : lancez `bin\ShaderToMP4.exe --get-ffmpeg` (ou placez `ffmpeg.exe` à côté du programme).
- **Une fenêtre qui n'apparaît pas** : la position est gardée dans `bin\window.ini` ; supprimez le fichier pour la réinitialiser.
