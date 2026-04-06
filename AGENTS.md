# GemOS - Kompleksowa Dokumentacja Techniczna Systemu

GemOS to nowoczesny, minimalistyczny system operacyjny typu desktop, budowany od podstaw z myślą o architekturze x86 (32-bit). Projekt nie jest jedynie demonstracją techniczną, lecz pełnoprawnym systemem operacyjnym (WIMP), zaprojektowanym z naciskiem na solidność, estetykę i przewidywalność.

## 1. Filozofia i Główne Założenia

GemOS kieruje się zestawem rygorystycznych zasad, które definiują jego tożsamość:
- **System, nie demo**: Każda funkcja jest projektowana pod kątem realnego użytkowania, nie tylko efektu wizualnego.
- **Boring is Success**: Sukcesem jest system, który nie irytuje. Stabilność i przewidywalność są ważniejsze niż "wow-efekt".
- **Architektura przed funkcjami**: Poprawna struktura kodu i subsystemów ma priorytet nad szybkością wdrażania nowych opcji.
- **Estetyka Inżynierska**: Wygląd systemu nawiązuje do klasycznych interfejsów (Windows XP-7) – jest czytelny, techniczny i spójny.
- **Samodzielność (Self-hosting mindset)**: System budowany jest bez zewnętrznych zależności typu libc czy GRUB. Wszystko, od pierwszego bajtu sektora startowego, jest autorskie.

## 2. Status Projektu (Checklista)

### 2.1. Warstwa Bootloadera i Jądra
- [x] **Stage 1 (MBR)**: Znajdowanie i ładowanie Stage 2 z dysku.
- [x] **Stage 2 (Loader)**: GDT, A20, inicjalizacja VBE, wejście w Protect Mode.
- [x] **Kernel Entry**: Poprawne ustawienie stosu i skok do `kernel_main`.
- [x] **Zarządzanie Pamięcią**: Stabilny alokator sterty (`heap.c`).
- [x] **Paging MVP**: 32-bit legacy paging (4 KB), `CR3` + `CR0.PG`, identity mapping dla jądra, heapu i VBE LFB.
- [x] **Przerwania (IDT)**: Obsługa wyjątków procesora i sprzętowych IRQ.
- [x] **System Zdarzeń**: Kolejka zdarzeń (Event Queue) jako podstawa komunikacji.

### 2.2. Sterowniki (Drivers)
- [x] **VBE Graphics**: Obsługa Linear Framebuffer i Page Flippingu (BGA).
- [x] **Klawiatura PS/2**: Pełna mapa znaków, obsługa klawiszy modyfikujących.
- [x] **Mysz PS/2**: Obsługa ruchu względnego i przycisków.
- [x] **PIT/RTC**: Precyzyjny czas systemowy (1ms) i zegar czasu rzeczywistego.
- [x] **Serial UART**: Przesyłanie logów debugowania na port COM1.
- [x] **ATA/IDE**: Odczyt i zapis sektorów w trybie PIO.

### 2.3. System Graficzny i UI
- [x] **Rasteryzator TTF**: Silnik czcionek wektorowych z Anti-Aliasingiem.
- [x] **Menedżer Okien (WM)**: Zarządzanie oknami, focus, drag & drop.
- [x] **Komponenty Desktopu**: Funkcjonalny Topbar, Dock oraz system Menu.
- [x] **GemFS**: Stabilny odczyt i zapis plików w autorskim formacie.

### 2.4. Plany i Rozwój (To-Do)

- [x] **Multitasking**: Preemptive round-robin scheduler oparty na IRQ0 (PIT, 1ms tick, kwant 10ms per-task).
- [ ] **Ring 3 / Izolacja procesów**: Osobne przestrzenie adresowe i uprawnienia użytkownika.
- [ ] **Stos Sieciowy**: Implementacja TCP/IP oraz sterownika RTL8139.
- [ ] **Obsługa USB**: Sterowniki dla nowoczesnych urządzeń wejścia.

## 3. Architektura i Start Systemu

GemOS działa w 32-bitowym trybie chronionym (Protected Mode) procesorów x86. System wykorzystuje obecnie minimalny 32-bit legacy paging z zachowaniem pojedynczej przestrzeni adresowej jądra. Kluczowe regiony są mapowane 1:1 (identity mapping), dzięki czemu system zachowuje dotychczasową prostotę startu, ale działa już za MMU.

### 2.1. Proces Bootowania
Proces startowy podzielony jest na dwa etapy przed przekazaniem kontroli do jądra:

1. **Stage 1 (MBR)**: Znajduje się w pierwszym sektorze (512 bajtów) dysku. Jego zadaniem jest:
   - Inicjalizacja stosu i rejestrów segmentowych.
   - Odczyt Stage 2 z kolejnych sektorów dysku (używając przerwania BIOS INT 13h, LBA).
   - Skok pod adres bazowy Stage 2 (zwykle 0x7C00 lub przesunięty).
2. **Stage 2 (Loader)**: Wykonuje krytyczne operacje przygotowawcze:
   - Włączenie linii A20 (dostęp do pełnej pamięci RAM powyżej 1MB).
   - Konfiguracja Globalnej Tabeli Deskryptorów (GDT) dla trybu 32-bitowego.
   - Inicjalizacja grafiki VBE (VESA BIOS Extensions) – wybór trybu (np. 1024x768x32).
   - Przejście w Protected Mode poprzez ustawienie bitu PE w rejestrze CR0.
   - Załadowanie jądra (`kernel.bin`) z dysku do pamięci pod adres 0x100000.
   - Skok do punktu wejścia jądra.

### 2.2. Kernel Entry
Jądro rozpoczyna bieg w `entry.S`. To tutaj następuje ostateczne ustawienie stosu jądra oraz wywołanie funkcji `kernel_main` w języku C. `entry.S` zapewnia również, że segmenty danych są poprawnie ustawione zgodnie z GDT zdefiniowaną w Stage 2.

## 4. Podsystemy Jądra

### 3.1. Zarządzanie Pamięcią (Heap)
Jądro posiada autorski mechanizm przydziału pamięci na stercie (`heap.c`). 
- **Alokacja**: System `kalloc` i `kfree` zarządza wolnymi blokami pamięci.
- **Układ**: Sterta rozpoczyna się zaraz za końcem sekcji BSS jądra (`__kernel_end`), która jest definiowana przez skrypt linkera.
- **Rozmiar**: Obecnie 24MB, co jest wartością wystarczającą do przechowywania wielu buforów graficznych Full HD oraz cache'u fontów.

### 3.2. Paging (Virtual Memory)
GemOS posiada obecnie minimalny subsystem pagingu (`kernel/memory/paging.c`) oparty o klasyczne 32-bit legacy paging x86.
- **Struktury**: `page_directory_t` i `page_table_t`, po 1024 wpisy typu `uint32_t`.
- **Rozmiar strony**: 4 KB.
- **Flagi**: `PAGE_PRESENT | PAGE_WRITABLE`.
- **Włączenie**: `paging_init()` buduje struktury, ładuje adres katalogu stron do `CR3` i ustawia bit `PG` w `CR0`.
- **Mapowanie**:
  - identity map `0x00000000 - 0x00400000` (kod jądra, stack, GDT, IDT, ISR, niska pamięć),
  - identity map całego kernel heap,
  - identity map 16MB regionu framebuffera VBE (np. `0xFD000000 - 0xFE000000`).
- **Lokalizacja struktur**: Page Directory i pula Page Tables są 4 KB aligned i umieszczone statycznie w sekcji `.bss`, przed początkiem heapu.
- **Zakres fazy**: brak jeszcze oddzielnych przestrzeni adresowych, user mode, swapu i demand paging.

### 3.3. Przerwania i ISR
Obsługa przerwań oparta jest na Tabeli Deskryptorów Przerwań (IDT).
- **IDT**: Tablica 256 wpisów definiujących procedury obsługi.
- **ISR (Interrupt Service Routines)**: Funkcje w `isr.c` i `interrupts.S`, które obsługują:
  - Wyjątki procesora (Divide Error, Page Fault, itp.).
  - Przerwania sprzętowe (IRQ) generowane przez kontroler PIC.
- **PIC (8259A)**: Programowalny kontroler przerwań jest remapowany tak, aby IRQ startowały od 0x20 i 0x28, co pozwala uniknąć konfliktów z zarezerwowanymi wyjątkami x86.

### 3.4. System Zdarzeń (Event System)
GemOS implementuje architekturę sterowaną zdarzeniami (Event-Driven). Centralna kolejka zdarzeń (`event.c`) gromadzi:
- `EVENT_KEY_PRESS/RELEASE`: Dane z klawiatury.
- `EVENT_MOUSE_MOVE/CLICK`: Dane z myszy.
- `EVENT_TIMER_TICK`: Sygnały z timera PIT (1ms).
Główna pętla w `kernel_main` pobiera zdarzenia i przekazuje je do odpowiednich handlerów w WM, Topbarze lub aplikacjach.

## 5. Sterowniki Sprzętowe (Drivers)

- **VBE (Video)**: Obsługuje tryby Linear Framebuffer. Korzysta z rozszerzeń BGA (Bochs Graphics Adaptor), jeśli są dostępne (np. w QEMU), co umożliwia sprzętowe przełączanie stron.
- **Keyboard (PS/2)**: Dekoduje skankody, wspiera klawisze modyfikujące (Shift, Ctrl, Alt) oraz mapy znaków.
- **Mouse (PS/2)**: Przetwarza pakiety 3-bajtowe (ruch X, Y, przyciski), zapewnia płynną obsługę kursora.
- **Serial (UART)**: Port COM1 używany do przesyłania logów tekstowych. Niezbędny przy debugowaniu braku obrazu.
- **PIT (8254)**: Programowalny timer ustawiony na częstotliwość 1000Hz (1 tick = 1ms). Podstawa czasu systemowego.
- **ATA (IDE)**: Obsługa dysków twardych w trybie PIO (Programmed I/O). Odczytuje i zapisuje sektory 512-bajtowe.
- **RTC (Real Time Clock)**: Odczytuje aktualną datę i godzinę z pamięci CMOS, z uwzględnieniem poprawek na strefy czasowe.

## 6. System Graficzny i Rendering

Podstawą GemOS jest wydajny pipeline renderujący, zoptymalizowany pod kątem braku akceleracji sprzętowej GPU.

### 5.1. Technika Triple/Double Buffering
Aby wyeliminować efekt "tearingu" i zapewnić 60 FPS:
1. **Backbuffer (RAM)**: Rendering odbywa się w buforze na stercie. Pisanie do RAM jest znacznie szybsze niż do VRAM przez magistralę PCI/ISA.
2. **Back-page (VRAM)**: Gotowa klatka jest kopiowana (MEMCPY) do niewidocznej strony pamięci wideo.
3. **Atomic Flip**: System instruuje emulator/kartę do zmiany wskaźnika wyświetlania na nową stronę podczas powrotu pionowego (V-Blank).

### 5.2. Kontekst GFX (`gfx_context_t`)
Struktura ta jest przekazywana do każdej funkcji rysunkowej. Zawiera:
- Wskaźnik do aktualnego bufora.
- Metadane o rozdzielczości i pitchu (szerokość linii w bajtach).
- Funkcje prymitywne: `rect_fill`, `line_draw`, `pixel_put` (z obsługą ARGB).

## 7. Silnik Fontów (Font Engine)

W GemOS "Fonty to silnik, nie zasoby". Oznacza to, że system nie korzysta z bitmap, lecz z matematycznego opisu glifów.

### 6.1. Architektura TrueType (TTF)
Jądro zawiera parser plików .ttf:
- **Tablice**: Odczytujemy `head`, `glyf`, `loca`, `cmap`, `maxp`.
- **Krzywe**: Glify są opisane jako serie punktów i krzywych Beziera drugiego stopnia (Quadratic Bezier).
- **Rasteryzacja Scanline**: Algorytm znajduje punkty przecięcia krzywych z każdą linią skanowania, wypełniając wnętrze kształtu.

### 6.2. Anti-Aliasing (AA) i Optymalizacja
- **Over-sampling**: Rendering odbywa się wewnętrznie w wyższej rozdzielczości, a następnie jest uśredniany, co daje gładkie krawędzie.
- **Font Cache**: Wyrenderowane glify (jako małe bitmapy alfa) są przechowywane w pamięci podręcznej dla konkretnego rozmiaru i stylu. To pozwala na renderowanie tekstu z prędkością bitmapy przy zachowaniu jakości wektorowej.

## 8. Window Manager (WM) i GUI

System okienkowy GemOS implementuje klasyczny model "Overlapping Windows".

### 7.1. Zarządzanie Oknami
- **Struktura `window_t`**: Przechowuje pozycję, wymiary, tytuł, oraz bufor zawartości.
- **Z-Order**: Lista okien jest sortowana. Okno na szczycie listy ma "focus" i otrzymuje zdarzenia klawiatury.
- **Decorations**: WM automatycznie rysuje ramki okien, przyciski zamknij/minimalizuj oraz paski tytułu.

### 7.2. Elementy UI
- **Desktop**: Obsługuje tło oraz skróty.
- **Topbar**: Pasek statusu z godziną, menu systemowym i indykatorami.
- **Dock**: Dynamiczny pasek na dole ekranu, pozwalający na szybkie przełączanie aplikacji. Animacje hover w docku są renderingiem w czasie rzeczywistym.

### 7.3. Propagacja Zdarzeń
Zdarzenie myszy przechodzi przez:
1. `menu_handle`: Czy kliknięto w otwarte menu?
2. `dock_handle`: Czy interakcja dotyczy doku?
3. `topbar_handle`: Czy kliknięto w pasek statusu?
4. `wm_handle`: Czy zdarzenie dotyczy konkretnego okna lub jego ramki (np. przeciąganie)?

## 9. Ekosystem Aplikacji

Aplikacje nie są osobnymi procesami w sensie Unixowym (ze względu na brak separacji przestrzeni adresowej użytkownika), lecz modułami jądra rejestrowanymi w `App Managerze`.

- **Terminal**: Wspiera podstawowe komendy: `ls`, `help`, `clear`, `version`.
- **Explorer**: Przeglądarka plików w systemie GemFS. Pozwala na nawigację po katalogach.
- **TextEdit**: Edytor tekstu z obsługą kursora tekstowego i zapisem na dysk.
- **About**: Wyświetla metryki systemu (użycie sterty, czas od uruchomienia, info o wersji).

## 10. System Plików GemFS

Autorski system plików zoptymalizowany pod kątem prostoty i bezpieczeństwa.
- **Struktura**: Superblok -> Tabela Inodów -> Bloki Danych.
- **Limit**: Wspiera pliki o wielkości do kilku megabajtów (na ten moment).
- **Zasady**: GemFS stawia na spójność metadanych; każda operacja zapisu jest weryfikowana.

## 11. Architektura Budowania (Build)

Projekt budowany jest przy użyciu `i686-elf-gcc`. Wybór cross-compilera jest kluczowy, aby uniknąć wpływu bibliotek systemowych hosta (np. macOS czy Windows) na kod jądra.

- **Linker API**: Skrypt `linker.ld` definiuje precyzyjnie:
  - Adres ładowania (0x100000).
  - Pozycję sekcji `.text` (kod).
  - Pozycję sekcji `.data` (dane zainicjalizowane).
  - Pozycję sekcji `.bss` (dane niezainicjalizowane).
  - Symbol `__kernel_end` używany przez heap manager.

## 12. Decyzje Projektowe (Design Decisions)

### 11.1. Minimalny Paging jako Etap Przejściowy
GemOS przeszedł z całkowicie płaskiego modelu pamięci do minimalnego pagingu jądra. Jest to świadomy etap pośredni: system korzysta już z MMU i struktur `Page Directory` / `Page Table`, ale nadal utrzymuje pojedynczą przestrzeń adresową i identity mapping kluczowych regionów. Taki model pozwala zachować stabilność istniejącego GUI i sterowników, a jednocześnie otwiera drogę do dalszych kroków: ochrony pamięci, Ring 3, ładowania ELF i osobnych przestrzeni procesów.

### 11.2. Wybór VBE jako Standardu Video
Vesa Bios Extensions 2.0+ to "najniższy wspólny mianownik" kart graficznych x86. Dzięki specyfikacji LFB (Linear Framebuffer), system uzyskuje bezpośredni dostęp do pamięci wideo bez konieczności przełączania banków pamięci. Jest to rozwiązanie "boring", ale niezawodne na prawdziwym sprzęcie z początku lat 2000 jak i w nowoczesnych emulatorach.

## 13. Dokumentacja Techniczna Komponentów

### 12.1. Zarządzanie Czasem (PIT i RTC)
Programowalny Interval Timer (PIT) generuje przerwanie IRQ0 co 1ms. W handlerze ISR32 inkrementowany jest licznik `tick_count`. Z kolei Real Time Clock (RTC) odczytywany jest co kilka sekund, aby zsynchronizować zegar systemowy z zegarem sprzętowym CMOS. Pozwala to na zachowanie precyzji czasu bez nadmiernego obciążania procesora częstymi odczytami portu I/O.

### 12.2. Algorytm Rasteryzacji Fontów
1. **Parsowanie TTF**: Odczytujemy tablicę `loca` i `glyf`.
2. **Krzywe Beziera**: Każda krzywa kwadratowa (P0, P1, P2) jest dzielona na krótkie odcinki proste (line segments) przy użyciu adaptacyjnego podziału.
3. **Scanline Table**: Dla każdej linii poziomej ekranu (Y) tworzona jest lista przecięć (X) ze wszystkimi odcinkami obrysu.
4. **Wypełnianie**: Wartości między parami przecięć są wypełniane kolorem (lub stopniem przezroczystości przy AA).

## 14. Technical FAQ

**Q: Jak system obsługuje czcionki polskie?**
A: Silnik TrueType wspiera standard Unicode. Jeśli plik font.ttf zawiera glify dla polskich znaków, zostaną one poprawnie zrasteryzowane i wyświetlone. Jądro GemOS domyślnie ładuje zestaw znaków Latin-Extended.

**Q: Czy GemOS wspiera dźwięk?**
A: Na obecnym etapie system nie posiada sterowników audio (AC97/HDA). Planowane jest dodanie sterownika PC Speaker dla sygnałów systemowych w wersji 1.1.

**Q: Jakie są minimalne wymagania?**
A: Procesor klasy Pentium II or higher, 32MB RAM, karta graficzna z obsługą VBE 2.0 (LFB). Zalecane 64MB RAM dla płynnego działania wielu aplikacji.

**Q: Jak debugować system?**
A: Najlepiej przez wyjście Serial (QEMU: `-serial stdio`) oraz podpinając GDB (`make debug`). Wszystkie krytyczne błędy (Panic) są logowane na port szeregowy z pełnym zrzutem rejestrów procesora.

## 15. Rozwój i Przyszłość GemOS

GemOS nie jest projektem ukończonym. To ewoluujący organizm techniczny. Najbliższe kamienie milowe to:

1. **Multitasking**: ~~Wprowadzenie planisty (scheduler)~~ **Zrealizowane** — preemptive round-robin scheduler (IRQ0, 10ms kwant per-task, `task_create()` API). Kolejny krok: priorytety tasków i `task_block()`/`task_unblock()`.
2. **Paging MVP**: **Zrealizowane** — 32-bit legacy paging, strony 4 KB, `CR3`, `CR0.PG`, identity mapping dla niskiej pamięci, heapu i framebuffera VBE.
3. **Dynamiczne Linkowanie**: Ładowanie plików ELF w czasie rzeczywistym z dysku.
4. **Obsługa USB**: Sterownik UHCI/EHCI dla nowoczesnych klawiatur i myszy.
5. **Sieć**: Implementacja stosu TCP/IP oraz podstawowego sterownika karty RTL8139.

## 16. Budowa i Uruchomienie (Tutorial)

### 15.1. Kompilacja
Aby zbudować system, wymagane jest środowisko Linux lub macOS z zainstalowanym cross-compilerem `i686-elf-gcc`.
```bash
make clean
make all
```
Wynikiem będzie plik `build/gemos.img` - gotowy obraz dysku.

### 15.2. Testowanie
Najszybszym sposobem na przetestowanie zmian jest QEMU:
```bash
make run
```
Dla głębokiej analizy:
```bash
make debug
```
Następnie w innym oknie:
```bash
i686-elf-gdb build/kernel.elf
target remote :1234
```

---
*Dokumentacja sporządzona dla systemu GemOS*
*Data aktualizacji: 6 kwietnia 2026*
*Wersja: 1.0-UX-FIX-1 (Golden Build)*
*Autor: Zespół GemOS (Antigravity Core)*

### Załącznik C: Mapa Pamięci Fizycznej i Kluczowych Mapowań

| Adres Start | Opis | Rozmiar |
|-------------|------|---------|
| 0x00000000  | IVT (BIOS Interrupt Vector Table) | 1KB |
| 0x00000500  | BIOS Data Area | 256 bytes |
| 0x00007C00  | Stage 1 Bootloader | 512 bytes |
| 0x00100000  | Kernel Code & Data | ~512KB |
| 0x0016F000  | Page Tables (`.bss`, 4KB aligned) | zależne od buildu |
| 0x0018F000  | Page Directory (`.bss`, 4KB aligned) | 4KB |
| 0x00199A80  | Kernel Heap (start za `__kernel_end`) | ~24MB |
| 0x0199A000  | Wolny obszar po końcu mapowanego heapu | Różne |
| 0xFD000000  | VBE Linear Framebuffer (Mapowany sprzętowo) | 16MB |

### Załącznik D: Skróty Klawiszowe Systemu

- `ALT + TAB`: Przełączanie okien (przyszłość)
- `ESC`: Zamknięcie aktualnego menu
- `PRINTSCREEN`: Zrzut ekranu do pamięci (debug)
- `F12`: Wywołanie systemowego okna "About"

---
Praca nad GemOS to dążenie do inżynierskiej perfekcji. Pamiętaj: **Boring is Success**. Każda linia kodu musi mieć uzasadnienie.
