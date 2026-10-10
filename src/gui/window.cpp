#include "app.h"
#include "core/ffmpeg_download.h"
#include "core/util.h"

static void layout() {
    RECT rc;
    GetClientRect(g.wnd, &rc);
    const int w = rc.right, h = rc.bottom, m = 10;
    int x = m;
    auto btn = [&](HWND b, int bw) {
        MoveWindow(b, x, m, bw, 28, TRUE);
        x += bw + 6;
    };
    btn(g.bAdd, 100);
    btn(g.bRemove, 80);
    btn(g.bClear, 60);
    btn(g.bFolder, 90);
    int rxr = w - m;
    auto rbtn = [&](HWND b, int bw) {
        rxr -= bw;
        MoveWindow(b, rxr, m, bw, 28, TRUE);
        rxr -= 6;
    };
    rbtn(g.bCancel, 80);
    rbtn(g.bPause, 80);
    rbtn(g.bStart, 100);
    rbtn(g.bSnap, 90);

    int y = 46;
    // --- Panneau de reglages (6 lignes) ---
    const int grpH = 206;
    MoveWindow(g.grp, m, y, w - 2 * m, grpH, TRUE);
    int rx = m + 12, ry = y + 20;
    auto put = [&](HWND c, int cw, int ch = 24, int dy = 0) {
        MoveWindow(c, rx, ry + dy, cw, ch, TRUE);
        rx += cw + 6;
    };
    auto nextRow = [&] {
        rx = m + 12;
        ry += 30;
    };
    put(g.labels[0], 72, 18, 4);
    put(g.cmbRes, 190, 220);
    put(g.edW, 58);
    put(g.labels[1], 12, 18, 4);
    put(g.edH, 58);
    put(g.labels[2], 30, 18, 4);
    put(g.cmbFps, 70, 200);
    put(g.labels[9], 48, 18, 4);
    put(g.cmbSS, 76, 200);
    nextRow();
    put(g.labels[3], 72, 18, 4);
    put(g.edDur, 70);
    put(g.labels[4], 48, 18, 4);
    put(g.edStart, 70);
    put(g.labels[5], 44, 18, 4);
    put(g.cmbCodec, 80, 200);
    put(g.labels[6], 50, 18, 4);
    put(g.cmbPreset, 100, 200);
    nextRow();
    put(g.labels[7], 72, 18, 4);
    put(g.trkCrf, 170, 28);
    put(g.lblCrf, 110, 18, 4);
    put(g.labels[8], 62, 18, 4);
    put(g.cmbContainer, 70, 200);
    put(g.labels[18], 56, 18, 4);
    put(g.cmbEncoder, 140, 200);
    nextRow();
    put(g.labels[11], 72, 18, 4);
    put(g.edBitrate, 60);
    put(g.labels[12], 50, 18, 4);
    put(g.cmbColor, 190, 200);
    put(g.labels[13], 32, 18, 4);
    put(g.cmbBlur, 76, 200);
    put(g.labels[14], 62, 18, 4);
    put(g.edLoop, 60);
    nextRow();
    put(g.labels[15], 72, 18, 4);
    put(g.edAudio, std::max(100, w - 2 * m - 24 - 72 - 90 - 78 - 50 - 28 - 50 - 7 * 6));
    put(g.bAudio, 90, 24);
    put(g.labels[16], 78, 18, 4);
    put(g.edFadeIn, 50);
    put(g.labels[17], 28, 18, 4);
    put(g.edFadeOut, 50);
    nextRow();
    put(g.labels[10], 72, 18, 4);
    put(g.edOut, std::max(100, w - 2 * m - 24 - 72 - 90 - 80 - 100 - 5 * 6));
    put(g.bBrowse, 90, 24);
    put(g.bLoadP, 80, 24);
    put(g.bSaveP, 100, 24);
    y += grpH + 8;

    // --- Liste (gauche) et apercu (droite) ---
    int avail = h - y - m - 52;
    int listH = std::max(170, avail * 50 / 100);
    int listW = (w - 2 * m) * 55 / 100;
    MoveWindow(g.list, m, y, listW, listH, TRUE);
    SendMessageW(g.list, LVM_SETCOLUMNWIDTH, 0, std::max(60, listW - 110 - 24));
    SendMessageW(g.list, LVM_SETCOLUMNWIDTH, 1, 110);
    int px = m + listW + 8, pw = w - 2 * m - listW - 8;
    int imgH = listH - 56;
    MoveWindow(g.preview, px, y, pw, imgH, TRUE);
    MoveWindow(g.trkPrev, px, y + imgH + 2, pw, 26, TRUE);
    MoveWindow(g.lblPrev, px, y + imgH + 32, std::max(40, pw - 90), 18, TRUE);
    MoveWindow(g.bPrev, px + pw - 84, y + imgH + 28, 84, 26, TRUE);
    y += listH + 8;
    MoveWindow(g.prog, m, y, w - 2 * m, 20, TRUE);
    y += 26;
    MoveWindow(g.status, m, y, w - 2 * m, 18, TRUE);
    y += 24;
    MoveWindow(g.log, m, y, w - 2 * m, std::max(20, h - y - m), TRUE);
}

static BOOL CALLBACK setFontProc(HWND c, LPARAM f) {
    SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
    return TRUE;
}

LRESULT CALLBACK wndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g.wnd = hw;
            HINSTANCE hi = GetModuleHandleW(nullptr);
            auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int id, DWORD ex = 0) {
                return CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, hw,
                                       (HMENU)(INT_PTR)id, hi, nullptr);
            };
            g.bAdd = mk(L"BUTTON", L"Ajouter...", WS_TABSTOP | BS_PUSHBUTTON, ID_ADD);
            g.bRemove = mk(L"BUTTON", L"Retirer", WS_TABSTOP | BS_PUSHBUTTON, ID_REMOVE);
            g.bClear = mk(L"BUTTON", L"Vider", WS_TABSTOP | BS_PUSHBUTTON, ID_CLEAR);
            g.bStart = mk(L"BUTTON", L"Demarrer", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_START);
            g.bCancel = mk(L"BUTTON", L"Annuler", WS_TABSTOP | BS_PUSHBUTTON, ID_CANCEL);
            g.bSnap = mk(L"BUTTON", L"Image PNG", WS_TABSTOP | BS_PUSHBUTTON, ID_SNAP);
            g.list = mk(L"SysListView32", L"", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS, ID_LIST);
            SendMessageW(g.list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH;
            col.cx = 400;
            col.pszText = const_cast<LPWSTR>(L"Fichier shader");
            SendMessageW(g.list, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
            col.cx = 150;
            col.pszText = const_cast<LPWSTR>(L"Statut");
            SendMessageW(g.list, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);
            // Panneau de reglages video
            auto label = [&](const wchar_t* t) {
                HWND l = mk(L"STATIC", t, SS_LEFT, 0);
                g.labels.push_back(l);
                return l;
            };
            auto addItems = [&](HWND c, std::initializer_list<const wchar_t*> items) {
                for (auto t : items) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)t);
            };
            g.grp = mk(L"BUTTON", L" Reglages video ", BS_GROUPBOX, 0);
            label(L"Resolution");
            g.cmbRes = mk(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_RES);
            for (const auto& r : kRes) SendMessageW(g.cmbRes, CB_ADDSTRING, 0, (LPARAM)r.name);
            SendMessageW(g.cmbRes, CB_ADDSTRING, 0, (LPARAM)L"Personnalise");
            g.edW = mk(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, ID_W, WS_EX_CLIENTEDGE);
            label(L"x");
            g.edH = mk(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, ID_H, WS_EX_CLIENTEDGE);
            label(L"FPS");
            g.cmbFps = mk(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL, ID_FPS);
            addItems(g.cmbFps, {L"24", L"30", L"60", L"120"});
            label(L"Duree (s)");
            g.edDur = mk(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, ID_DUR, WS_EX_CLIENTEDGE);
            label(L"Debut (s)");
            g.edStart = mk(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, ID_START_T, WS_EX_CLIENTEDGE);
            label(L"Codec");
            g.cmbCodec = mk(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_CODEC);
            addItems(g.cmbCodec, {L"H.264", L"H.265"});
            label(L"Preset");
            g.cmbPreset = mk(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_PRESET);
            for (auto p : kPresets) SendMessageW(g.cmbPreset, CB_ADDSTRING, 0, (LPARAM)p);
            label(L"Qualite CRF");
            g.trkCrf = mk(L"msctls_trackbar32", L"", WS_TABSTOP | TBS_AUTOTICKS, ID_CRF);
            SendMessageW(g.trkCrf, TBM_SETRANGE, TRUE, MAKELPARAM(0, 35));
            g.lblCrf = mk(L"STATIC", L"", SS_LEFT, 0);
            label(L"Conteneur");
            g.cmbContainer = mk(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_CONTAINER);
            addItems(g.cmbContainer, {L"MP4", L"MOV", L"MKV"});
            label(L"Lissage");
            g.cmbSS = mk(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_SS);
            addItems(g.cmbSS, {L"Aucun", L"x2", L"x3", L"x4"});
            label(L"Sortie");
            g.edOut = mk(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, ID_OUT, WS_EX_CLIENTEDGE);
            SendMessageW(g.edOut, 0x1501 /*EM_SETCUEBANNER*/, TRUE, (LPARAM)L"vide = a cote du fichier shader");
            g.bBrowse = mk(L"BUTTON", L"Parcourir...", WS_TABSTOP | BS_PUSHBUTTON, ID_BROWSE);
            g.bLoadP = mk(L"BUTTON", L"Profil...", WS_TABSTOP | BS_PUSHBUTTON, ID_LOADP);
            g.bSaveP = mk(L"BUTTON", L"Enregistrer...", WS_TABSTOP | BS_PUSHBUTTON, ID_SAVEP);
            label(L"Debit (kb/s)");
            g.edBitrate = mk(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, ID_BITRATE, WS_EX_CLIENTEDGE);
            SendMessageW(g.edBitrate, 0x1501, TRUE, (LPARAM)L"0 = crf");
            label(L"Couleur");
            g.cmbColor = mk(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_COLOR);
            addItems(g.cmbColor, {L"BT.709 TV (standard)", L"BT.709 PC (pleine plage)", L"sRGB (TV)", L"BT.601 TV (ancien)"});
            label(L"Flou");
            g.cmbBlur = mk(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_BLUR);
            addItems(g.cmbBlur, {L"Aucun", L"x4", L"x8", L"x16"});
            label(L"Boucle (s)");
            g.edLoop = mk(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, ID_LOOP, WS_EX_CLIENTEDGE);
            SendMessageW(g.edLoop, 0x1501, TRUE, (LPARAM)L"0 = libre");
            label(L"Audio");
            g.edAudio = mk(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, ID_AUDIO, WS_EX_CLIENTEDGE);
            SendMessageW(g.edAudio, 0x1501, TRUE, (LPARAM)L"fichier son (mp3, wav...) - vide = aucun");
            g.bAudio = mk(L"BUTTON", L"Parcourir...", WS_TABSTOP | BS_PUSHBUTTON, ID_AUDIOB);
            label(L"Fondu entree");
            g.edFadeIn = mk(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, ID_FADEIN, WS_EX_CLIENTEDGE);
            label(L"sortie");
            g.edFadeOut = mk(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, ID_FADEOUT, WS_EX_CLIENTEDGE);
            g.bPause = mk(L"BUTTON", L"Pause", WS_TABSTOP | BS_PUSHBUTTON, ID_PAUSE);
            g.bFolder = mk(L"BUTTON", L"Dossier", WS_TABSTOP | BS_PUSHBUTTON, ID_FOLDER);
            g.preview = mk(L"STATIC", L"", SS_OWNERDRAW, ID_PREVIEW, WS_EX_STATICEDGE);
            g.trkPrev = mk(L"msctls_trackbar32", L"", WS_TABSTOP | TBS_AUTOTICKS, ID_PREVTRK);
            SendMessageW(g.trkPrev, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            g.lblPrev = mk(L"STATIC", L"Apercu : t = debut", SS_LEFT, 0);
            g.bPrev = mk(L"BUTTON", L"Apercu", WS_TABSTOP | BS_PUSHBUTTON, ID_PREVBTN);
            label(L"Encodeur");
            g.cmbEncoder = mk(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_ENCODER);
            addItems(g.cmbEncoder, {L"CPU (x264/x265)", L"Auto (materiel si dispo)", L"NVIDIA NVENC", L"AMD AMF", L"Intel QuickSync"});

            g.prog = mk(L"msctls_progress32", L"", 0, ID_PROG);
            SendMessageW(g.prog, PBM_SETRANGE32, 0, 1000);
            g.status = mk(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, ID_STATUS);
            g.log = mk(L"EDIT", L"", WS_VSCROLL | WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, ID_LOG);
            SendMessageW(g.log, EM_LIMITTEXT, 1 << 24, 0);

            NONCLIENTMETRICSW ncm{};
            ncm.cbSize = sizeof(ncm);
            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
                g.font = CreateFontIndirectW(&ncm.lfMessageFont);
            if (g.font) EnumChildWindows(hw, setFontProc, (LPARAM)g.font);

            settingsToUI(g.base);
            updatePrevLabel();
            setStatus(L"Glisse des fichiers shader dans la fenetre, ou clique sur \"Ajouter...\".");
            updateButtons();
            return 0;
        }
        case WM_NOTIFY: {
            NMHDR* nh = (NMHDR*)lp;
            if (nh->hwndFrom == g.list && nh->code == NM_DBLCLK) {  // double-clic : lire la video terminee
                int i = ((NMITEMACTIVATE*)lp)->iItem;
                if (i >= 0 && i < (int)g.paths.size() && g.state[i] == ST_DONE) {
                    std::wstring p = outputPath(g.paths[i], g.base.outdir, L"." + W(g.base.container));
                    if (fileExists(p)) ShellExecuteW(hw, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
            }
            return 0;
        }
        case WM_HSCROLL:
            if ((HWND)lp == g.trkCrf) updateCrfLabel();
            if ((HWND)lp == g.trkPrev) {
                updatePrevLabel();
                if (LOWORD(wp) == TB_ENDTRACK) runPreview();  // curseur relache -> nouvel apercu
            }
            return 0;
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
            if (d->CtlID != ID_PREVIEW) break;
            RECT r = d->rcItem;
            HBRUSH bg = CreateSolidBrush(RGB(24, 24, 28));
            FillRect(d->hDC, &r, bg);
            DeleteObject(bg);
            if (g.prevBmp && g.prevW > 0 && g.prevH > 0) {
                int cw = r.right - r.left, ch = r.bottom - r.top;
                double sc = std::min((double)cw / g.prevW, (double)ch / g.prevH);
                int dw = std::max(1, (int)(g.prevW * sc)), dh = std::max(1, (int)(g.prevH * sc));
                HDC mem = CreateCompatibleDC(d->hDC);
                HGDIOBJ old = SelectObject(mem, g.prevBmp);
                SetStretchBltMode(d->hDC, HALFTONE);
                SetBrushOrgEx(d->hDC, 0, 0, nullptr);
                StretchBlt(d->hDC, r.left + (cw - dw) / 2, r.top + (ch - dh) / 2, dw, dh, mem, 0, 0, g.prevW, g.prevH,
                           SRCCOPY);
                SelectObject(mem, old);
                DeleteDC(mem);
            } else {
                SetBkMode(d->hDC, TRANSPARENT);
                SetTextColor(d->hDC, RGB(150, 150, 160));
                DrawTextW(d->hDC, L"Clique sur \"Apercu\" ou deplace le curseur", -1, &r,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            return TRUE;
        }
        case WM_APP_DL: {
            std::wstring* msg = (std::wstring*)lp;
            SendMessageW(g.prog, PBM_SETPOS, (WPARAM)(wp * 10), 0);
            setStatus(*msg);
            delete msg;
            return 0;
        }
        case WM_APP_DLDONE: {
            std::wstring* e = (std::wstring*)lp;
            if (g.worker.joinable()) g.worker.join();
            g.running = false;
            if (wp) {
                setStatus(L"ffmpeg.exe installe : tu peux lancer les rendus.");
                appendLog(L"ffmpeg.exe installe.");
            } else {
                setStatus(L"Telechargement de ffmpeg echoue.");
                appendLog(L"ERREUR : " + *e);
                MessageBoxW(hw, (L"Le telechargement de ffmpeg a echoue :\n\n" + *e +
                                 L"\n\nTu peux aussi placer ffmpeg.exe a la main a cote de ShaderToMP4.exe.").c_str(),
                            L"ffmpeg", MB_ICONWARNING);
            }
            delete e;
            updateButtons();
            return 0;
        }
        case WM_APP_PREVIEW: {
            PreviewData* p = (PreviewData*)lp;
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth = p->w;
            bi.bmiHeader.biHeight = -p->h;  // haut en bas
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP b = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (b && bits) {
                memcpy(bits, p->bgra.data(), p->bgra.size());
                if (g.prevBmp) DeleteObject(g.prevBmp);
                g.prevBmp = b;
                g.prevW = p->w;
                g.prevH = p->h;
                InvalidateRect(g.preview, nullptr, FALSE);
            }
            delete p;
            return 0;
        }
        case WM_SIZE:
            if (g.list) layout();
            return 0;
        case WM_GETMINMAXINFO:
            ((MINMAXINFO*)lp)->ptMinTrackSize = {780, 690};
            return 0;
        case WM_DROPFILES: {
            HDROP d = (HDROP)wp;
            UINT n = DragQueryFileW(d, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::wstring> files;
            for (UINT i = 0; i < n; i++) {
                wchar_t p[MAX_PATH * 2];
                if (DragQueryFileW(d, i, p, MAX_PATH * 2)) files.push_back(p);
            }
            DragFinish(d);
            addFiles(files);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == ID_RES && HIWORD(wp) == CBN_SELCHANGE) {
                int i = (int)SendMessageW(g.cmbRes, CB_GETCURSEL, 0, 0);
                if (i >= 0 && i < kResCount) {
                    SetWindowTextW(g.edW, std::to_wstring(kRes[i].w).c_str());
                    SetWindowTextW(g.edH, std::to_wstring(kRes[i].h).c_str());
                }
                return 0;
            }
            if ((LOWORD(wp) == ID_W || LOWORD(wp) == ID_H) && HIWORD(wp) == EN_CHANGE) {
                syncResCombo();
                return 0;
            }
            switch (LOWORD(wp)) {
                case ID_ADD: browseFiles(); break;
                case ID_REMOVE: removeSelected(); break;
                case ID_CLEAR: clearAll(); break;
                case ID_START: startJobs(); break;
                case ID_SNAP: startJobs(true); break;
                case ID_BROWSE: browseOutDir(); break;
                case ID_LOADP: loadProfile(); break;
                case ID_SAVEP: saveProfile(); break;
                case ID_CANCEL:
                    g.cancel = true;
                    g.pause = false;
                    EnableWindow(g.bCancel, FALSE);
                    EnableWindow(g.bPause, FALSE);
                    setStatus(L"Annulation en cours...");
                    break;
                case ID_PAUSE:
                    g.pause = !g.pause;
                    SetWindowTextW(g.bPause, g.pause ? L"Reprendre" : L"Pause");
                    setStatus(g.pause ? L"En pause." : L"Rendu en cours...");
                    break;
                case ID_FOLDER: openFolder(); break;
                case ID_AUDIOB: browseAudio(); break;
                case ID_PREVBTN: runPreview(); break;
            }
            return 0;
        case WM_APP_LOG: {
            std::wstring* s = (std::wstring*)lp;
            appendLog(*s);
            delete s;
            return 0;
        }
        case WM_APP_PROGRESS: {
            ProgressInfo* p = (ProgressInfo*)lp;
            int idx = (int)wp;
            int pct = (int)(p->n * 100 / std::max(1LL, p->total));
            SendMessageW(g.prog, PBM_SETPOS, (WPARAM)(p->n * 1000 / std::max(1LL, p->total)), 0);
            if (idx >= 0 && idx < (int)g.paths.size()) setItemText(idx, 1, wfmt(L"En cours... %d%%", pct));
            setStatus(wfmt(L"Image %lld/%lld  (%d%%)  %.1f img/s  reste %ls", p->n, p->total, pct, p->speed,
                           fmtTime(p->remaining).c_str()));
            delete p;
            return 0;
        }
        case WM_APP_FILE: {
            int idx = (int)wp;
            if (idx >= 0 && idx < (int)g.paths.size()) {
                if (g.snapshotRun && lp == ST_DONE) {  // l'image ne compte pas comme rendu video termine
                    ShellExecuteW(hw, L"open", g.snapshotPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    lp = ST_PENDING;
                }
                g.state[idx] = (int)lp;
                setItemText(idx, 1, statusText((int)lp));
                if (lp == ST_RUNNING) SendMessageW(g.prog, PBM_SETPOS, 0, 0);
                if (lp == ST_DONE) SendMessageW(g.prog, PBM_SETPOS, 1000, 0);
            }
            return 0;
        }
        case WM_APP_DONE: {
            if (g.worker.joinable()) g.worker.join();
            g.running = false;
            if (g.previewRun) {
                g.previewRun = false;
                setStatus(L"Apercu pret.");
                updateButtons();
                return 0;
            }
            int fails = 0, cancelled = 0;
            for (int st : g.state) {
                if (st == ST_FAILED) fails++;
                if (st == ST_CANCELLED) cancelled++;
            }
            if (cancelled) setStatus(L"Annule.");
            else if (fails) setStatus(wfmt(L"Termine avec %d erreur(s).", fails));
            else setStatus(g.snapshotRun ? L"Image creee." : L"Tout est termine.");
            g.snapshotRun = false;
            appendLog(L"");
            updateButtons();
            return 0;
        }
        case WM_CLOSE:
            if (g.running) {
                g.cancel = true;
                if (g.worker.joinable()) g.worker.join();
                g.running = false;
            }
            {
                Settings cur = g.base;
                std::wstring e;
                if (uiToSettings(cur, e)) {
                    g.base = cur;
                    saveSettings();
                }
            }
            DestroyWindow(hw);
            return 0;
        case WM_DESTROY:
            if (g.font) DeleteObject(g.font);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}
