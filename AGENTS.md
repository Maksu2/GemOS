# GemOS — instrukcje dla agentów

Opis stanu na podstawie kodu (wrzesień 2026). Szczegóły, dowody i plan prac: [docs/AUDIT-2026-09.md](docs/AUDIT-2026-09.md).

## Stan systemu

- **Platforma:** x86, 32-bit protected mode, jedno jądro monolityczne. Działa w QEMU (VBE/BGA, dyskietka + dysk ATA); na prawdziwym sprzęcie nie było testowane.
- **Boot:** własny loader z dyskietki 1,44 MB (`build/gemos.img`) albo z dysku twardego (`build/gemos-hdd.img`, ten sam układ sektorów).
  - Stage 1 czyta stage 2 przez INT 13h ext (LBA), jeśli BIOS je daje (dysk twardy), inaczej przez CHS z geometrią z AH=08h (dyskietka). Każdy odczyt ma 3 próby.
  - Stage 2:
    - włącza A20 i zbiera mapę E820,
    - czyta nagłówek jądra (`GEMK` + rozmiar, na początku `kernel/entry.S`; `_start` musi być pierwszy w linkowaniu),
    - czyta jądro kawałkami po 32 KB i kopiuje każdy pod `0x100000` w trybie unreal,
    - ustawia najlepszy tryb VBE z listy preferencji (1920×1080 … 800×600, 32 bpp, LFB),
    - skacze do jądra z EBX = blok boot-info (`kernel/include/boot_info.h`: napęd, E820, tryb VBE).
  - `entry.S` zeruje BSS, a `kernel_main` najpierw kopiuje boot-info.
- **Jądro:**
  - GDT z segmentami ring 0/3 i TSS (`esp0` per zadanie); IDT, PIC pod `0x20/0x28`, PIT 1000 Hz.
  - Pamięć z mapy E820 (`kernel/memory/pmm.c`):
    - sterta first-fit zaraz za `__kernel_end` (co najmniej backbuffer + 4 MB, najwyżej 24 MB; `kernel/heap.c`: nagłówki z magic, kanarek za blokiem, łączenie wolnych bloków w obie strony; double free, nadpisany nagłówek i zapis za koniec bloku to panika); silnik fontów bierze bloki do 4 KB z osobnej puli 1 MB (`kernel/memory/pool.c`, `kernel/font/font_mem.c`), większe ze sterty,
    - pula ramek dostaje resztę użytecznego RAM-u poniżej 32 MB,
    - RAM powyżej 32 MB nie jest używany: jądro widzi pamięć fizyczną tylko przez identity map 0–32 MB, a od 32 MB zaczyna się userland,
    - za mało RAM kończy start komunikatem; minimum przy 1920×1080 to ok. 17 MB (backbuffer 8,1 MB + 4 MB zapasu sterty + 2 MB ramek + jądro z BSS, w tym stosy jądra).
  - Paging 4 KB: identity map 0–32 MB + 16 MB framebuffera, osobny katalog stron na proces.
  - Stosy jądra (`kernel/memory/kstack.c`) leżą w BSS, każdy z niezmapowaną stroną ochronną pod spodem: task 0 ma 64 KB (przełącza się na niego `entry.S`), idle i handler #DF po 8 KB, każdy proces 16 KB. #DF to bramka zadania z własnym TSS i stosem, więc przepełnienie stosu jądra kończy się paniką z rejestrami, a nie resetem.
- **Procesy:**
  - Round-robin, kwant 10 ms, maksymalnie 16 zadań plus zadanie idle. Każde zadanie ma własny stan FPU/SSE (`kernel/fpu.c`, FXSAVE/FXRSTOR przy każdym przełączeniu); z `float` w jądrze korzysta tylko silnik fontów. Wywłaszczany jest tylko kod w Ring 3 (reguła niżej). Zadanie 0 to pętla GUI w `kernel_main`: po każdej iteracji oddaje CPU i śpi (`TASK_BLOCKED`), dopóki nie ma zdarzeń. `hlt` wykonuje tylko idle.
  - Programy użytkownika to statyczne ELF32 `ET_EXEC` linkowane pod `0x02000000` (`userland/user_linker.ld`: segment kodu R+X i segment danych R+W od osobnej strony), ze stosem 8 KB pod `0x07FFF000`. Rozmiar programu ogranicza tylko GemFS (plik do ok. 4 MB) i wolna sterta: loader czyta plik z GemFS do bloku sterty o jego rozmiarze, a program z obrazu jądra ładuje prosto z obrazu.
  - Program startuje z `argc`, wskaźnikami `argv` i NULL na stosie; `userland/crt0.S` woła `main(argc, argv)` (`include/gemos/user_api.h`). `argv[0]` to nazwa, z którą go uruchomiono, a `argv[1]` plik do otwarcia, jeśli jądro go podało (`process_spawn_user_with_arg()`, np. File Explorer przez launcher edytora).
  - Strony kodu są tylko do odczytu także dla jądra (`CR0.WP=1`). Bez PAE nie ma bitu NX, więc dane pozostają wykonywalne.
  - Każdy wyjątek wywołany w Ring 3 (wektory 0–31 poza NMI, #DF i #MC) kończy tylko ten proces (`[USERFAULT]`, potem `Faulted PID=…`). Wyjątek w jądrze to panika z pełnym zrzutem rejestrów (także CR0–CR4) i zatrzymanie.
- **Syscalle:** `int 0x80`, 13 wywołań (`include/gemos/syscall_abi.h`).
  - Bramka wyłącza przerwania, a syscall włącza je zaraz po wejściu i wyłącza przed wyjściem, gdzie scheduler wybiera zadanie. IRQ wraca do przerwanego syscalla.
  - Wskaźniki użytkownika przechodzą przez `copy_from_user`/`copy_to_user` (sprawdzanie tablic stron).
  - `SYS_console_wait_event` blokuje proces do zdarzenia albo timeoutu: przy blokadzie EIP cofa się na `int $0x80` i syscall wykonuje się ponownie po obudzeniu.
  - `SYS_file_read`/`SYS_file_write` oddają CPU między blokami dysku (GemFS niżej).
  - Proces zabity w środku syscalla (`process_kill_pid()`) kończy syscall najszybciej, jak może (zapis się wycofuje), i zamiast wrócić do Ring 3 staje się zombie.
- **Userland:**
  - `UTERM.ELF`, `ABOUT.ELF` i `UTEXTEDIT.ELF` (plus `USRSMOKE.ELF` do debugowania) są wbudowane w obraz jądra. Przy starcie jądro zapisuje do GemFS, jako pliki systemowe, tylko te, których brakuje, które mają inną wersję (CRC-32 obrazu) albo nie zgadzają się z obrazem; kolejny start z tym samym jądrem nic nie zapisuje (`[PROC] Programs on GemFS: N seeded, M up to date`). Loader woli kopię z GemFS, a gdy jej nie przyjmie, bierze wbudowaną (także dla nazwy ze ścieżką, np. `/ABOUT.ELF` z File Explorera).
  - Procesy nie zmieniają programów: `SYS_file_write` zwraca `GEMOS_ERR_DENIED` dla nazw `*.ELF` (dowolna wielkość liter) i plików z flagą systemową. Sprawdza to `gemfs_write_user()` na sparsowanej nazwie i na inode, który zapis by zastąpił. Jądro (seeding, File Explorer) pisze bez tych ograniczeń.
  - Model „hosted app”: aplikacja wysyła siatkę komórek tekstowych (maks. 96×32), jądro rysuje okno. Działa najwyżej 8 takich okien naraz. Aplikacje śpią w `SYS_console_wait_event` (ABOUT z timeoutem do pełnej sekundy).
  - `UTEXTEDIT` otwiera i zapisuje pliki tekstowe do 8 KB przez `SYS_file_read`/`SYS_file_write` (Ctrl+O, Ctrl+S, Ctrl+Shift+S; nazwa bez rozszerzenia dostaje `.txt`) i otwiera plik z `argv[1]`. Zanim straci niezapisane zmiany (Esc, Ctrl+Q, przycisk X, Ctrl+N, Ctrl+O), pyta: Y zapisz, N porzuć, Esc anuluj. Pytania rysuje w ramce nad tekstem.
  - `FAULTS.ELF`, `FPUCHECK.ELF`, `FILETEST.ELF` i `LONGIO.ELF` (`userland/selftest/`) są tylko w obrazie autotestu (`make selftest`).
- **GUI (w jądrze):**
  - Menedżer okien, topbar, dock, menu, font TrueType (Inter) z antyaliasingiem.
  - Okna (`kernel/gui/wm/wm.c`):
    - `wm_add_window` stawia każde nowe okno w obszarze roboczym (`wm_work_area()`: między topbarem a dockiem); za duże trzyma tam lewy górny róg,
    - okno na rogu innego (w promieniu 8 px) przesuwa się o 24 px w dół i w prawo,
    - przeciągane okno trzyma pasek tytułu pod topbarem i nad dockiem, a co najmniej 64 px okna zostaje na ekranie; puszczenie przycisku kończy przeciąganie także nad topbarem i dockiem (wcześniej one je połykały i okno dalej jechało za myszą),
    - maksymalizacja wypełnia obszar roboczy,
    - aplikacja okna, które dostało wciśnięcie przycisku, dostaje też jego puszczenie (gdziekolwiek nastąpi), a okno z fokusem ruchy myszy, gdy żadne okno nie jest przeciągane,
    - log: `[WM] Work area`, `[WM] Placed`, `[WM] Dropped` i położenie okna w `[CONSOLE] Opened` (z nich korzysta smoke).
  - Tekst jest przycięty do `clip_rect`, jak inne prymitywy (`aa_blend_pixel`), więc nie wychodzi poza okno.
  - Okno aplikacji hostowanej rysuje każdy znak w jego komórce 8×15 (`font_draw_cell`: glif wyśrodkowany w pikselach fizycznych), więc kolumny się zgadzają mimo proporcjonalnego fontu. Znaków spoza 0x21–0x7E nie rysuje.
  - Skala UI (całkowita, `int ui_scale`) 2 przy 1920×1080 (współrzędne logiczne 960×540), 1 w mniejszych trybach.
  - **Shutdown** (menu GemOS) obsługuje pętla GUI:
    - `gemfs_shutdown()` czeka na trwające wywołanie GemFS i już nie oddaje blokady (każdy zapis kończy się FLUSH CACHE),
    - ekran pokazuje „It is now safe to turn off the computer.”,
    - `power_off()` (`drivers/power.c`) pisze polecenie uśpienia ACPI do PM1a_CNT pod `0x604` (QEMU) i `0xB004` (Bochs); gdzie to nie działa, jądro loguje `[POWER] Still running` i staje (`hlt` z IF=0),
    - od wywołania żaden proces już nie działa, a tekst niezapisany w edytorze przepada.
  - Klatki (`kernel/kernel.c`):
    - pełna klatka przerysowuje cały ekran do backbuffera i kopiuje go do VRAM; page flip przez BGA tylko po wykryciu adaptera (ID `0xB0C0`–`0xB0C5`) i przy VRAM na dwie strony, inaczej `memcpy` do framebuffera,
    - backbuffer nigdy nie trzyma kursora: pełna klatka bierze go tylko na czas kopii (`cursor_frame_begin`/`cursor_frame_end`),
    - ruch myszy, który nic innego nie zmienia, daje klatkę samego kursora (`cursor_move_on`): na widocznej stronie stary prostokąt kursora wraca z backbuffera, a kursor rysuje się w nowym miejscu,
    - zmiana w jednym miejscu zgłasza prostokąt przez `kernel_damage()` (`kernel/include/gui_loop.h`): konsola obszar klienta przy `console_present`/`write`/`clear`, Explorer przy podświetleniu i obrysie przeciągania, WM starą i nową ramkę przeciąganego okna z cieniem, zegar topbar,
    - częściowa klatka rysuje scenę tylko w tych prostokątach (`gfx_set_bound()`: nic nie wychodzi poza `bound_rect` kontekstu, okna poza nim są pomijane), kopiuje je na widoczną stronę i rysuje kursor na wierzchu; najwyżej 16 prostokątów, nachodzące na siebie się łączą, a gdy brakuje miejsca, rysowana jest pełna klatka,
    - pełną klatkę dają kliknięcia i puszczenia przycisku, ruch myszy nad otwartym menu, otwarcie i zamknięcie okna (`kernel_request_redraw()`) i aplikacje z `changed()`; klawisze idą do aplikacji, która sama zgłasza, co zmieniła,
    - co sekundę pętla pyta zegar w topbarze (`topbar_clock_changed()`, wtedy prostokąt topbaru) i aplikacje (opcjonalne `app_t.changed`, wtedy pełna klatka; Log Viewer odpowiada „tak”, gdy log urósł), czy coś zmieniło się bez zdarzenia,
    - log `[GFX] Frames: full= partial= cursor=` najwyżej co 2 s, gdy przybyły klatki, ze średnim i najdłuższym czasem pełnej (pod TCG ok. 50 ms).
- **ATA:** PIO LBA28 (`drivers/ata.c`). IDENTIFY na 4 pozycjach, każde oczekiwanie z limitem, kody błędów zamiast pętli bez końca. Limity są czasowe, mierzone stoperem na liczniku PIT (`pit_stopwatch_*`: liczy przeładowania licznika, więc działa też z IF=0, a z IF=1 bierze też ticki): 5 s na IDENTIFY, 30 s na odczyt, zapis i FLUSH CACHE. Po timeoutcie sterownik resetuje kanał (SRST), żeby następne polecenie nie przesłało złych bajtów. Każdy dysk jest tylko do odczytu, dopóki GemFS go nie zamontuje (`ata_allow_writes()`); zapis na inny dysk kończy się `ATA_ERR_READ_ONLY` i linią `[ATA] Refused …` w logu.
- **GemFS v3** (`kernel/fs/`, format w `docs/GEMFS.md`):
  - cały dysk ATA w blokach 4 KB: superblok z magic `GEMOS-FS`, wersją, rozmiarem i CRC-32, bitmapa bloków, tablica inode'ów (128 B: 12 bloków bezpośrednich i 1 pośredni, plik do ok. 4 MB), katalogi (wpisy 64 B, nazwy do 55 bajtów, ścieżki do 127);
  - jądro montuje pierwszy dysk z poprawnym superblokiem, bitmapą i katalogiem głównym. Bez takiego dysku startuje bez FS (programy idą z obrazu jądra) i nie zapisuje na żaden dysk ani jednego sektora;
  - jądro nigdy nie formatuje: dysk tworzy narzędzie hosta `tools/mkgemfs` (`make` robi `build/data.img`, 10 MB);
  - zapis pliku to copy-on-write: nowe bloki w bitmapie, dane, inode jako zatwierdzenie, na końcu zwolnienie starych bloków, więc stara treść zostaje, dopóki nowa nie jest na dysku. `gemfs_stat`/`write`/`mkdir`/`list`/`delete` przyjmują ścieżki z podkatalogami, a `gemfs_delete` usuwa katalog z całą zawartością;
  - księgowania nie ma. Awaria w trakcie zapisu albo usuwania zostawia najwyżej zajęte bloki i inode'y, do których nic nie prowadzi, a jądro zwalnia je przy montowaniu (`gemfs_reclaim()`: przejście drzewa od korzenia, `[GemFS] Reclaimed …`):
    - nic nie zwalnia, jeśli drzewo nie jest takie, jak zostawia je kolejność zapisów: nie da się przeczytać inode'a, katalogu albo bloku pośredniego, wskaźnik wychodzi poza obszar danych, wpis prowadzi do wolnego inode'a, inode jest w dwóch wpisach, blok w dwóch inode'ach albo blok drzewa jest w bitmapie wolny (`[GemFS] Nothing reclaimed: …`; wtedy zostaje `tools/mkgemfs check`),
    - dysk, na którym nie ma czego zwolnić, nie dostaje ani jednego zapisu (`[GemFS] Nothing to reclaim`);
  - jedno zadanie naraz: każde publiczne wywołanie trzyma blokadę od początku do końca. Długi odczyt albo zapis oddaje CPU między blokami (`scheduler_preempt_point()`), a inne zadanie, które wtedy wywoła GemFS, śpi do końca tej operacji (zabity proces przestaje czekać). `gemfs_read_file()` czyta cały plik pod jedną blokadą; tak czyta `SYS_file_read`;
  - zadanie GUI nie śpi na blokadzie: oddaje CPU jej właścicielowi (`scheduler_yield_to()`), który działa sam do końca operacji i od razu oddaje CPU z powrotem. Nic innego nie działa w tym czasie, więc nikt nie zmienia stanu GUI w środku iteracji.
- **Aplikacje jądra (`apps/`):**
  - File Explorer (foldery, UP, New Folder). Kliknięcie zaznacza, kliknięcie zaznaczonego otwiera:
    - plik `.txt`/`.gemtext` w UTEXTEDIT z pełną ścieżką,
    - `.ELF` uruchamia z GemFS,
    - rozszerzenia działają w dowolnej wielkości liter, a plik, którego nie da się otworzyć, albo program, który nie wystartował, zamienia ścieżkę w pasku na czerwony komunikat.
  - Log Viewer oraz launchery trzech programów userlandu.
  - Test App jest zarejestrowana, ale niedostępna z menu.

## Znane problemy

Nie obchodzić ich po cichu; plan naprawy jest w §8.2 audytu.

- **Zadanie GUI czeka na koniec operacji GemFS, w której trakcie potrzebuje dysku** (File Explorer albo start programu w trakcie zapisu do 1 MB): GUI stoi wtedy do jej końca.
- **Render pełnej klatki (1080p, pod TCG ok. 50 ms) w task 0 nie jest przerywany:** procesy czekają na koniec iteracji. Ruch myszy i zmiany w jednym oknie (pisanie, zegar ABOUT) już jej nie wywołują; nadal dają ją kliknięcia, menu oraz otwieranie i zamykanie okien.
- **RAM powyżej 32 MB nie jest używany** (okno identity map, patrz wyżej).
- **Shutdown nie pyta programów o niezapisane zmiany:** tekst niezapisany w UTEXTEDIT przepada.

## Mapa repo

```text
boot/       stage1 (MBR) + stage2 (A20, E820, jądro wg nagłówka, VBE, boot-info)
kernel/     kernel.c (kernel_main + pętla GUI), gdt/idt/isr, scheduler, process,
            elf, syscall, console (okna aplikacji hostowanych), heap, event,
            memory/ (paging, pmm, stosy jądra, pula), fs/ (GemFS), gfx/ (prymitywy, ikony, font),
            selftest.c (autotest, tylko w `make selftest`),
            gui/ (WM, okna, topbar, pulpit), ui/ (dock, menu, kursor, fokus),
            app/ (rejestr aplikacji), font/ (TrueType, rasteryzer, cache, AA)
drivers/    serial, VBE/BGA, PIC, PIT, klawiatura, mysz, ATA PIO, RTC, wyłączanie (ACPI)
lib/        string.c (implementacja include/string.h)
include/    freestanding stdint/stddef/stdbool/string/io + gemos/ (ABI userlandu)
apps/       aplikacje jądra i launchery programów userlandu
userland/   programy ring 3 (uterm2/, about/, textedit/, common/, crt0.S,
            usrsmoke.S, user_linker.ld) + selftest/ (programy autotestu)
assets/     font.ttf (Inter) + OFL.txt
tools/      smoke.sh + smoke.py (test w QEMU), mkgemfs (dyski GemFS na hoście)
docs/       strona GitHub Pages, audyt kodu, GEMFS.md (format dysku)
```

## Budowanie i testy

- `make all` tworzy `build/gemos.img` (dyskietka) i `build/gemos-hdd.img` (dysk twardy). Wymaga `nasm` oraz `i686-elf-gcc` albo `x86_64-elf-gcc`; bez nich Makefile używa hostowego `gcc -m32`, tak jak CI.
- `tools/smoke.sh` buduje obraz, bootuje go w QEMU bez okna, uruchamia UTERM, ABOUT i UTEXTEDIT, sprawdza log i zrzuty ekranu oraz rozmieszczenie okien (obszar roboczy, kaskada drugiego okna, przeciąganie na dock i puszczenie nad topbarem; położenie okien bierze z logu) i siatkę konsoli (10 liter różnej szerokości zajmuje dokładnie 10 komórek), sprawdza, że ruch myszy po pustym pulpicie nie rysuje pełnych klatek (liczniki z `[GFX] Frames`) ani nie zostawia śladów kursora na tapecie, że pisanie w UTERM rysuje tylko częściowe klatki, a ekran po nich jest piksel w piksel taki jak po pełnej, i że zegar w topbarze sam pokazuje nową minutę (harness ustawia zegar QEMU na 14 s przed pełną minutą), a Log Viewer nowe linie logu, uruchamia ABOUT i USRSMOKE z File Explorera (kliknięcie nie zostawia prostokąta przeciągania), kończy się Shutdown z menu GemOS (QEMU musi zamknąć się samo z kodem 0, a log pokazać zatrzymanie GemFS przed wyłączeniem), a potem sprawdza dysk danych przez `tools/mkgemfs check`. Na dysku GemFS log musi mieć `[GemFS] Nothing to reclaim`. Shutdown kończy też test edytora i stress. Musi skończyć się `SMOKE: PASS` przed każdym commitem. Test nie wysyła żadnego wejścia, gdy czeka na klatkę: okno ma się pojawić samo.
- `tools/smoke.sh --stress [N]` (domyślnie 25 cykli) otwiera, obsługuje klawiaturą i myszą i zamyka wszystkie programy; każdy proces musi skończyć z `exit=0`, a log nie może mieć przeplecionych linii. Uruchom go po każdej zmianie schedulera, syscalli, konsoli albo pętli GUI.
- `tools/smoke.sh --matrix` uruchamia smoke na 32/64/256 MB, bez dysku danych, z 4 MB VRAM (1280×800, `memcpy`), przy starcie z dysku twardego (dysk rozruchowy musi zostać bez zmian), z dyskiem danych bez GemFS i z uszkodzonym superblokiem (SHA-256 obrazu przed i po musi być ten sam), przy drugim starcie tego samego dysku (nic nie może się zapisać), z dyskiem, który zapisuje wolno (QEMU przepuszcza 12 zapisów na sekundę), na maszynie bez ACPI (`-machine pc,acpi=off`: Shutdown nie wyłącza QEMU, jądro staje, a ekran mówi, że można wyłączyć komputer) oraz na dysku po awarii (`after-crash`). W tym ostatnim harness po pierwszym starcie dokłada przez `tools/mkgemfs` pliki, które muszą przetrwać (jeden z blokiem pośrednim), i to, co zostawia awaria w trakcie zapisu albo usuwania: 21 bloków i 4 inode'y, do których nic nie prowadzi, także katalog z plikiem i plik z blokiem pośrednim. Start, przy którym blok pośredni jednego z zachowanych plików wskazuje poza obszar danych, nie może niczego zwolnić ani zapisać (SHA-256 obrazu); następny, już z poprawnym wskaźnikiem, musi zwolnić dokładnie te 21 bloków i 4 inode'y, a po teście `tools/mkgemfs check` nie może nic znaleźć, a zachowane pliki mają być takie jak przedtem. Uruchom go po każdej zmianie bootloadera, pamięci, VBE, ATA albo GemFS.
- `tools/smoke.sh --selftest` buduje obraz autotestu (`make selftest`, `build/selftest/gemos.img`, jądro z `-DGEMOS_SELFTEST`) i sprawdza jego raport. `kernel/selftest.c` działa jako zadanie jądra obok GUI i sprawdza:
  - stertę i pulę: double free, nadpisany nagłówek, zapis za koniec bloku, scalanie w obie strony, brak wycieków;
  - tekst rysowany z `clip_rect` nie zmienia żadnego piksela poza nim;
  - odrzucanie 14 zepsutych ELF-ów bez wycieku pamięci i ramek;
  - każdy wyjątek osiągalny z Ring 3 (`FAULTS.ELF`, jeden proces na wyjątek): ginie tylko ten proces;
  - stan x87/SSE dwóch kopii `FPUCHECK.ELF` i zadania jądra przez przełączenia;
  - GemFS przez API jądra: katalogi, plik 100 KB z blokiem pośrednim, zastępowanie, zapis przerwany w połowie, błędy, 70 wpisów w jednym katalogu z usuwaniem, rekurencyjne usuwanie bez wycieku bloków i inode'ów;
  - odmowę zapisu ATA na drugi dysk bez GemFS (harness sprawdza potem, że się nie zmienił);
  - `FILETEST.ELF` (83 KB, uruchamiany z GemFS): plik 80 KB i plik w podkatalogu przez syscalle, odczyt `UTERM.ELF`, odmowa nadpisania programów;
  - `/USRSMOKE.ELF` zepsuty na GemFS uruchamia się z obrazu jądra, a potem wraca na dysk bez zmian;
  - długie syscalle plikowe (`LONGIO.ELF`; autotest spowalnia wtedy każdy zapisany sektor o 2 ms, więc zapis 128 KB trwa ponad pół sekundy):
    - w trakcie zapisu zegar idzie dalej, a inne zadania i pętla GUI dostają CPU,
    - trzy procesy na dwóch plikach naraz nie widzą połowy zapisu,
    - proces zabity w środku zapisu kończy się na następnym bloku, a plik zostaje taki jak był,
    - zadanie GUI, które potrzebuje GemFS w trakcie zapisu, czeka do jego końca, a działa wtedy tylko właściciel blokady.

  Harness uruchamia autotest dwa razy na tych samych dyskach: pliki z pierwszego startu sprawdzają `tools/mkgemfs` na hoście i drugi start, który przy montowaniu nie może mieć czego odzyskać (zapisy przerwane i zabite oddały swoje bloki).

  Na końcu celowo przepełnia swój stos jądra: log musi skończyć się paniką #DF z nazwą strony ochronnej. Wyjątek, którego CPU nie zgłasza (QEMU TCG: #XM), daje linię `SKIP`, a nie PASS. Uruchom go po każdej zmianie sterty, loadera ELF, obsługi wyjątków, FPU, stosów jądra, schedulera, GemFS albo syscalli plikowych.
- `tools/smoke.sh --editor` testuje UTEXTEDIT z plikami na nowym dysku GemFS: Esc i przycisk X przy niezapisanym tekście pokazują pytanie (a proces dalej działa; ramka pytania leży na siatce konsoli), Esc je anuluje, Y i „Save as” zapisuje, File Explorer otwiera ten plik w edytorze, Ctrl+S zapisuje ponownie, N porzuca zmiany, zapis jako `UTERM.ELF` daje błąd w statusie. Na końcu harness czyta plik z dysku na hoście. Uruchom go po każdej zmianie UTEXTEDIT, konsoli, File Explorera albo syscalli plikowych.
- CI (`.github/workflows/ci.yml`) uruchamia smoke, test edytora, stress, macierz i autotest przy każdym pushu i PR.
- `make run` otwiera QEMU z dyskiem danych `build/data.img` (robi go `tools/mkgemfs format`, jeśli go nie ma; stary `data.img` sprzed GemFS v3 jądro pomija, trzeba go usunąć), `make run-hdd` startuje z dysku twardego, `make debug` dodatkowo czeka na GDB na porcie `:1234` (działa też bez dysku danych).
- `tools/mkgemfs` (Python, sama biblioteka standardowa) obsługuje obraz dysku GemFS na hoście: `format`, `info`, `ls`, `cat`, `put` (`--system`, `--version`), `mkdir`, `rm`, `check`.
- Makefile przerywa build, gdy `kernel.bin` nie zaczyna się nagłówkiem `GEMK` z właściwym rozmiarem albo nie mieści się na dyskietce.
- Zmiany, które nie powinny zmieniać zachowania, sprawdzaj porównaniem binariów (`build/kernel.bin`, `build/*.elf`) przed i po.

## Zasady architektury

- **Współbieżność: kod w Ring 0 nie jest wywłaszczany.** Szczegóły są w `kernel/scheduler.c`.
  - **Kiedy następuje przełączenie zadania:**
    - IRQ0 przerwał Ring 3 (koniec kwantu albo obudzone inne zadanie),
    - zadanie samo oddaje CPU: syscall kończy się `exit`/`yield`/blokadą, fault zabija proces, kod jądra woła `scheduler_yield()` albo `scheduler_yield_to()`,
    - długa praca w jądrze woła `scheduler_preempt_point()` tam, gdzie nic nie jest w połowie zmiany (GemFS między blokami), a kwant się skończył albo obudzono inne zadanie. Zadanie GUI oddaje CPU tylko między iteracjami pętli (i właścicielowi blokady GemFS).
  - **IRQ, które przerwało kod jądra, zawsze do niego wraca** (`isr_handler`, `scheduler_tick`).
  - **Kod jądra wykonuje się po kolei.** Task 0 i syscalle biegną jeden po drugim między punktami oddania CPU i nie potrzebują blokad względem innych zadań. Nie wolno oddawać CPU w środku operacji na współdzielonym stanie. Wyjątkiem jest GemFS: jego długie operacje oddają CPU, więc ma blokadę.
  - **Zabicie procesu, który oddał CPU w jądrze, czeka na koniec syscalla.** Proces może wtedy trzymać GemFS albo być w połowie zmiany: `scheduler_kill_task()` tylko go oznacza i budzi, syscall kończy się najszybciej, jak może, a wyjście z niego robi z procesu zombie.
  - **Nie wolno czekać w pętli na inne zadanie** (`hlt`, busy-wait). Należy zablokować zadanie (`scheduler_block_current()`) i obudzić je (`scheduler_wake()`, `process_wake()`):
    - zadanie jądra woła potem `scheduler_yield()`,
    - syscall blokuje się z restartem jak `SYS_console_wait_event` albo w środku i woła `scheduler_yield()`, jak przy czekaniu na blokadę GemFS,
    - `hlt` wykonuje tylko idle.
  - **Handlery IRQ przerywają także Ring 0** (również syscalle, bo działają z IF=1), więc nie drukują logu, nie alokują, nie dotykają GUI ani FS i nie używają FPU (pliki z `IRQ_PATH_SOURCES` w Makefile kompilują się z `-mgeneral-regs-only`). Stan dzielony z IRQ zmienia się z zadania tylko w `irq_save()`/`irq_restore()` (`kernel/include/irq.h`). Ten stan to:
    - kolejka zdarzeń,
    - licznik ticków,
    - tablica zadań,
    - stan klawiatury i myszy,
    - pozycja kursora.
  - **Odświeżanie ekranu:** wszystko, co zmienia zawartość ekranu, woła `kernel_damage()` (prostokąt, który się zmienił) albo `kernel_request_redraw()` (cały ekran: zmiana układu okien), co budzi task 0. Dotyczy to także zmian po ruchu myszy i klawiszu: sam ruch rysuje tylko kursor, a klawisz nic.
- **32-bit, własny bootloader**, bez GRUB-a, bez libc i bez libgcc.
- **GUI zostaje w jądrze.** Userland rozmawia z systemem wyłącznie przez ABI z `include/gemos/` (`syscall_abi.h`, `console_abi.h`, `user_api.h`; programy autotestu także `selftest_abi.h`) i nie includuje nagłówków jądra.
- **Kody klawiszy** pochodzą tylko z `GEMOS_KEY_*` w `include/gemos/console_abi.h`.
- **Jeden `string.h`**: `include/string.h`, dołączany jako `<string.h>`.
- **Kolejność prac jest w §8.2 audytu.** Etapy 0–5 są zrobione (etap 5 krok po kroku: `docs/ETAP5-PLAN.md`). Dalej idą znane problemy z listy wyżej; duże nowe rzeczy, przed którymi audyt ostrzega (GUI w userlandzie, sieć, USB, SMP), i przeniesienie Explorera do userlandu czekają na osobną decyzję.
