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
  - Programy użytkownika to statyczne ELF32 `ET_EXEC` linkowane pod `0x02000000` (`userland/user_linker.ld`: segment kodu R+X i segment danych R+W od osobnej strony), ze stosem 8 KB pod `0x07FFF000`. Maksymalny rozmiar programu to ok. 8 KB (bufor loadera, slot GemFS).
  - Strony kodu są tylko do odczytu także dla jądra (`CR0.WP=1`). Bez PAE nie ma bitu NX, więc dane pozostają wykonywalne.
  - Każdy wyjątek wywołany w Ring 3 (wektory 0–31 poza NMI, #DF i #MC) kończy tylko ten proces (`[USERFAULT]`, potem `Faulted PID=…`). Wyjątek w jądrze to panika z pełnym zrzutem rejestrów (także CR0–CR4) i zatrzymanie.
- **Syscalle:** `int 0x80`, 13 wywołań (`include/gemos/syscall_abi.h`), każde od wejścia do `iret` z IF=0. Wskaźniki użytkownika przechodzą przez `copy_from_user`/`copy_to_user` (sprawdzanie tablic stron). `SYS_console_wait_event` blokuje proces do zdarzenia albo timeoutu: przy blokadzie EIP cofa się na `int $0x80` i syscall wykonuje się ponownie po obudzeniu.
- **Userland:**
  - `UTERM.ELF`, `ABOUT.ELF` i `UTEXTEDIT.ELF` (plus `USRSMOKE.ELF` do debugowania) są wbudowane w obraz jądra i przy każdym starcie zapisywane do GemFS; loader woli kopię z GemFS.
  - Model „hosted app”: aplikacja wysyła siatkę komórek tekstowych (maks. 96×32), jądro rysuje okno. Działa najwyżej 8 takich okien naraz. Aplikacje śpią w `SYS_console_wait_event` (ABOUT z timeoutem do pełnej sekundy).
  - `UTEXTEDIT` nie zapisuje ani nie otwiera plików.
  - `FAULTS.ELF` i `FPUCHECK.ELF` (`userland/selftest/`) są tylko w obrazie autotestu (`make selftest`).
- **GUI (w jądrze):**
  - Menedżer okien, topbar, dock, menu, font TrueType (Inter) z antyaliasingiem.
  - Skala UI (całkowita, `int ui_scale`) 2 przy 1920×1080 (współrzędne logiczne 960×540), 1 w mniejszych trybach.
  - Każda klatka przerysowuje cały ekran. Page flip przez BGA tylko po wykryciu adaptera (ID `0xB0C0`–`0xB0C5`) i przy VRAM na dwie strony; inaczej `memcpy` do framebuffera.
- **ATA:** PIO LBA28 (`drivers/ata.c`). IDENTIFY na 4 pozycjach, każde oczekiwanie z limitem, kody błędów zamiast pętli bez końca.
- **GemFS:** tablica 64 wpisów pod LBA 1–4 i stałe sloty po 8 KB od LBA 5. Leży na pierwszym dysku ATA **bez sygnatury rozruchowej MBR**; nie ma własnego superbloku ani sygnatury. Bez takiego dysku system startuje bez FS: tablica jest pusta, programy idą z obrazu jądra.
- **Aplikacje jądra (`apps/`):**
  - File Explorer i Log Viewer oraz launchery trzech programów userlandu.
  - Test App jest zarejestrowana, ale niedostępna z menu.
  - `apps/textedit` to kernelowy edytor: wkompilowany, niezarejestrowany, zostaje do etapu 5.

## Znane problemy

Nie obchodzić ich po cichu; plan naprawy jest w §8.2 audytu.

- **Syscalle z IF=0:** długi syscall (zapis pliku to do ~24 sektorów ATA) wstrzymuje przerwania, a PIT gubi ticki.
- **Render całej klatki (1080p) w task 0 nie jest przerywany:** procesy czekają na koniec iteracji.
- **GemFS bez sygnatury** pisze po surowym dysku; chroni go tylko to, że pomija dyski z sygnaturą rozruchową (etap 4).
- **RAM powyżej 32 MB nie jest używany** (okno identity map, patrz wyżej).

## Mapa repo

```text
boot/       stage1 (MBR) + stage2 (A20, E820, jądro wg nagłówka, VBE, boot-info)
kernel/     kernel.c (kernel_main + pętla GUI), gdt/idt/isr, scheduler, process,
            elf, syscall, console (okna aplikacji hostowanych), heap, event,
            memory/ (paging, pmm, stosy jądra, pula), fs/ (GemFS), gfx/ (prymitywy, ikony, font),
            selftest.c (autotest, tylko w `make selftest`),
            gui/ (WM, okna, topbar, pulpit), ui/ (dock, menu, kursor, fokus),
            app/ (rejestr aplikacji), font/ (TrueType, rasteryzer, cache, AA)
drivers/    serial, VBE/BGA, PIC, PIT, klawiatura, mysz, ATA PIO, RTC
lib/        string.c (implementacja include/string.h)
include/    freestanding stdint/stddef/stdbool/string/io + gemos/ (ABI userlandu)
apps/       aplikacje jądra i launchery programów userlandu
userland/   programy ring 3 (uterm2/, about/, textedit/, common/, crt0.S,
            usrsmoke.S, user_linker.ld) + selftest/ (programy autotestu)
assets/     font.ttf (Inter) + OFL.txt
tools/      smoke.sh + smoke.py (test w QEMU)
docs/       strona GitHub Pages + audyt kodu
```

## Budowanie i testy

- `make all` tworzy `build/gemos.img` (dyskietka) i `build/gemos-hdd.img` (dysk twardy). Wymaga `nasm` oraz `i686-elf-gcc` albo `x86_64-elf-gcc`; bez nich Makefile używa hostowego `gcc -m32`, tak jak CI.
- `tools/smoke.sh` buduje obraz, bootuje go w QEMU bez okna, uruchamia UTERM, ABOUT i UTEXTEDIT, sprawdza log i zrzuty ekranu. Musi skończyć się `SMOKE: PASS` przed każdym commitem. Test nie wysyła żadnego wejścia, gdy czeka na klatkę: okno ma się pojawić samo.
- `tools/smoke.sh --stress [N]` (domyślnie 25 cykli) otwiera, obsługuje klawiaturą i myszą i zamyka wszystkie programy; każdy proces musi skończyć z `exit=0`, a log nie może mieć przeplecionych linii. Uruchom go po każdej zmianie schedulera, syscalli, konsoli albo pętli GUI.
- `tools/smoke.sh --matrix` uruchamia smoke na 32/64/256 MB, bez dysku danych, z 4 MB VRAM (1280×800, `memcpy`) i przy starcie z dysku twardego. Uruchom go po każdej zmianie bootloadera, pamięci, VBE albo ATA.
- `tools/smoke.sh --selftest` buduje obraz autotestu (`make selftest`, `build/selftest/gemos.img`, jądro z `-DGEMOS_SELFTEST`) i sprawdza jego raport. `kernel/selftest.c` działa jako zadanie jądra obok GUI i sprawdza:
  - stertę i pulę: double free, nadpisany nagłówek, zapis za koniec bloku, scalanie w obie strony, brak wycieków;
  - odrzucanie 14 zepsutych ELF-ów bez wycieku pamięci i ramek;
  - każdy wyjątek osiągalny z Ring 3 (`FAULTS.ELF`, jeden proces na wyjątek): ginie tylko ten proces;
  - stan x87/SSE dwóch kopii `FPUCHECK.ELF` i zadania jądra przez przełączenia.

  Na końcu celowo przepełnia swój stos jądra: log musi skończyć się paniką #DF z nazwą strony ochronnej. Wyjątek, którego CPU nie zgłasza (QEMU TCG: #XM), daje linię `SKIP`, a nie PASS. Uruchom go po każdej zmianie sterty, loadera ELF, obsługi wyjątków, FPU albo stosów jądra.
- CI (`.github/workflows/ci.yml`) uruchamia smoke, stress, macierz i autotest przy każdym pushu i PR.
- `make run` otwiera QEMU z dyskiem danych `build/data.img`, `make run-hdd` startuje z dysku twardego, `make debug` dodatkowo czeka na GDB na porcie `:1234` (działa też bez dysku danych).
- Makefile przerywa build, gdy `kernel.bin` nie zaczyna się nagłówkiem `GEMK` z właściwym rozmiarem albo nie mieści się na dyskietce.
- Zmiany, które nie powinny zmieniać zachowania, sprawdzaj porównaniem binariów (`build/kernel.bin`, `build/*.elf`) przed i po.

## Zasady architektury

- **Współbieżność: kod w Ring 0 nie jest wywłaszczany.** Szczegóły są w `kernel/scheduler.c`.
  - **Kiedy następuje przełączenie zadania:**
    - IRQ0 przerwał Ring 3 (koniec kwantu albo obudzone inne zadanie),
    - zadanie samo oddaje CPU: syscall kończy się `exit`/`yield`/blokadą, fault zabija proces, zadanie jądra woła `scheduler_yield()`.
  - **Kod jądra wykonuje się po kolei.** Task 0 i syscalle biegną jeden po drugim między punktami oddania CPU i nie potrzebują blokad względem innych zadań. Nie wolno oddawać CPU w środku operacji na współdzielonym stanie.
  - **Nie wolno czekać w pętli na inne zadanie** (`hlt`, busy-wait). Należy zablokować zadanie (`scheduler_block_current()`) i obudzić je (`scheduler_wake()`, `process_wake()`):
    - zadanie jądra woła potem `scheduler_yield()`,
    - syscall blokuje się z restartem jak `SYS_console_wait_event`,
    - `hlt` wykonuje tylko idle.
  - **Handlery IRQ przerywają także Ring 0**, więc nie drukują logu, nie alokują, nie dotykają GUI ani FS i nie używają FPU (pliki z `IRQ_PATH_SOURCES` w Makefile kompilują się z `-mgeneral-regs-only`). Stan dzielony z IRQ zmienia się z zadania tylko w `irq_save()`/`irq_restore()` (`kernel/include/irq.h`). Ten stan to:
    - kolejka zdarzeń,
    - licznik ticków,
    - tablica zadań,
    - stan klawiatury i myszy,
    - pozycja kursora.
  - **Odświeżanie ekranu:** wszystko, co zmienia zawartość ekranu, woła `kernel_request_redraw()`, co budzi task 0.
- **32-bit, własny bootloader**, bez GRUB-a, bez libc i bez libgcc.
- **GUI zostaje w jądrze.** Userland rozmawia z systemem wyłącznie przez ABI z `include/gemos/` (`syscall_abi.h`, `console_abi.h`, `user_api.h`; programy autotestu także `selftest_abi.h`) i nie includuje nagłówków jądra.
- **Kody klawiszy** pochodzą tylko z `GEMOS_KEY_*` w `include/gemos/console_abi.h`.
- **Jeden `string.h`**: `include/string.h`, dołączany jako `<string.h>`.
- **Żadnych nowych funkcji przed naprawą współbieżności** (etap 3 audytu). Kolejność prac jest w §8.2 audytu.
