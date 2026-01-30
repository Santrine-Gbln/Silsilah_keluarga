/**
 * Family Tree Viewer - ROBUST LOADING VERSION
 * Penjelasan Detail untuk Video Presentasi
 */

// 1. PREVENT WINDOWS MACROS
// Mencegah Windows mendefinisikan makro min dan max agar tidak bentrok dengan std::min/max milik C++
#define NOMINMAX

// Memastikan aplikasi menggunakan set karakter Unicode (mendukung berbagai bahasa/simbol)
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// Include library yang dibutuhkan
#include <windows.h>   // Library utama untuk GUI Windows (Win32 API)
#include <vector>      // Kontainer array dinamis
#include <string>      // Manipulasi teks (std::wstring untuk Unicode)
#include <map>         // Untuk mapping ID orang ke indeks array secara cepat
#include <set>         // Untuk menyimpan data unik (misal: ID mantan pasangan)
#include <fstream>     // Untuk operasi pembacaan file eksternal (CSV)
#include <sstream>     // Untuk memproses string per baris atau per kolom
#include <algorithm>   // Untuk fungsi matematika seperti std::max
#include <iostream>    // Untuk output ke console (debugging)

// -----------------------------------------------------------------------------
// CONFIGURATION (Pengaturan Dimensi Visual)
// -----------------------------------------------------------------------------
const int BOX_WIDTH = 120;     // Lebar kotak tiap anggota keluarga
const int BOX_HEIGHT = 60;     // Tinggi kotak tiap anggota keluarga
const int V_GAP = 80;          // Jarak vertikal antar generasi (Ayah -> Anak)
const int H_GAP = 15;          // Jarak horizontal antar kotak saudara kandung
const int SPOUSE_GAP = 10;     // Jarak horizontal antara suami dan istri

// Definisi warna menggunakan format RGB
const COLORREF COL_CANVAS = RGB(242, 242, 235);   // Warna background (putih tulang)
const COLORREF COL_BOX_FEM = RGB(245, 144, 144);  // Warna kotak Perempuan (pink)
const COLORREF COL_BOX_MALE = RGB(123, 157, 201); // Warna kotak Laki-laki (biru)
const COLORREF COL_LINE = RGB(0, 0, 0);           // Warna garis hubungan (hitam)
const COLORREF COL_LINE_EX = RGB(0, 0, 0);        // Warna garis mantan pasangan

const std::string DATA_FILE_A = "Family.csv"; // Nama file sumber data CSV

// -----------------------------------------------------------------------------
// DATA STRUCTURES (Struktur Data)
// -----------------------------------------------------------------------------
struct Person {
    int id = 0;               // ID Unik setiap orang
    std::wstring name;        // Nama (Wide string untuk Unicode)
    std::wstring role;        // Peran/Jabatan (misal: Kakek, Ayah)
    std::wstring gender;      // Jenis Kelamin ("M" atau "F")
    int fatherId = 0;         // Referensi ID Ayah
    int motherId = 0;         // Referensi ID Ibu
    std::vector<int> spouses; // Daftar ID pasangan (suami/istri)
    std::set<int> exSpouses;  // Daftar ID mantan pasangan

    // Variabel untuk Layouting (Posisi di layar)
    int x = 0;                // Koordinat X di canvas
    int y = 0;                // Koordinat Y di canvas
    bool placed = false;      // Flag apakah orang ini sudah diatur posisinya
    int subtreeWidth = 0;     // Total lebar area yang dibutuhkan orang ini dan keturunannya
};

class DataModel {
public:
    std::vector<Person> people;       // List utama seluruh orang di database
    std::map<int, size_t> idToIndex;  // Kamus untuk mencari indeks berdasarkan ID
    FILETIME lastWriteTime = {0, 0};  // Menyimpan waktu terakhir file diubah (untuk auto-reload)
    int maxX = 0;                     // Batas terjauh koordinat X (untuk scrollbar)
    int maxY = 0;                     // Batas terjauh koordinat Y (untuk scrollbar)

    // Menghapus data lama saat akan memuat ulang file
    void Clear() {
        people.clear();
        idToIndex.clear();
        maxX = 0;
        maxY = 0;
    }

    // Mengambil pointer data orang berdasarkan ID
    Person* Get(int id) {
        auto it = idToIndex.find(id);
        if (it != idToIndex.end()) {
            return &people[it->second];
        }
        return nullptr;
    }
};

DataModel g_Model; // Instansiasi global model data

// -----------------------------------------------------------------------------
// HELPERS (Fungsi Pembantu)
// -----------------------------------------------------------------------------

// RAII Wrapper untuk GDI Object: Memastikan objek (pena/font) dikembalikan ke semula secara otomatis
class GdiObj {
    HGDIOBJ m_hOld;
    HDC m_hDC;
public:
    GdiObj(HDC hdc, HGDIOBJ hObj) : m_hDC(hdc) { m_hOld = SelectObject(hdc, hObj); }
    ~GdiObj() { SelectObject(m_hDC, m_hOld); }
};

// Mengonversi string standar (ANSI/UTF-8) ke wstring (Unicode Windows)
std::wstring ToWString(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_ACP, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_ACP, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// Mengonversi teks angka ke tipe data integer secara aman (mencegah crash jika data bukan angka)
int SafeToInt(const std::string& s) {
    if (s.empty()) return 0;
    try { return std::stoi(s); } catch (...) { return 0; }
}

// -----------------------------------------------------------------------------
// ROBUST DATA LOADING (Proses Membaca CSV)
// -----------------------------------------------------------------------------
void LoadData() {
    // Mengecek atribut file (apakah ada perubahan waktu modifikasi)
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExA(DATA_FILE_A.c_str(), GetFileExInfoStandard, &fileInfo)) {
        // Jika file belum berubah sejak load terakhir, batalkan load (efisiensi)
        if (CompareFileTime(&g_Model.lastWriteTime, &fileInfo.ftLastWriteTime) == 0) return;
        g_Model.lastWriteTime = fileInfo.ftLastWriteTime;
    } else {
        std::cout << "[ERROR] File not found: " << DATA_FILE_A << "\n";
        return;
    }

    g_Model.Clear(); // Bersihkan memori sebelum memuat data baru

    // Membuka file menggunakan stream byte standar
    std::ifstream file(DATA_FILE_A);

    if (!file.is_open()) {
        std::cout << "[ERROR] Could not open file stream!\n";
        return;
    }

    std::cout << "[INFO] File opened. Reading lines...\n";

    std::string line;
    int lineNum = 0;
    // Membaca file baris demi baris
    while (std::getline(file, line)) {
        lineNum++;
        // Menghapus karakter '\r' tersembunyi jika file dibuat di Windows
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string item;
        Person p;

        // Proses parsing kolom berdasarkan tanda koma (CSV)
        // 1. Ambil ID
        std::getline(ss, item, ',');
        p.id = SafeToInt(item);

        // Abaikan baris Header (biasanya baris pertama yang berisi teks bukan angka)
        if (p.id == 0) {
            std::cout << "[SKIP] Line " << lineNum << ": '" << line << "' (Not an ID)\n";
            continue;
        }

        // 2. Ambil Nama
        std::getline(ss, item, ','); p.name = ToWString(item);
        // 3. Ambil Role/Peran
        std::getline(ss, item, ','); p.role = ToWString(item);
        // 4. Ambil Gender
        std::getline(ss, item, ','); p.gender = ToWString(item);
        // 5. Ambil ID Ayah
        std::getline(ss, item, ','); p.fatherId = SafeToInt(item);
        // 6. Ambil ID Ibu
        std::getline(ss, item, ','); p.motherId = SafeToInt(item);

        // 7. Ambil Kolom Pasangan (Bisa lebih dari satu, dipisah karakter '|')
        if (std::getline(ss, item, ',')) {
            std::stringstream ssSpouse(item);
            std::string token;
            while (std::getline(ssSpouse, token, '|')) {
                if (token.empty()) continue;
                // Cek jika ada tanda 'x' (menandakan mantan pasangan/cerai)
                bool isEx = (token.back() == 'x' || token.back() == 'X');
                if (isEx) token.pop_back();

                int spId = SafeToInt(token);
                if (spId != 0) {
                    p.spouses.push_back(spId);
                    if (isEx) p.exSpouses.insert(spId);
                }
            }
        }

        // Masukkan objek orang ke dalam model data global
        g_Model.people.push_back(p);
        g_Model.idToIndex[p.id] = g_Model.people.size() - 1;

        std::cout << "[LOAD] OK - ID:" << p.id << "\n";
    }

    std::cout << "[STATUS] Finished. Loaded " << g_Model.people.size() << " people.\n";
}

// -----------------------------------------------------------------------------
// LAYOUT ENGINE (Logika Penempatan Pohon)
// -----------------------------------------------------------------------------

// Mengambil daftar anak berdasarkan ID Ayah dan Ibu
std::vector<int> GetChildren(int fatherId, int motherId) {
    std::vector<int> children;
    for (const auto& p : g_Model.people) {
        if (fatherId != 0 && motherId != 0) {
            // Anak dari pasangan resmi
            if (p.fatherId == fatherId && p.motherId == motherId) children.push_back(p.id);
        }
        else if (fatherId != 0) {
            // Hanya diketahui Ayahnya
            if (p.fatherId == fatherId && (p.motherId == 0)) children.push_back(p.id);
        }
        else if (motherId != 0) {
            // Hanya diketahui Ibunya
            if (p.motherId == motherId && (p.fatherId == 0)) children.push_back(p.id);
        }
    }
    return children;
}

// Rekursi untuk menghitung lebar total yang dibutuhkan sebuah keluarga (sub-pohon)
int CalculateSubtreeWidth(int personId) {
    Person* p = g_Model.Get(personId);
    if (!p) return 0;

    int spouseCount = (int)p->spouses.size();
    // Lebar blok orang tua: Lebar kotak + (Jumlah pasangan * lebar kotak mereka)
    int parentsBlockWidth = BOX_WIDTH + (spouseCount * (BOX_WIDTH + SPOUSE_GAP));
    int childrenTotalWidth = 0;

    // Hitung lebar yang dibutuhkan oleh semua anak-anaknya (secara rekursif)
    for (int spId : p->spouses) {
        int f = (p->gender == L"M") ? p->id : spId;
        int m = (p->gender == L"M") ? spId : p->id;
        auto children = GetChildren(f, m);
        for (int childId : children) {
            childrenTotalWidth += CalculateSubtreeWidth(childId) + H_GAP;
        }
    }

    // Hitung juga anak dari hubungan tanpa pasangan terdaftar
    int f = (p->gender == L"M") ? p->id : 0;
    int m = (p->gender == L"F") ? p->id : 0;
    if (f != 0 || m != 0) {
         auto singleChildren = GetChildren(f, m);
         for (int childId : singleChildren) {
             childrenTotalWidth += CalculateSubtreeWidth(childId) + H_GAP;
         }
    }

    if (childrenTotalWidth > 0) childrenTotalWidth -= H_GAP;
    // Lebar pohon adalah yang terbesar antara lebar barisan orang tua vs barisan anak
    p->subtreeWidth = std::max(parentsBlockWidth, childrenTotalWidth);
    return p->subtreeWidth;
}

// Menentukan koordinat X dan Y untuk setiap orang secara rekursif
void PositionSubtree(int personId, int x, int y) {
    Person* p = g_Model.Get(personId);
    if (!p || p->placed) return;

    p->y = y;
    p->placed = true;

    int spouseCount = (int)p->spouses.size();
    int parentsBlockWidth = BOX_WIDTH + (spouseCount * (BOX_WIDTH + SPOUSE_GAP));
    // Menengahkan posisi orang tua terhadap lebar total sub-pohon mereka
    int currentX = x + (p->subtreeWidth / 2) - (parentsBlockWidth / 2);

    p->x = currentX;

    // Mengatur posisi pasangan di sebelah kanan orang pertama
    int spouseStartX = currentX + BOX_WIDTH + SPOUSE_GAP;
    for (int spId : p->spouses) {
        Person* sp = g_Model.Get(spId);
        if (sp) {
            sp->x = spouseStartX;
            sp->y = y;
            sp->placed = true;
            spouseStartX += BOX_WIDTH + SPOUSE_GAP;
        }
    }

    // Mengatur posisi anak-anak di level berikutnya (bawah)
    int childStartX = x;
    auto LayoutBatch = [&](const std::vector<int>& kids) {
        for (int childId : kids) {
            Person* child = g_Model.Get(childId);
            if(child) {
                // Rekursi untuk anak
                PositionSubtree(childId, childStartX, y + V_GAP);
                childStartX += child->subtreeWidth + H_GAP;
            }
        }
    };

    // Panggil fungsi penempatan untuk tiap kelompok anak
    for (int spId : p->spouses) {
        int f = (p->gender == L"M") ? p->id : spId;
        int m = (p->gender == L"M") ? spId : p->id;
        LayoutBatch(GetChildren(f, m));
    }

    int f = (p->gender == L"M") ? p->id : 0;
    int m = (p->gender == L"F") ? p->id : 0;
    LayoutBatch(GetChildren(f, m));
}

// Fungsi utama untuk mengatur ulang seluruh tata letak pohon
void RecalculateLayout() {
    for (auto& p : g_Model.people) {
        p.placed = false;
        p.subtreeWidth = 0;
    }
    g_Model.maxX = 0;
    g_Model.maxY = 0;

    int currentRootX = 50; // Titik awal penggambaran
    int startY = 50;

    for (auto& p : g_Model.people) {
        // Mencari Akar (Orang yang tidak punya Ayah & Ibu di data)
        if (p.fatherId == 0 && p.motherId == 0 && !p.placed) {
            bool isLeader = true;
            // Jika punya pasangan, hanya proses orang dengan ID terkecil sebagai titik awal
            for(int spId : p.spouses) {
                if(spId < p.id) { isLeader = false; break; }
            }

            if (isLeader) {
                std::cout << "[LAYOUT] Positioning Root: " << p.id << "\n";
                int w = CalculateSubtreeWidth(p.id);
                PositionSubtree(p.id, currentRootX, startY);
                currentRootX += w + H_GAP + 50;
            }
        }
    }

    // Update dimensi maksimum untuk area scrollbar
    for (const auto& p : g_Model.people) {
        if (p.placed) {
            g_Model.maxX = std::max(g_Model.maxX, p.x + BOX_WIDTH);
            g_Model.maxY = std::max(g_Model.maxY, p.y + BOX_HEIGHT);
        }
    }
    g_Model.maxX += 50;
    g_Model.maxY += 50;
}

// -----------------------------------------------------------------------------
// RENDERING (Proses Menggambar ke Layar)
// -----------------------------------------------------------------------------

// Menggambar kotak informasi per orang
void DrawBox(HDC hdc, Person* p) {
    if(!p->placed) return;

    RECT rc = { p->x, p->y, p->x + BOX_WIDTH, p->y + BOX_HEIGHT };

    // Gambar Bayangan (Shadow)
    RECT rcShadow = rc; OffsetRect(&rcShadow, 4, 4);
    HBRUSH hShadow = CreateSolidBrush(RGB(220, 220, 220));
    FillRect(hdc, &rcShadow, hShadow);
    DeleteObject(hShadow);

    // Tentukan warna berdasarkan jenis kelamin
    COLORREF bgCol = (p->gender == L"F" || p->gender == L"f") ? COL_BOX_FEM : COL_BOX_MALE;
    HBRUSH hBg = CreateSolidBrush(bgCol);
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);

    // Gambar bingkai hitam
    FrameRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(hdc, TRANSPARENT);

    // Gambar Nama (Font Tebal/Bold)
    HFONT hFontBold = CreateFont(16, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, DEFAULT_QUALITY, 0, L"Segoe UI");
    {
        GdiObj font(hdc, hFontBold);
        RECT rcText = rc; rcText.bottom -= BOX_HEIGHT/2;
        DrawText(hdc, p->name.c_str(), -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    DeleteObject(hFontBold);

    // Gambar Peran/Role (Font Normal)
    HFONT hFontNorm = CreateFont(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, DEFAULT_QUALITY, 0, L"Segoe UI");
    {
        GdiObj font(hdc, hFontNorm);
        RECT rcText = rc; rcText.top += BOX_HEIGHT/2;
        DrawText(hdc, p->role.c_str(), -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    DeleteObject(hFontNorm);
}

// Menggambar garis penghubung antar anggota keluarga
void DrawConnectors(HDC hdc) {
    HPEN hPenStd = CreatePen(PS_SOLID, 1, COL_LINE);       // Pena untuk garis solid
    HPEN hPenEx = CreatePen(PS_DOT, 1, COL_LINE_EX);      // Pena untuk garis putus-putus (mantan)
    GdiObj pen(hdc, hPenStd);

    for (auto& p : g_Model.people) {
        if (!p.placed) continue;

        POINT pCenter = { p.x + BOX_WIDTH/2, p.y + BOX_HEIGHT/2 };
        POINT pBottom = { p.x + BOX_WIDTH/2, p.y + BOX_HEIGHT };

        // 1. Gambar garis ke pasangan
        for (int spId : p.spouses) {
            if (spId > p.id) { // Gambar sekali saja (mencegah double line)
                Person* sp = g_Model.Get(spId);
                if (sp && sp->placed) {
                    bool isEx = (p.exSpouses.count(spId) > 0);
                    POINT spCenter = { sp->x + BOX_WIDTH/2, sp->y + BOX_HEIGHT/2 };

                    HGDIOBJ oldPen = SelectObject(hdc, isEx ? hPenEx : hPenStd);
                    MoveToEx(hdc, pCenter.x, pCenter.y, NULL);
                    LineTo(hdc, spCenter.x, spCenter.y); // Garis horizontal antar pasangan
                    SelectObject(hdc, oldPen);

                    // Ambil daftar anak dari pasangan ini
                    int f = (p.gender == L"M") ? p.id : spId;
                    int m = (p.gender == L"M") ? spId : p.id;
                    auto kids = GetChildren(f, m);

                    if (!kids.empty()) {
                        // Tarik garis turun dari tengah-tengah pasangan
                        int midX = (pCenter.x + spCenter.x) / 2;
                        MoveToEx(hdc, midX, pCenter.y, NULL);
                        LineTo(hdc, midX, pCenter.y + BOX_HEIGHT/2 + 15);

                        int minKidX = 100000, maxKidX = -100000;
                        for(int kId : kids) {
                             Person* k = g_Model.Get(kId);
                             if(k && k->placed) {
                                 int kCx = k->x + BOX_WIDTH/2;
                                 if(kCx < minKidX) minKidX = kCx;
                                 if(kCx > maxKidX) maxKidX = kCx;
                             }
                        }

                        // Jika anak > 1, buat garis horizontal (fork) untuk menghubungkan semua anak
                        if (kids.size() == 1) {
                             LineTo(hdc, midX, pBottom.y + V_GAP);
                        } else {
                             MoveToEx(hdc, minKidX, pCenter.y + BOX_HEIGHT/2 + 15, NULL);
                             LineTo(hdc, maxKidX, pCenter.y + BOX_HEIGHT/2 + 15);

                             // Tarik garis vertikal ke masing-masing anak
                             for(int kId : kids) {
                                 Person* k = g_Model.Get(kId);
                                 if(k && k->placed) {
                                     MoveToEx(hdc, k->x + BOX_WIDTH/2, pCenter.y + BOX_HEIGHT/2 + 15, NULL);
                                     LineTo(hdc, k->x + BOX_WIDTH/2, k->y);
                                 }
                             }
                        }
                    }
                }
            }
        }

        // 2. Garis untuk anak dari orang tua tunggal
        int f = (p.gender == L"M") ? p.id : 0;
        int m = (p.gender == L"F") ? p.id : 0;
        auto singleKids = GetChildren(f, m);
        if(!singleKids.empty()) {
             MoveToEx(hdc, pBottom.x, pBottom.y, NULL);
             LineTo(hdc, pBottom.x, pBottom.y + 15);
             for(int kId : singleKids) {
                 Person* k = g_Model.Get(kId);
                 if(k && k->placed) {
                     MoveToEx(hdc, pBottom.x, pBottom.y + 15, NULL);
                     LineTo(hdc, k->x + BOX_WIDTH/2, k->y);
                 }
             }
        }
    }
    DeleteObject(hPenStd);
    DeleteObject(hPenEx);
}

// -----------------------------------------------------------------------------
// WINDOW PROCEDURE (Logika Interaksi Jendela)
// -----------------------------------------------------------------------------
int xScroll = 0, yScroll = 0; // Posisi scroll saat ini

// Update status dan range scrollbar berdasarkan luas pohon
void UpdateScrollBars(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    SCROLLINFO si = { sizeof(SCROLLINFO), SIF_ALL };

    si.nMax = g_Model.maxY; si.nPage = rc.bottom; si.nPos = yScroll;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

    si.nMax = g_Model.maxX; si.nPage = rc.right; si.nPos = xScroll;
    SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
}

// Fungsi pengolah pesan dari sistem operasi Windows
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: // Saat jendela baru dibuat
            LoadData();
            RecalculateLayout();
            SetTimer(hwnd, 1, 1000, NULL); // Timer 1 detik untuk cek update file otomatis
            break;

        case WM_TIMER: // Kejadian setiap detik (cek file)
            {
                FILETIME oldT = g_Model.lastWriteTime;
                LoadData();
                // Jika file berubah, refresh tampilan
                if (CompareFileTime(&oldT, &g_Model.lastWriteTime) != 0) {
                    RecalculateLayout();
                    UpdateScrollBars(hwnd);
                    InvalidateRect(hwnd, NULL, TRUE); // Memicu WM_PAINT
                }
            }
            break;

        case WM_SIZE: // Saat jendela di-resize oleh user
            UpdateScrollBars(hwnd);
            break;

        case WM_VSCROLL: // Saat scrollbar vertikal digeser
            {
                SCROLLINFO si = { sizeof(SCROLLINFO), SIF_ALL };
                GetScrollInfo(hwnd, SB_VERT, &si);
                int oldY = yScroll;
                switch(LOWORD(wParam)) {
                    case SB_LINEUP: yScroll -= 10; break;
                    case SB_LINEDOWN: yScroll += 10; break;
                    case SB_PAGEUP: yScroll -= si.nPage; break;
                    case SB_PAGEDOWN: yScroll += si.nPage; break;
                    case SB_THUMBTRACK: yScroll = HIWORD(wParam); break;
                }
                if (yScroll < 0) yScroll = 0;
                if (yScroll > g_Model.maxY) yScroll = g_Model.maxY;
                if (yScroll != oldY) {
                    SetScrollPos(hwnd, SB_VERT, yScroll, TRUE);
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }
            break;

        case WM_HSCROLL: // Saat scrollbar horizontal digeser
            {
                SCROLLINFO si = { sizeof(SCROLLINFO), SIF_ALL };
                GetScrollInfo(hwnd, SB_HORZ, &si);
                int oldX = xScroll;
                switch(LOWORD(wParam)) {
                    case SB_LINELEFT: xScroll -= 10; break;
                    case SB_LINERIGHT: xScroll += 10; break;
                    case SB_PAGELEFT: xScroll -= si.nPage; break;
                    case SB_PAGERIGHT: xScroll += si.nPage; break;
                    case SB_THUMBTRACK: xScroll = HIWORD(wParam); break;
                }
                if (xScroll < 0) xScroll = 0;
                if (xScroll > g_Model.maxX) xScroll = g_Model.maxX;
                if (xScroll != oldX) {
                    SetScrollPos(hwnd, SB_HORZ, xScroll, TRUE);
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }
            break;

        case WM_PAINT: // Proses menggambar ke jendela
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps); // Mulai proses gambar
                RECT rc; GetClientRect(hwnd, &rc);

                // DOUBLE BUFFERING: Gambar ke memori dulu baru ke layar agar tidak berkedip (flicker)
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBM = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
                HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

                // Gambar latar belakang canvas
                HBRUSH bg = CreateSolidBrush(COL_CANVAS);
                FillRect(memDC, &rc, bg);
                DeleteObject(bg);

                // Terapkan Transformasi Scroll (geser posisi gambar)
                int savedDC = SaveDC(memDC);
                SetGraphicsMode(memDC, GM_ADVANCED);
                XFORM xform = { 1.0f, 0, 0, 1.0f, (float)-xScroll, (float)-yScroll };
                SetWorldTransform(memDC, &xform);

                // Gambar seluruh elemen (Garis dulu baru kotak agar kotak menimpa garis)
                DrawConnectors(memDC);
                for (auto& p : g_Model.people) DrawBox(memDC, &p);

                RestoreDC(memDC, savedDC);
                // Salin dari memori ke layar utama
                BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

                SelectObject(memDC, oldBM); DeleteObject(memBM); DeleteDC(memDC);
                EndPaint(hwnd, &ps); // Selesai proses gambar
            }
            break;

        case WM_DESTROY: PostQuitMessage(0); break; // Tutup aplikasi
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// -----------------------------------------------------------------------------
// MAIN ENTRY (Titik Awal Program)
// -----------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Membuka Konsol Debug untuk melihat log saat aplikasi berjalan
    AllocConsole();
    FILE* fp; freopen_s(&fp, "CONOUT$", "w", stdout);
    std::cout << "Family Tree Debugger Started...\n";

    // Registrasi Kelas Jendela
    const wchar_t CLASS_NAME[] = L"FamilyTreeClass";
    WNDCLASS wc = { };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    RegisterClass(&wc);

    // Membuat Jendela Utama
    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, L"Family Tree Viewer",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) return 0;

    // Membuat file CSV contoh jika file tidak ditemukan di folder aplikasi
    std::ifstream check(DATA_FILE_A);
    if (!check.good()) {
        std::ofstream out(DATA_FILE_A);
        out << "ID,Name,Role,Gender,FatherID,MotherID,SpouseID\n";
        out << "1,Grandpa,Root,M,0,0,2\n";
        out << "2,Grandma,Root,F,0,0,1\n";
        out.close();

        char buf[MAX_PATH]; GetCurrentDirectoryA(MAX_PATH, buf);
        std::cout << "[INIT] Created file at: " << buf << "\\" << DATA_FILE_A << "\n";
    }
    check.close();

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Message Loop: Menunggu input dari pengguna (klik, ketik, dll)
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
