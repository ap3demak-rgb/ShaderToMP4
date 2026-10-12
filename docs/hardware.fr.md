# Vérification sur matériel

Ce qui a réellement été essayé sur du vrai matériel, et ce qui ne l'a pas été. Rien n'est supposé : un point non testé est indiqué comme tel.

## Machine de test

- Windows 10, une NVIDIA GeForce GT 1030 (le rendu OpenGL 3.3 fonctionne ; la carte n'a pas d'encodeur vidéo matériel).
- Pas de GPU AMD ou Intel, pas de machine Linux avec un vrai GPU, pas de Mac.

## Encodeurs matériels (Windows, GT 1030)

Chaque encodeur a été demandé avec `encoder=` pour H.264, H.265 et AV1, avec un court rendu à chaque fois (12 rendus : `nvenc`, `amf`, `qsv` et `auto`, trois codecs chacun).

| Demande | Résultat |
|---|---|
| `nvenc`, `amf`, `qsv`, `auto` × `h264`, `h265`, `av1` | L'encodage d'essai de 0,2 s échoue, le journal indique que l'encodeur est indisponible, le rendu continue avec l'encodeur CPU (libx264, libx265, libsvtav1), le fichier est valide, code de sortie 0 |

Le repli sur le CPU est donc vérifié pour chaque encodeur et chaque codec sur une machine qui n'en possède aucun. Que les chemins matériels eux-mêmes produisent une vidéo correcte n'est **pas** vérifié ici.

## Non testé (pas de matériel)

| Point | Raison |
|---|---|
| Linux avec un vrai GPU NVIDIA (rendu, NVENC) | Pas de machine. Linux n'est couvert que par la CI (Mesa llvmpipe) |
| Linux avec un vrai GPU AMD (rendu, VAAPI) | Pas de machine |
| PC à deux GPU (`gpu=high` / `gpu=low`) | Pas de machine |
| AMD AMF, Intel QuickSync, VAAPI, AV1 matériel | Pas de tel GPU ; seul le repli a été exercé |
| macOS (fenêtre, VideoToolbox) | Pas de Mac ; les exécuteurs hébergés de la CI n'ont pas de GPU, la CI compile donc seulement et lance `--version` |
| `endaction=sleep` | Mettrait le PC du mainteneur en veille pendant l'essai ; non exécuté |

Si vous disposez d'un tel matériel, `ShaderToMP4 --diagnostic` et le journal d'un court rendu sont ce qu'il y a de plus utile à joindre à un ticket.

## Accessibilité et échelle d'affichage (Windows)

- NVDA : chaque bouton, champ, liste et curseur de la fenêtre principale et de la fenêtre Sortie est annoncé avec un nom par MSAA (boutons à icône seule compris).
- Narrator : l'arbre de l'interface expose les mêmes noms (vérifié par UI Automation pendant que Narrator tournait) ; la voix elle-même ne peut pas être enregistrée automatiquement et n'a pas été écoutée.
- Déplacement des fenêtres entre deux écrans à 100 % et 150 % : la fenêtre est redimensionnée à la nouvelle échelle et reste dans la zone de travail.
