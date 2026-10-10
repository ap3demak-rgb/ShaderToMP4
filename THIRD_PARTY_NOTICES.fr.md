<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

**Français** · [English](THIRD_PARTY_NOTICES.md)

# Notices des composants tiers

ShaderToMP4 est distribué sous la **Licence Publique Générale GNU v3.0 ou ultérieure** (`GPL-3.0-or-later`, voir [COPYING](COPYING)). Ce document liste tous les composants tiers utilisés par le programme, avec leur licence et la raison pour laquelle elle est compatible avec la GPL-3.0. **Aucun composant ne peut être ajouté au projet sans une entrée ici.**

Dernière revue : ShaderToMP4 1.2.0.

## 1. Code compilé dans le programme

Aucun. Le lecteur JSON (traductions), la couche de dessin Direct2D, les fonctions de processus, de HTTP et de fichiers sont du code original du projet (`src/`). Seuls l'environnement d'exécution C++ et le Windows SDK servent à la compilation (voir la section 3).

## 2. Composants utilisés à l'exécution

| Composant | Rôle | Utilisation | Licence | Compatibilité GPL-3.0 |
|---|---|---|---|---|
| **FFmpeg** ([ffmpeg.org](https://ffmpeg.org)), build « release essentials » de Gyan Doshi ([gyan.dev](https://www.gyan.dev/ffmpeg/builds/)), avec libx264 et libx265 | Encodage vidéo / audio | **Non inclus** dans ce dépôt ni dans les archives de version. Le programme le télécharge à la demande (empreinte SHA-256 vérifiée) dans son propre dossier et l'exécute comme **processus séparé** (tubes). Aucune édition de liens. | GPL v3 pour ce build (il active les bibliothèques GPL libx264 et libx265 ; FFmpeg seul est LGPL 2.1+ / GPL 2+). Texte complet de la licence et sources : ffmpeg.org et gyan.dev. | Compatible : GPL v3, aucun code combiné. |
| **Composants système de Windows** : Direct2D, DirectWrite, Direct3D 11, DXGI, OpenGL (pilote graphique), WinInet, CryptoAPI, Shell, Common Controls, RichEdit, glisser-déposer OLE, progression de la barre des tâches (ITaskbarList3) | Interface, rendu, téléchargement, empreintes | Fournis par Windows et le pilote graphique ; **non redistribués**. | Licence Microsoft Windows / licence du pilote graphique | Couverts par la définition des « bibliothèques système » de la GPL-3.0 : ils font partie du système d'exploitation sur lequel le programme s'exécute. |
| **Polices** : Segoe UI (interface), Cascadia Mono (journal) | Texte | Fournies par Windows ; **non redistribuées**, non embarquées. Cascadia Mono est remplacée par une police système si elle est absente. | Licences de polices Microsoft / SIL OFL 1.1 (Cascadia) | Non distribuées avec le programme : aucune obligation de licence n'est transmise. |

## 3. Outils servant à compiler le programme (non distribués)

| Outil | Note de licence |
|---|---|
| Compilateur, éditeur de liens et compilateur de ressources Microsoft Visual C++ (Build Tools de Visual Studio), en-têtes et bibliothèques d'import du Windows SDK | L'environnement d'exécution C/C++ est **lié statiquement** (`/MT`) : la licence de la bibliothèque d'exécution de Microsoft autorise sa redistribution dans une application, et la GPL-3.0 considère les bibliothèques d'exécution du compilateur comme des bibliothèques système. |
| Windows PowerShell, `tar.exe` (bsdtar) | Fournis par Windows. `tar.exe` est aussi lancé à l'exécution pour décompresser l'archive FFmpeg téléchargée. |

## 4. Ressources

| Ressource | Origine | Licence |
|---|---|---|
| Logo et icône de l'application (`res/icons/logo.svg`, `res/app.ico`) | Création originale pour ShaderToMP4 | GPL-3.0-or-later |
| Icônes de l'interface (`res/icons/*.svg`) | Dessins originaux faits pour le projet dans un style de traits géométriques (grille 24 × 24, trait de 2 px). Ce sont des pictogrammes simples et génériques (flèches, dossiers, œil, lecture...) qui ne sont pas copiés d'un jeu d'icônes. | GPL-3.0-or-later |
| Shaders d'exemple et de test (`examples/`, `tests/`) | Code original du projet | GPL-3.0-or-later |
| Fichiers de langue (`lang/en.json`, `lang/fr.json`) | Textes originaux du projet | GPL-3.0-or-later |
| Captures d'écran et bannière (`docs/`) | Produites à partir du programme lui-même | GPL-3.0-or-later |

## 5. Règles pour les futurs composants

Avant d'ajouter une dépendance (bibliothèque, police, jeu d'icônes, outil téléchargé à l'exécution) :

1. Vérifier que sa licence est compatible avec la GPL-3.0-or-later (MIT, BSD, ISC, Zlib, Apache-2.0, LGPL, MPL-2.0, GPL-3.0, SIL OFL pour les polices : oui ; « usage non commercial », « pas de modification », GPL-2.0 seule, propriétaire : **non**).
2. Ajouter une ligne à ce fichier (anglais, [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)) et à celui-ci (français), avec la licence et la manière dont il est utilisé.
3. L'afficher dans la fenêtre « À propos » (bouton « Composants tiers »).
4. Garder le programme portable : rien n'est installé en dehors du dossier de l'application.
