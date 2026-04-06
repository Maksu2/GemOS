# GemOS Status Report
> Stan na: 30 Stycznia 2026

---

## ✅ Gotowe (Production Ready)

### Jądro i Infrastruktura
- **Bootloader** – Dwuetapowy (Stage1 + Stage2), ładowanie kernela z dyskietki
- **Zarządzanie pamięcią** – Heap z `kalloc`/`kfree`, 12MB dla systemu
- **Przerwania (ISR/IRQ)** – Obsługa PIC, PIT (1000Hz), klawiatura, mysz
- **Sterownik ATA PIO** – Zapis/odczyt sektorów na dysk twardy
- **Port szeregowy** – Debug output przez `serial_print`

### System Plików (GemFS v2)
- Hierarchiczna struktura katalogów z `parent_id`
- Obsługa plików i folderów (`GEMFS_TYPE_FILE`, `GEMFS_TYPE_DIR`)
- Persistencja na osobnym dysku (`data.img`)
- API: `gemfs_create`, `gemfs_create_dir`, `gemfs_read`, `gemfs_write`, `gemfs_find`

### Grafika i UI
- **VBE Driver** – 1920x1080x32 z double buffering
- **Font Engine** – TrueType rasterizer z cache, antyaliasing
- **Window Manager** – Z-order, fokus, przeciąganie, minimalizacja, maksymalizacja
- **Top Bar** – Menu systemowe (GemOS, Apps), zegar
- **Dock** – Pasek z otwartymi oknami
- **Menu System** – Dropdowny, nawigacja klawiaturą, obsługa kliknięć

### Aplikacje
| Aplikacja | Status | Funkcje |
|-----------|--------|---------|
| **About GemOS** | ✅ | Splash screen z info |
| **Terminal** | ✅ | Podstawowe wyjście tekstowe |
| **Text Editor** | ✅ | Edycja, Save/Open, File Picker, rozszerzenie `.gemtext` |
| **File Explorer** | ✅ | Grid view, nawigacja folderów, tworzenie folderów, asocjacje plików |

---

## 🔧 W Najbliższych Planach (Next Sprint)

### File Explorer Rozszerzenia
- [ ] Drag & Drop (przenoszenie plików między folderami) – Kod stub istnieje, potrzebuje `gemfs_move`
- [ ] Ikony per typ pliku (zamiast kolorowych kwadratów)
- [ ] Kontekstowe menu (PPM: usuń, zmień nazwę)
- [ ] Odświeżanie widoku po zmianach

### GemFS v3
- [ ] `gemfs_move(src_id, dest_parent_id)`
- [ ] `gemfs_delete(id)`
- [ ] `gemfs_rename(id, new_name)`
- [ ] Metadata: data modyfikacji, rozmiar pliku

### UX Polish
- [ ] Podwójne kliknięcie z debounce (zamiast "klik = select, drugi klik = open")
- [ ] Kursor zmienia się nad aktywnymi elementami
- [ ] Animacje przejść (fade in/out okien)

---

## 🚀 Długoterminowe (Backlog)

### Nowe Aplikacje
- **Calculator** – Kalkulator z GUI
- **Paint** – Prosty edytor graficzny
- **System Settings** – Zmiana tapety, rozdzielczości, kolorów
- **Image Viewer** – Przeglądarka BMP/PNG

### Infrastruktura
- **Wielozadaniowość** – Scheduler, procesy, przełączanie kontekstu
- **Networking** – NE2000/RTL8139, TCP/IP stack
- **Dźwięk** – AC'97/SB16 driver, format WAV
- **USB** – UHCI/OHCI, klawiatura/mysz USB

### System Plików
- **FAT32** – Kompatybilność z pendrive'ami
- **Partycje** – MBR/GPT parsing
- **Montowanie** – `/mnt/usb`, `/mnt/cdrom`

### GUI Zaawansowane
- **Compositor** – Alpha blending, cienie, blur
- **Theming** – Zmiana kolorów, czcionek, stylów okien
- **Multi-monitor** – Obsługa wielu ekranów
- **HiDPI** – Skalowanie UI (już częściowo: `ui_scale`)

---

## 📊 Metryki Projektu

| Metryka | Wartość |
|---------|---------|
| Linie kodu C | ~15,000 |
| Linie ASM | ~1,500 |
| Rozmiar kernela | ~420 KB |
| Czas startu | <2s |
| Rozdzielczość | 1920x1080 |
| FPS UI | ~60 |

---

*GemOS – System operacyjny tworzony od zera, bez magii, z architekturą przed fetyszem funkcji.*
