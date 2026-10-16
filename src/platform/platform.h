// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Couche plateforme : seul endroit du programme qui appelle directement le système d'exploitation.
// Une implémentation par système : src/platform/windows/ et src/platform/posix/ (Linux et macOS).
// Le moteur garde std::wstring pour les textes et les chemins ; la conversion vers l'encodage natif se fait ici.
#pragma once
#include "common.h"

namespace platform {

// --- Chemins et fichiers -------------------------------------------------------------------------------------------
std::wstring appDir();  // dossier de l'application, séparateur final inclus
bool fileExists(const std::wstring& path);
bool dirExists(const std::wstring& path);
bool fileSize(const std::wstring& path, uint64_t& bytes);   // taille d'un fichier sans le lire
bool readFile(const std::wstring& path, std::string& out);  // contenu binaire complet
FILE* openFile(const std::wstring& path, const char* mode);  // fopen avec chemin Unicode ("rb", "wb"...)
bool makeDir(const std::wstring& path);                      // un niveau ; true aussi si le dossier existe déjà
bool removeFile(const std::wstring& path);
bool removeDir(const std::wstring& path);  // dossier vide
bool moveFile(const std::wstring& from, const std::wstring& to, bool overwrite);
bool freeSpace(const std::wstring& path, uint64_t& bytes);  // octets libres du volume contenant path
// Cree le fichier avec `data` seulement s'il n'existe pas (creation atomique, y compris sur un dossier partage) : faux s'il existe deja ou si la creation echoue.
// Sert de verrou entre plusieurs processus ou plusieurs ordinateurs (ferme de rendu).
bool createExclusive(const std::wstring& path, const std::string& data);
bool fileModTime(const std::wstring& path, long long& unixSeconds);  // date de derniere ecriture (horloge du systeme de fichiers), en secondes depuis 1970
struct DirEntry {
    std::wstring name;
    bool isDir = false;
};
bool listDir(const std::wstring& path, std::vector<DirEntry>& out);  // noms seulement, sans « . » ni « .. »
std::wstring homeDir();  // dossier personnel de l'utilisateur (point de départ des sélecteurs de fichiers)
wchar_t pathSeparator();  // '\\' ou '/'

// --- Système -------------------------------------------------------------------------------------------------------
std::wstring hostName();      // nom de l'ordinateur
long processId();             // identifiant du processus
std::string systemLanguage();                                    // code à deux lettres de la langue de l'utilisateur ("fr", "en"...)
std::wstring exeFileName(const std::wstring& baseName);          // "ffmpeg" -> "ffmpeg.exe" ou "ffmpeg"
std::wstring findInPath(const std::wstring& fileName);          // chemin complet du programme trouvé dans le PATH (et, sous macOS, dans les dossiers Homebrew), sinon vide
void openWithDefault(const std::wstring& pathOrUrl);             // ouvre un fichier, un dossier ou une adresse
void revealInFolder(const std::wstring& filePath);               // montre le fichier dans l'explorateur / le gestionnaire de fichiers
void sleepMs(int ms);
// Carte graphique a utiliser sur un PC a deux GPU : "default" (le systeme choisit), "high" (la plus puissante), "low" (la
// plus economique). A appeler AVANT la creation du premier contexte OpenGL. Linux : variables PRIME de Mesa et de NVIDIA ;
// macOS et Windows : sans effet (Windows demande deja le GPU puissant ; sa preference par programme est dans le registre).
void applyGpuPreference(const std::string& pref);
struct DateTime {
    int year, month, day;  // month : 1..12
    int hour, minute, second;
};
DateTime localTime();
// Police sans empattement du système (fichier .ttf / .ttc lisible), pour le texte incrusté dans la vidéo ; vide si aucune n'est trouvée.
std::wstring systemFontFile();
// Police du système qui contient les caractères japonais ("ja") ou chinois simplifiés ("zh") ; `fontNo` = indice dans un fichier .ttc. Vide si
// le système n'en a pas : l'interface Linux / macOS (police Inter, latine) s'en sert pour afficher ces deux langues.
std::wstring systemCjkFontFile(const std::string& lang, int& fontNo);
enum class MessageKind { Info, Warning, Error };
void showMessage(const std::wstring& title, const std::wstring& text, MessageKind kind);  // boîte de message, ou stderr sans interface

// --- Données embarquées dans l'exécutable (fichiers de langue) ---------------------------------------------------
bool embeddedData(const char* name, std::string& out);  // ex. "lang/en.json"

// --- Processus -----------------------------------------------------------------------------------------------------
// Lance un programme sans fenêtre. argv[0] : chemin ou nom cherché dans le PATH.
class Process {
public:
    // pipeStdin : le programme lit son entrée sur un tube (sinon vide) ; captureOutput : stdout et stderr fusionnés sur un tube.
    static std::unique_ptr<Process> start(const std::vector<std::wstring>& argv, bool pipeStdin, bool captureOutput);
    ~Process();                                          // tue le programme s'il tourne encore
    bool writeStdin(const uint8_t* data, size_t size);   // bloquant ; false si le tube est cassé
    void closeStdin();                                   // fin de l'entrée
    size_t readOutput(char* buf, size_t capacity);       // bloquant ; 0 = fin du flux
    void terminate();                                    // arrêt immédiat
    int wait();                                          // attend la fin, renvoie le code de sortie
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

private:
    struct Impl;
    explicit Process(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
// "a \"b c\" d" -> {"a", "b c", "d"} (les guillemets doubles regroupent ; pas d'autre échappement)
std::vector<std::wstring> splitCommand(const std::wstring& command);

// --- Réseau et empreintes ------------------------------------------------------------------------------------------
// Télécharge une adresse HTTPS ; sink(données, taille, taille totale ou 0) renvoie false pour arrêter.
bool httpGet(const std::wstring& url, const std::function<bool(const uint8_t*, size_t, unsigned long long)>& sink, std::wstring& err);
bool sha256File(const std::wstring& path, std::string& hexLower);

// --- OpenGL --------------------------------------------------------------------------------------------------------
// Contexte OpenGL 3.3 core caché, rendu courant dans le thread appelant. À détruire dans le même thread.
// ctx reçoit un identifiant opaque (nul si échec).
bool createGLContext(void*& ctx, std::wstring& err);
// Contextes OpenGL crees dans d'autres threads que le principal (auto-test de l'apercu en temps reel) : Windows le permet
// toujours ; sous Linux / macOS GLFW exige une fenetre cachee creee par le thread principal. A appeler depuis ce thread,
// release apres l'arret des threads concernes. Renvoie false si impossible.
// Vrai si plusieurs contextes OpenGL peuvent travailler en meme temps dans des threads differents (rendu parallele). Windows : oui. Linux / macOS : un seul
// contexte est partage (fenetre GLFW ou EGL unique), le rendu parallele y est remplace par un rendu en serie.
bool multiContext();
bool prepareOffThreadContexts();
void releaseOffThreadContexts();
void destroyGLContext(void*& ctx);
void* glProc(const char* name);  // adresse d'une fonction OpenGL (contexte courant requis)

}  // namespace platform
