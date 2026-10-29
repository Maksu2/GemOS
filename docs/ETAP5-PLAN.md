# Etap 5 — plan prac na podstawie kodu (październik 2026)

Plan reszty etapu 5 z [AUDIT-2026-09.md](AUDIT-2026-09.md) §8.2. Powstał z analizy kodu na `main` po etapie 3 (`d1e206f`); numery linii dotyczą tej wersji. Każdy punkt to osobny PR: commit na krok, testy w harnessie, AGENTS.md na końcu.

## Kolejność

1. Przycinanie tekstu do `clip_rect` (warunek dla punktu 4).
2. Okna mieszczą się na ekranie.
3. Siatka konsoli: każdy znak w swojej komórce.
4. Render bez pełnej klatki przy ruchu myszy, potem lista uszkodzonych prostokątów.
5. Explorer uruchamia `.ELF`; „Shutdown” działa albo znika.

## 1. Przycinanie tekstu

- `aa_blend_pixel` sprawdza tylko granice ekranu (`kernel/font/aa.c:64`), a nie `clip_rect`. Tekst może więc wyjść poza okno i zostać zamalowany przez okno wyżej dopiero przy pełnej klatce.
- `gfx_set_clip`/`gfx_reset_clip` nadpisują przycięcie (`kernel/gfx/context.c:17-36`), a WM woła je dla każdego okna (`kernel/gui/wm.c:227,243,303`). Potrzebny jest zewnętrzny `bound_rect` w kontekście, z którym obie funkcje robią część wspólną. Ten sam mechanizm posłuży potem za prostokąt uszkodzenia.
- Test: tekst przy krawędzi okna nie zmienia pikseli poza oknem (zrzut ekranu, obszar tuż za ramką).

## 2. Okna na ekranie

Dziś nie ma żadnego rozmieszczania: okno otwiera się tam, gdzie wskaże wołający (`kernel/gui/window.c:9-31`, `wm.c:147-162`). Każda konsola otwiera się w (120,100) (`kernel/console.c:515`), więc kolejne leżą dokładnie na sobie.

- **Liczby przy 1920×1080** (skala 2, ekran logiczny 960×540):
  - topbar: y 0–27,
  - dock: y 492–539, rysowany pod oknami, ale klik w swoim pasie łapie pierwszy (`kernel/ui/dock/dock.c:144,180`),
  - obszar roboczy: 960×464,
  - komórka konsoli: 8×15, wysokość okna = wiersze·15 + 16 + 26 (`console.c:24-27,512-513`).
- **Okna, które dziś się nie mieszczą:**
  - UTEXTEDIT 84×28 ma 688×462 i przy y=100 kończy się na 562, więc wiersz 27 zaczyna się na 540, cały poza ekranem. Przy y=28–30 by się zmieścił; 28 wierszy to maksimum przy 1080p.
  - UTERM i Explorer kończą się pod dockiem.
- **Przeciąganie:**
  - Jedyne ograniczenie to y ≥ 28 (`wm.c:437-439`). Pasek tytułu da się upuścić w pasie docku, a wtedy nie da się go już złapać.
  - Błąd: puszczenie przycisku nad topbarem zjada `topbar.c:118-144`, więc okno dalej jedzie za myszą.
  - Maksymalizacja ignoruje dock (`wm.c:390-395`).
- **Program nie zna rozmiaru swojej konsoli:**
  - `console_open` zwraca tylko uchwyt, a `console_present` z innym rozmiarem kończy się EINVAL (`console.c:596-598`). UTEXTEDIT wtedy wychodzi z kodem 3.
  - Obcinanie nadmiarowych wierszy zamiast odrzucania ramki byłoby bezpieczne: bufor ma 96×32, a rysowanie już kończy się na wysokości okna (`console.c:328-333`).

**Projekt:**
- `wm_get_work_area()` i `wm_place_window()` w `wm_add_window`:
  - okno dociskane do obszaru roboczego (za wysokie: y=28),
  - kaskada +24/+24, gdy lewy górny róg innego okna jest w promieniu 8 px,
  - linia logu `[WM] Placed x= y= w= h=` bez tytułu, bo tytuł pochodzi od programu i mógłby udawać znaczniki logu.
- **Przeciąganie:** y w [28, 465], x w [64−w, 896]; puszczenie myszy zawsze dochodzi do WM. Maksymalizacja i przywracanie biorą obszar roboczy.
- **Wymiary:** pasek tytułu i ramka zdefiniowane w jednym miejscu (`window.h`; dziś `wm.c:29-30`, `window.c:6-7` i literał 26 w `console.c:513`). Wzór wysokości konsoli doliczy 2 px ramki.
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

(Analiza w toku: rysowanie komórek konsoli w `kernel/console.c`.)

## 4. Render: najpierw kursor, potem uszkodzone prostokąty

Dziś każdy ruch myszy to pełna klatka: gradient tła 8,3 MB, wszystkie okna i kopia 8,3 MB do VRAM (`kernel/kernel.c:330,370-393`), co 16–20 ms, póki mysz się rusza. Aplikacje nie dostają ruchu myszy (`kernel/gui/wm.c:429-442`), więc bez menu i przeciągania zmienia się tylko kursor.

- **Krok 1 — sam kursor.** Ruch myszy ustawia `cursor_moved`, a nie `pending_redraw`. Zmiany podświetlenia menu (`kernel/ui/menu.c:180-202`) i przeciąganie okna (`wm.c:430-441`) dalej proszą o klatkę. Klatka „tylko kursor” niczego nie rysuje: kopiuje z backbuffera do VRAM stary i nowy prostokąt kursora (nowa funkcja `present_rect`, wierszami) i rysuje kursor w VRAM. Backbuffer zostaje bez kursora, więc sam jest „save-under”.
- **Krok 2 — lista uszkodzeń.** `gui_damage(rect)`: około 16 prostokątów, scalanych; przepełnienie albo samo `kernel_request_redraw()` oznacza pełną klatkę. Zgłaszają:
  - konsola: obszar klienta okna,
  - WM przy fokusie, dodaniu, usunięciu, minimalizacji i przeciąganiu: stara i nowa ramka z cieniem 4 px (`wm.c:228`),
  - menu, dock i topbar.
  
  Render z przycięciem do uszkodzenia pomija okna, które go nie przecinają.
- **Pułapki:**
  - Zegar w topbarze (`kernel/gui/topbar.c:100`), Log Viewer (`apps/terminal/terminal.c:52`) i `explorer_refresh` (`apps/explorer/explorer.c:136`) czytają stan dopiero przy rysowaniu, więc same nie zgłoszą uszkodzenia.
  - Przy page flip BGA tylna strona ma klatkę N−2 (`drivers/vbe.c:89`). Kopiować trzeba uszkodzenie(N) ∪ uszkodzenie(N−1), a pierwsza klatka jest pełna. Ścieżka `memcpy` (`vbe.c:85`) potrzebuje tylko uszkodzenia(N).
- **Pomiar i test:**
  - Co ok. 2 s linia `[GFX] frames= full= cursor= partial= avg_ms= max_ms=` z czasem `kernel.c:370-393`.
  - Smoke przeciąga kursor (`Pointer.move_to`, `tools/smoke.py:443`) i sprawdza, że klatek pełnych prawie nie ma.
  - Tło to gradient zależny tylko od wiersza (`kernel/gui/desktop.c:6-7`), więc smugi po kursorze da się wykryć w zrzucie ekranu.
  - Test „ducha” kursora na drugiej stronie BGA: ruch z A do B, potem uszkodzenie gdzie indziej (menu) i dwa zrzuty.
  - Powtórzyć na wariancie macierzy `vga-4M` (ścieżka `memcpy`).

## 5. Explorer i Shutdown

(Analiza w toku: `apps/explorer/explorer.c` i pozycja „Shutdown” w menu.)
