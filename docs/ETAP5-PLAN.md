# Etap 5 — plan prac na podstawie kodu (październik 2026)

Plan reszty etapu 5 z [AUDIT-2026-09.md](AUDIT-2026-09.md) §8.2. Powstał z analizy kodu na `main` po etapie 3 (`d1e206f`); numery linii dotyczą tej wersji. Każdy punkt to osobny PR: commit na krok, testy w harnessie, AGENTS.md na końcu.

## Kolejność

1. Przycinanie tekstu do `clip_rect` (warunek dla punktu 4).
2. Okna mieszczą się na ekranie.
3. Siatka konsoli: każdy znak w swojej komórce.
4. Render bez pełnej klatki przy ruchu myszy, potem lista uszkodzonych prostokątów.
5. Explorer uruchamia `.ELF`; „Shutdown” działa albo znika.

## 1. Przycinanie tekstu — zrobione

Tekst respektuje `clip_rect` (`aa_blend_pixel`), test w autoteście. Zewnętrzny `bound_rect` przyjdzie z punktem 4.


- `aa_blend_pixel` sprawdza tylko granice ekranu (`kernel/font/aa.c:64`), a nie `clip_rect`. Tekst może więc wyjść poza okno i zostać zamalowany przez okno wyżej dopiero przy pełnej klatce.
- `gfx_set_clip`/`gfx_reset_clip` nadpisują przycięcie (`kernel/gfx/context.c:17-36`), a WM woła je dla każdego okna (`kernel/gui/wm/wm.c:227,243,303`). Potrzebny jest zewnętrzny `bound_rect` w kontekście, z którym obie funkcje robią część wspólną. Ten sam mechanizm posłuży potem za prostokąt uszkodzenia.
- Test: tekst przy krawędzi okna nie zmienia pikseli poza oknem (zrzut ekranu, obszar tuż za ramką).

## 2. Okna na ekranie — zrobione

Rozmieszczanie, kaskada, ograniczenia przeciągania, puszczenie nad topbarem i maksymalizacja jak niżej; testy w smoke. Bez syscalla rozmiaru konsoli (zmiana ABI do decyzji): przy 1080p UTEXTEDIT mieści się w (120,28).


Dziś nie ma żadnego rozmieszczania: okno otwiera się tam, gdzie wskaże wołający (`kernel/gui/window/window.c:9-31`, `kernel/gui/wm/wm.c:147-162`). Każda konsola otwiera się w (120,100) (`kernel/console.c:515`), więc kolejne leżą dokładnie na sobie.

- **Liczby przy 1920×1080** (skala 2, ekran logiczny 960×540):
  - topbar: y 0–27,
  - dock: y 492–539, rysowany pod oknami, ale klik w swoim pasie łapie pierwszy (`kernel/ui/dock/dock.c:144,180`),
  - obszar roboczy: 960×464,
  - komórka konsoli: 8×15, wysokość okna = wiersze·15 + 16 + 26 (`console.c:24-27,512-513`).
- **Okna, które dziś się nie mieszczą:**
  - UTEXTEDIT 84×28 ma 688×462 i przy y=100 kończy się na 562, więc wiersz 27 zaczyna się na 540, cały poza ekranem. Przy y=28–30 by się zmieścił; 28 wierszy to maksimum przy 1080p.
  - UTERM i Explorer kończą się pod dockiem.
- **Przeciąganie:**
  - Jedyne ograniczenie to y ≥ 28 (`kernel/gui/wm/wm.c:437-439`). Pasek tytułu da się upuścić w pasie docku, a wtedy nie da się go już złapać.
  - Błąd: puszczenie przycisku nad topbarem zjada `topbar.c:118-144`, więc okno dalej jedzie za myszą.
  - Maksymalizacja ignoruje dock (`kernel/gui/wm/wm.c:390-395`).
- **Program nie zna rozmiaru swojej konsoli:**
  - `console_open` zwraca tylko uchwyt, a `console_present` z innym rozmiarem kończy się EINVAL (`console.c:596-598`). UTEXTEDIT wtedy wychodzi z kodem 3.
  - Obcinanie nadmiarowych wierszy zamiast odrzucania ramki byłoby bezpieczne: bufor ma 96×32, a rysowanie już kończy się na wysokości okna (`console.c:328-333`).

**Projekt:**
- `wm_get_work_area()` i `wm_place_window()` w `wm_add_window`:
  - okno dociskane do obszaru roboczego (za wysokie: y=28),
  - kaskada +24/+24, gdy lewy górny róg innego okna jest w promieniu 8 px,
  - linia logu `[WM] Placed x= y= w= h=` bez tytułu, bo tytuł pochodzi od programu i mógłby udawać znaczniki logu.
- **Przeciąganie:** y w [28, 465], x w [64−w, 896]; puszczenie myszy zawsze dochodzi do WM. Maksymalizacja i przywracanie biorą obszar roboczy.
- **Wymiary:** pasek tytułu i ramka zdefiniowane w jednym miejscu (`window.h`; dziś `kernel/gui/wm/wm.c:29-30`, `window.c:6-7` i literał 26 w `console.c:513`). Wzór wysokości konsoli doliczy 2 px ramki.
- **Za duża konsola dostaje mniej wierszy, nie mniejsze komórki:**
  - `console_open` przycina wiersze do (wysokość obszaru − 44)/15,
  - `console_present` obcina nadmiar zamiast EINVAL.
  
  Żeby program wiedział o mniejszym rozmiarze, potrzebny byłby nowy syscall `SYS_console_get_size`. **To zmiana ABI, do decyzji.** Bez niego programy działają, a UTEXTEDIT 84×28 i tak mieści się przy y=28.
- **Testy:**
  - `tools/smoke.py` ma na sztywno (120,100) w `:139,150,163-167,178-183,189`; te punkty trzeba liczyć z logu `[WM] Placed`.
  - Każde `[WM] Placed` mieści się w obszarze roboczym, także przy 1280×800 z macierzy.
  - Wiersz 27 UTEXTEDIT jest narysowany, a piksel docku (400,500) zostaje #202020 przy otwartym edytorze.
  - Drugie okno ABOUT jest przesunięte o 24.
  - Pasek przeciągnięty do (480,539) kończy z y ≤ 465.
  - Po puszczeniu nad topbarem okno nie jedzie dalej za myszą.

## 3. Siatka konsoli

**Dziś:**
- `console_render_surface` (`kernel/console.c:311-380`) łączy sąsiednie komórki o tym samym stylu w jeden ciąg i rysuje go jednym `font_draw_text`. Na siatce 8 px ląduje więc tylko początek ciągu. Tryb starszy rysuje całe linie (`console.c:302-308`).
- Font Inter przesuwa pióro o szerokość glifu: przy 12 px spacja ma 3 px, `-` 5, `i` 2, `W` 11, a komórka 8. Przykłady z UTEXTEDIT:
  - „Untitled” w nagłówku, przeznaczone na x=592, ląduje ok. 244,
  - po wpisaniu „second” tekst kończy się na 44, a kursor stoi na 64,
  - 72 kreski ramki pytania zajmują 360 px zamiast 576 (stąd brak prawej krawędzi).
- Kursor (podkreślenie 7×2, `console.c:370-376`) już jest na siatce.

**Projekt:**
- **Font:** w `kernel/gfx/font/font.c` wydzielić z `font_draw_text` (l. 69-185) pobranie glifu z cache i rysowanie. Wpisy cache (`kernel/font/font_cache.h:10-20`) mają już szerokość, przesunięcia i bitmapę. Dodać `font_draw_cell(ctx, x, y, cell_w, ch, size, color)`, które centruje glif w komórce w pikselach fizycznych (przy skali 2 współrzędne logiczne nie trafią w nieparzysty piksel).
- **Konsola:** w `console.c` jedno wywołanie na komórkę w `x + col*8`; puste komórki i znaki spoza 0x20–0x7E pomijać. `console_present` kopiuje komórki bez sprawdzania, a znak sterujący narysowałby pusty prostokąt, a `\n` przeniósłby resztę do nowej linii.
- **UTEXTEDIT:** ramka pytania dostaje z powrotem prawą krawędź (`userland/textedit/textedit_render.c:143-155`).
- **Koszt:** do 96×32 = 3072 wyszukań w cache na klatkę, a mieszanie pikseli jest tej samej skali co dziś. Cache ma 512 miejsc, czyli ok. 4 rozmiary × 95 znaków ASCII, więc po pierwszej klatce nic nie jest rasteryzowane od nowa.
- **Brzegi:** szerokie glify (`%@MOQWmw`, przy skali 2 też `CDGHNU`) wystają do ok. 1,75 px poza komórkę, co jest do przyjęcia. Glif, którego nie da się wczytać, nie trafia do cache i jest próbowany w każdej klatce (`font.c:183-185`).
- **Test:**
  - Komórka (r,c) zaczyna się w (129+8c, 135+15r) logicznie; `tools/smoke.py:179` ma 136, czyli 1 px za nisko.
  - Komórka „ma tusz”, jeśli w [x+2,x+6)×[y+1,y+12) jest piksel inny niż 0x111111 (`Image.logical_area`, `smoke.py:378`).
  - Po otwarciu edytora wiersz 0 ma tusz dokładnie w kolumnach 2–5, 7–12 i 74–81.
  - W zrzucie z pytaniem wiersze 8 i 12 mają tusz w kolumnach 6–77, a w 78–83 nie.
  - Oba sprawdzenia dziś padają, a po poprawce przechodzą.

## 4. Render: najpierw kursor, potem uszkodzone prostokąty

Dziś każdy ruch myszy to pełna klatka: gradient tła 8,3 MB, wszystkie okna i kopia 8,3 MB do VRAM (`kernel/kernel.c:330,370-393`), co 16–20 ms, póki mysz się rusza. Aplikacje nie dostają ruchu myszy (`kernel/gui/wm/wm.c:429-442`), więc bez menu i przeciągania zmienia się tylko kursor.

- **Krok 1 — sam kursor.** Ruch myszy ustawia `cursor_moved`, a nie `pending_redraw`. Zmiany podświetlenia menu (`kernel/ui/menu.c:180-202`) i przeciąganie okna (`kernel/gui/wm/wm.c:430-441`) dalej proszą o klatkę. Klatka „tylko kursor” niczego nie rysuje: kopiuje z backbuffera do VRAM stary i nowy prostokąt kursora (nowa funkcja `present_rect`, wierszami) i rysuje kursor w VRAM. Backbuffer zostaje bez kursora, więc sam jest „save-under”.
- **Krok 2 — lista uszkodzeń.** `gui_damage(rect)`: około 16 prostokątów, scalanych; przepełnienie albo samo `kernel_request_redraw()` oznacza pełną klatkę. Zgłaszają:
  - konsola: obszar klienta okna,
  - WM przy fokusie, dodaniu, usunięciu, minimalizacji i przeciąganiu: stara i nowa ramka z cieniem 4 px (`kernel/gui/wm/wm.c:228`),
  - menu, dock i topbar.
  
  Render z przycięciem do uszkodzenia pomija okna, które go nie przecinają.
- **Pułapki:**
  - Zegar w topbarze (`kernel/gui/topbar/topbar.c:100`), Log Viewer (`apps/terminal/terminal.c:52`) i `explorer_refresh` (`apps/explorer/explorer.c:136`) czytają stan dopiero przy rysowaniu, więc same nie zgłoszą uszkodzenia.
  - Przy page flip BGA tylna strona ma klatkę N−2 (`drivers/vbe.c:89`). Kopiować trzeba uszkodzenie(N) ∪ uszkodzenie(N−1), a pierwsza klatka jest pełna. Ścieżka `memcpy` (`vbe.c:85`) potrzebuje tylko uszkodzenia(N).
- **Pomiar i test:**
  - Co ok. 2 s linia `[GFX] frames= full= cursor= partial= avg_ms= max_ms=` z czasem `kernel.c:370-393`.
  - Smoke przeciąga kursor (`Pointer.move_to`, `tools/smoke.py:443`) i sprawdza, że klatek pełnych prawie nie ma.
  - Tło to gradient zależny tylko od wiersza (`kernel/gui/desktop.c:6-7`), więc smugi po kursorze da się wykryć w zrzucie ekranu.
  - Test „ducha” kursora na drugiej stronie BGA: ruch z A do B, potem uszkodzenie gdzie indziej (menu) i dwa zrzuty.
  - Powtórzyć na wariancie macierzy `vga-4M` (ścieżka `memcpy`).

## 5. Explorer i Shutdown

### Explorer uruchamia programy

**Dziś:**
- Pierwsze kliknięcie zaznacza element, a kliknięcie zaznaczonego go otwiera (`apps/explorer/explorer.c:250-279`).
- Folder otwiera się na miejscu; `.txt` i `.gemtext` idą do UTEXTEDIT przez `app_open_with_file` (`:266`).
- Inne pliki są po cichu pomijane, a sprawdzanie rozszerzenia rozróżnia wielkość liter (`strends`, `:48`).
- Start programu z pełnej ścieżki GemFS już działa (autotest robi to z `/fstest/FILETEST.ELF`).

**Projekt:**
- W gałęzi otwierania: `.ELF` bez względu na wielkość liter (jak `name_is_program` w `kernel/fs/gemfs.c`), potem `process_spawn_user_from_file(path)` i zdjęcie zaznaczenia, żeby kolejne kliknięcia nie uruchamiały następnych kopii.
- Błąd startu albo nieznany typ: czerwony tekst w pasku Explorera, w miejscu ścieżki (`:137-139`), a nie tylko linia w logu.
- `process_find_embedded_program` (`kernel/process.c:74`) ma pomijać początkowe `/`. Inaczej `"/UTERM.ELF"` traci zapasową kopię z obrazu jądra.

**Pułapki:**
- Program dostaje tylko `argv[0]`.
- Program bez okna (USRSMOKE.ELF kończy z 42) nie daje użytkownikowi żadnego sygnału.
- Limity: 15 zadań użytkownika i 8 okien konsoli.
- Podwójne kliknięcie mierzone czasem zepsułoby `--editor`, bo jego kliknięcia dzielą ok. 0,8 s.

**Test:** jak `open_from_explorer` (`tools/smoke.py:990-1001`). Dwa kliknięcia w ABOUT.ELF na liście, potem sprawdzenia z `run_app` dla nazwy `/ABOUT.ELF`. USRSMOKE.ELF sprawdza przypadek bez okna: `USRSMOKE OK` i `exit=42` w logu.

### Shutdown

**Dziś:** `action_exit` tylko loguje `[ACTION] Shutdown Request` (`kernel/gui/topbar/topbar.c:25,42`).

**Projekt:**
- `action_exit` ustawia flagę, a pętla w `kernel_main` ją obsługuje, bo `backbuffer` i `use_page_flip` są jej zmiennymi lokalnymi.
- **GemFS:** `gemfs_shutdown()` bierze blokadę i jej nie oddaje. Gdy proces jest w połowie długiego zapisu, zadanie GUI przekaże mu CPU do końca operacji (mechanizm z etapu 3). Skończone zapisy już są na dysku: każdy kończy się FLUSH CACHE, a bitmapa trafia na dysk przed zdjęciem blokady.
- **Nic więcej nie trzeba zapisywać:** log jest tylko w RAM, a port szeregowy pisze od razu. Na sprzęcie trzeba jeszcze poczekać na bit TEMT UART-u. Niezapisany tekst w edytorze przepada.
- **`power_off()` w nowym `kernel/power.c`:**
  - linia `[POWER] Powering off`, `cli`, `outw(0x604, 0x2000)` (nowsze QEMU), chwila przerwy, `outw(0xB004, 0x2000)` (Bochs, starsze QEMU),
  - jeśli to nie zadziała: ekran „It is now safe to turn off”, linia w logu i pętla `hlt`.
  
  Pełne ACPI (FADT, `\_S5`) później, bo tablice ACPI leżą zwykle powyżej 32 MB, których jądro nie mapuje.
- **Test:**
  - Harness startuje QEMU bez `-no-shutdown`, więc wyłączenie kończy QEMU. Nowy krok po `scan_log`: menu GemOS, Shutdown, `qemu.wait(30)` zwraca 0, w logu jest `[POWER]`, potem `mkgemfs check`.
  - Trwałość: zapis pliku w edytorze, od razu Shutdown, odczyt `mkgemfs cat` na hoście.
  - `--preboot` może kończyć Shutdownem zamiast `quit` z HMP.
  - Wariant macierzy z `-machine pc,acpi=off` sprawdza ekran zapasowy (nie sprawdzone, czy QEMU 8.2 wtedy ignoruje oba porty).
