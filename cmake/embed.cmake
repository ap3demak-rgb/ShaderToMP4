# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Patrick JAILLET
#
# Produit un fichier C++ contenant des fichiers binaires (langues, police, icônes) :
#   cmake -DOUT=<fichier.cpp> -DROOT=<dossier> -DLIST="nom=chemin;nom=chemin" -P embed.cmake
set(body "// Fichier produit par cmake/embed.cmake : ne pas modifier.\n#include <cstddef>\nnamespace platform {\nstruct EmbeddedFile { const char* name; const unsigned char* data; size_t size; };\n")
set(table "")
set(count 0)
foreach(item IN LISTS LIST)
    string(REGEX MATCH "^([^=]+)=(.*)$" _ "${item}")
    set(name "${CMAKE_MATCH_1}")
    set(path "${ROOT}/${CMAKE_MATCH_2}")
    file(READ "${path}" hex HEX)
    string(LENGTH "${hex}" len)
    math(EXPR size "${len} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\1," bytes "${hex}")
    string(APPEND body "static const unsigned char kData${count}[] = {${bytes}0};\n")
    string(APPEND table "    {\"${name}\", kData${count}, ${size}},\n")
    math(EXPR count "${count} + 1")
endforeach()
string(APPEND body "extern const EmbeddedFile kEmbeddedFiles[] = {\n${table}};\nextern const size_t kEmbeddedFileCount = ${count};\n}  // namespace platform\n")
file(WRITE "${OUT}" "${body}")
