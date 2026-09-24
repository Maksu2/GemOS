# GemOS — instrukcje dla agentów

Opis stanu na podstawie kodu (wrzesień 2026). Szczegóły, dowody i plan prac: [docs/AUDIT-2026-09.md](docs/AUDIT-2026-09.md).

## Stan systemu

- **Platforma:** x86, 32-bit protected mode, jedno jądro monolityczne. Działa w QEMU (VBE/BGA, dyskietka + dysk ATA); na prawdziwym sprzęcie nie było testowane.
- **Boot:** własny loader z dyskietki 1,44 MB.
  - Stage 1 czyta stage 2 przez INT 13h w trybie CHS.
  - Stage 2 włącza A20, zbiera mapę E820 (jądro jej nie używa), ustawia VBE **tylko 1920×1080×32** (inny tryb = halt), czyta dokładnie 1120 sektorów jądra, kopiuje je pod `0x100000` i skacze na początek obrazu (`_start` z `kernel/entry.S` musi być pierwszy w linkowaniu).
  - BSS nie jest zerowany.
- **Jądro:**
  - GDT z segmentami ring 0/3 i TSS (`esp0` per zadanie); IDT, PIC pod `0x20/0x28`, PIT 1000 Hz.
  - Sterta first-fit 24 MB za `__kernel_end`.
  - Paging 4 KB: identity map 0–32 MB + 16 MB framebuffera, pula ramek użytkownika do `0x02000000`, osobny katalog stron na proces.
  - Rozmiar RAM nie jest wykrywany; minimum to 32 MB.
- **Procesy:**
  - Round-robin z wywłaszczaniem, kwant 10 ms, maksymalnie 16 zadań. Zadanie 0 to pętla GUI w `kernel_main`.
  - Programy użytkownika to statyczne ELF32 `ET_EXEC` linkowane pod `0x02000000`, ze stosem 8 KB pod `0x07FFF000`. Maksymalny rozmiar programu to ok. 8 KB (bufor loadera, slot GemFS).
  - Proces z wyjątkiem #DE, #UD, #TS, #NP, #SS, #GP lub #PF jest zabijany; każdy inny wyjątek z ring 3 zatrzymuje system.
- **Syscalle:** `int 0x80`, 12 wywołań (`include/gemos/syscall_abi.h`). Wskaźniki użytkownika przechodzą przez `copy_from_user`/`copy_to_user` (sprawdzanie tablic stron).
- **Userland:**
  - `UTERM.ELF`, `ABOUT.ELF` i `UTEXTEDIT.ELF` (plus `USRSMOKE.ELF` do debugowania) są wbudowane w obraz jądra i przy każdym starcie zapisywane do GemFS; loader woli kopię z GemFS.
  - Model „hosted app”: aplikacja wysyła siatkę komórek tekstowych (maks. 96×32), jądro rysuje okno. Działa najwyżej 8 takich okien naraz.
  - `UTEXTEDIT` nie zapisuje ani nie otwiera plików.
- **GUI (w jądrze):**
  - Menedżer okien, topbar, dock, menu, font TrueType (Inter) z antyaliasingiem.
  - Skala UI 2.0, czyli współrzędne logiczne 960×540.
  - Każda klatka przerysowuje cały ekran.
- **GemFS:** tablica 64 wpisów pod LBA 1–4 pierwszego dysku ATA i stałe sloty po 8 KB od LBA 5. Nie ma superbloku ani sygnatury. Bez podpiętego dysku jądro wisi w sterowniku ATA.
- **Aplikacje jądra (`apps/`):**
  - File Explorer i Log Viewer oraz launchery trzech programów userlandu.
  - Test App jest zarejestrowana, ale niedostępna z menu.
  - `apps/textedit` to kernelowy edytor: wkompilowany, niezarejestrowany, zostaje do etapu 5.

## Znane problemy

Nie obchodzić ich po cichu; plan naprawy jest w §8.2 audytu.

- **Brak synchronizacji.** Jądro jest wywłaszczalne, a syscalle zmieniają stertę, listę okien, dock i GemFS bez blokad. Widoczny objaw to zgubione żądanie odświeżenia ekranu po otwarciu okna. `tools/smoke.sh` obchodzi to ruchem myszy i raportuje jako `lost-redraw race`.
- **Brak zapisu stanu FPU**, choć jądro liczy na `float` (`ui_scale`), także w przerwaniu myszy.
- **Brak twardych limitów:** niezerowany BSS, brak detekcji RAM, sterownik ATA bez timeoutów, GemFS bez sygnatury piszący po surowym dysku.

## Mapa repo

```text
boot/       stage1 (MBR) + stage2 (A20, E820, VBE, tryb chroniony, kopia jądra)
kernel/     kernel.c (kernel_main + pętla GUI), gdt/idt/isr, scheduler, process,
            elf, syscall, console (okna aplikacji hostowanych), heap, event,
            memory/ (paging), fs/ (GemFS), gfx/ (prymitywy, ikony, font),
            gui/ (WM, okna, topbar, pulpit), ui/ (dock, menu, kursor, fokus),
            app/ (rejestr aplikacji), font/ (TrueType, rasteryzer, cache, AA)
drivers/    serial, VBE/BGA, PIC, PIT, klawiatura, mysz, ATA PIO, RTC
lib/        string.c (implementacja include/string.h)
include/    freestanding stdint/stddef/stdbool/string/io + gemos/ (ABI userlandu)
apps/       aplikacje jądra i launchery programów userlandu
userland/   programy ring 3 (uterm2/, about/, textedit/, common/, crt0.S,
            usrsmoke.S, user_linker.ld)
assets/     font.ttf (Inter) + OFL.txt
tools/      smoke.sh + smoke.py (test w QEMU)
docs/       strona GitHub Pages + audyt kodu
```

## Budowanie i testy

- `make all` tworzy `build/gemos.img`. Wymaga `nasm` oraz `i686-elf-gcc` albo `x86_64-elf-gcc`; bez nich Makefile używa hostowego `gcc -m32`, tak jak CI.
- `tools/smoke.sh` buduje obraz, bootuje go w QEMU bez okna, uruchamia UTERM, ABOUT i UTEXTEDIT, sprawdza log i zrzuty ekranu. Musi skończyć się `SMOKE: PASS` przed każdym commitem. CI (`.github/workflows/ci.yml`) uruchamia to samo przy każdym pushu i PR.
- `make run` otwiera QEMU z dyskiem danych `build/data.img`; `make debug` dodatkowo czeka na GDB na porcie `:1234`.
- Makefile przerywa build, gdy `kernel.bin` przekracza limit loadera (`KERNEL_SECTORS` w `boot/stage2/loader.asm`).
- Zmiany, które nie powinny zmieniać zachowania, sprawdzaj porównaniem binariów (`build/kernel.bin`, `build/*.elf`) przed i po.

## Zasady architektury

- **32-bit, własny bootloader**, bez GRUB-a, bez libc i bez libgcc.
- **GUI zostaje w jądrze.** Userland rozmawia z systemem wyłącznie przez ABI z `include/gemos/` (`syscall_abi.h`, `console_abi.h`, `user_api.h`) i nie includuje nagłówków jądra.
- **Kody klawiszy** pochodzą tylko z `GEMOS_KEY_*` w `include/gemos/console_abi.h`.
- **Jeden `string.h`**: `include/string.h`, dołączany jako `<string.h>`.
- **Żadnych nowych funkcji przed naprawą współbieżności** (etap 3 audytu). Kolejność prac jest w §8.2 audytu.
