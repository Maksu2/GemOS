# Procedura Weryfikacji Manualnej GemOS (Phase 2.6)

## 1. Wstęp — Zakres weryfikacji

Celem tego dokumentu jest umożliwienie ręcznego potwierdzenia stabilności i poprawności fundamentu systemu GemOS. Na obecnym etapie system **nie posiada** jeszcze graficznego interfejsu użytkownika (GUI), okien, ani możliwości uruchamiania programów użytkownika.

**System działa poprawnie, jeśli:**
1. Uruchamia się powtarzalnie bez błędów.
2. Reaguje na przerwania sprzętowe (Timer, Klawiatura, Mysz).
3. Loguje zdarzenia na porcie szeregowym (Serial COM1).
4. Nie wiesza się (freeze) ani nie restartuje samoczynnie (triple fault).

Weryfikacja odbywa się poprzez obserwację:
1. Ekranu QEMU (statyczny obraz / gradient).
2. Logów Serial (plik `build/serial.log` lub wyjście terminala).

---

## 2. Weryfikacja Bootloadera

**Jak uruchomić:**
Użyj polecenia `make run` w terminalu.

**Oczekiwany rezultat:**
1. QEMU uruchamia się.
2. Na ekranie pojawia się gradient (niebieski do czarnego) lub zdefiniowany wzór graficzny.
3. W logach serial pojawiają się komunikaty startowe:
   - `[BOOT] GemOS Kernel Starting...`
   - `[BOOT] VBE Mode Info...`

**Typowe problemy:**
- **Czarny ekran + brak logów:** Błąd w Stage 1 lub Stage 2 (złe ładowanie z dysku).
- **Restart w pętli (QEMU miga):** Triple Fault przy przejściu do Protected Mode (błędne GDT/IDT).
- **Zawieszenie na "Booting from Floppy...":** BIOS nie wykrył sygnatury bootloadera (0x55AA).

---

## 3. Weryfikacja Protected Mode

**Jak sprawdzić:**
Obserwuj logi serial zaraz po starcie.

**Symptomy poprawnego działania:**
1. Kernel wykonuje kod w C (funkcja `kernel_main`).
2. Dostępna jest pamięć powyżej 1MB (kernel jest załadowany pod `0x100000`).
3. Adresy wskaźników w logach (np. Framebuffer) są 32-bitowe (np. `0xFD000000`).

**Potwierdzenie:**
Jeśli widzisz log `[BOOT] Kernel initialization complete`, system poprawnie działa w 32-bitowym trybie chronionym.

---

## 4. Weryfikacja IDT i Wyjątków CPU

System podczas startu wykonuje autotest wyjątków.

**Procedura:**
1. Sprawdź logi serial w sekcji `[TEST] --- Starting System Verification`.
2. Szukaj linii: `[TEST] 1. Breakpoint Exception (INT 3)... PASS`.

**Co to oznacza:**
- Kernel celowo wywołał instrukcję `int $3`.
- Procesor przerwał wykonanie i skoczył do handlera w IDT (Vector 3).
- Handler `isr_handler` rozpoznał wyjątek, zalogował go i **pozwolił na kontynuację** (zamiast paniki).

**Zachowanie błędne:**
- System zatrzymuje się z komunikatem `[PANIC] CPU Exception: Breakpoint`.
- System restartuje się (Triple Fault - brak handlera IDT).

---

## 5. Weryfikacja PIC i IRQ

System dokonuje remapowania kontrolera przerwań (PIC), aby uniknąć konfliktów z wyjątkami CPU.

**Procedura:**
1. Sprawdź logi serial.
2. Szukaj linii: `[BOOT] PIC initialized (0x20/0x28)`.
3. Szukaj testu IRQ: `[TEST] 2. IRQ0 Stub Trigger (INT 32)... PASS`.

**Interpretacja:**
- PIC Master obsługuje przerwania 0x20-0x27 (IRQ 0-7).
- PIC Slave obsługuje przerwania 0x28-0x2F (IRQ 8-15).
- Programowe wywołanie `int $32` symuluje przerwanie sprzętowe sterownika Timera.
- Sukces oznacza, że IDT poprawnie wskazuje na handler IRQ, a handler wysyła sygnał EOI (End of Interrupt).

**Błąd krytyczny:**
- Komunikat `[PANIC] ... Double Fault` lub `General Protection Fault` przy włączeniu przerwań (`sti`).

---

## 6. Weryfikacja Timera (PIT)

Timer jest sercem systemu ("bije" 1000 razy na sekundę).

**Procedura:**
1. Po uruchomieniu systemu nie zamykaj QEMU przez co najmniej 10-20 sekund.
2. Obserwuj logi serial.
3. Powinieneś widzieć cykliczne komunikaty (co około sekundy):
   ```
   [TIMER] 1 second passed (Ticks: 1000)
   ...
   [TIMER] 1 second passed (Ticks: 2000)
   ```

**Interpretacja:**
- **Regularne logi:** PIT działa stabilnie, przerwania nie są blokowane.
- **Brak logów:** Przerwania wyłączone lub PIC źle skonfigurowany.
- **Logi znikają po chwili:** Wyciek pamięci, stack overflow lub nieskończona pętla w innym handlerze blokująca przerwania.

---

## 7. Weryfikacja Klawiatury (PS/2)

**Procedura:**
1. Kliknij myszką w okno QEMU, aby przekazać focus.
2. Pisz na klawiaturze (litery A-Z, cyfry).
3. Obserwuj logi serial.

**Oczekiwany rezultat:**
Dla każdego naciśnięcia klawisza powinien pojawić się log:
```
[KEY] Down: A
[KEY] Down: B
```

**Analiza:**
- **Key Down:** Pojawia się w momencie wciśnięcia.
- **Brak reakcji:** Upewnij się, że okno QEMU jest aktywne.
- **Dziwne znaki:** System na razie obsługuje tylko podstawowy Set 1 (US QWERTY). Klawisze specjalne mogą być ignorowane lub logowane jako `Unknown Scancode`.

---

## 8. Weryfikacja Myszy (PS/2)

**Procedura:**
1. Upewnij się, że QEMU przechwyciło mysz (tytuł okna QEMU powinien to sugerować, lub kursor systemowy zniknie).
2. Ruszaj myszą w różnych kierunkach.
3. Klikaj przyciski myszy.

**Oczekiwany rezultat:**
System obecnie **nie loguje każdego ruchu myszy** domyślnie, aby nie zalewać logów (chyba że włączono debug w `drivers/mouse.c`).
Aby zweryfikować mysz bez logów (jeśli są wyłączone):
- Sprawdź komunikat startowy: `[MOUSE] Driver initialized (IRQ12)`.
- Jeśli system nie crashuje podczas ruchu myszą, oznacza to, że przerwania są poprawnie odbierane i przetwarzane (i EOI jest wysyłane), nawet jeśli są "milczące".

*(Opcjonalnie: Włącz logowanie w `drivers/mouse.c` i rekompiluj, aby widzieć `X=... Y=...`)*

---

## 9. Test Obciążeniowy (Stress Test)

Sprawdzenie stabilności przy pełnym obciążeniu przerwaniami.

**Procedura:**
1. Uruchom system.
2. Poczekaj, aż Timer zacznie logować (system żyje).
3. Zacznij chaotycznie ruszać myszą w oknie QEMU.
4. Jednocześnie naciskaj losowe klawisze na klawiaturze.
5. Kontynuuj przez 30 sekund.

**Kryterium sukcesu:**
- Timer nadal loguje `[TIMER] 1 second passed ...` (nie został zagłodzony).
- Klawiatura nadal loguje znaki (nie gubi przerwań).
- System nie zalicza Panic/Crash.
- Nie pojawia się Double Fault.

---

## 10. Najczęstsze Problemy i Interpretacja

| Objaw | Prawdopodobna Przyczyna | Rozwiązanie |
|-------|-------------------------|-------------|
| **Cisza w logach (brak timera i inputu)** | Przerwania wyłączone (`cli`) lub PIC zmaskowany. | Sprawdź `kernel_main` pod kątem `sti`. |
| **System działa chwilę, potem staje** | Brak EOI w handlerze przerwania. | PIC czeka na potwierdzenie i blokuje kolejne IRQ. Sprawdź `pic_send_eoi`. |
| **Zdublowane znaki klawiatury** | Błędne odczytywanie statusu lub brak pętli wait. | Sprawdź timeouty w `keyboard.c`. |
| **Crash przy ruchu myszą** | Przepełnienie stosu lub błąd wskaźnika w handlerze. | Sprawdź `mouse_callback`. |
| **Unknown Interrupt ISR 39/47** | Spurious Interrupt (zakłócenia sprzętowe). | To normalne w prawdziwym sprzęcie, w QEMU rzadkie. Można zignorować. |

---

## 11. Kryterium Zakończenia Weryfikacji

Jeśli:
- [ ] System bootuje do pętli głównej.
- [ ] Timer odlicza czas w logach.
- [ ] Klawiatura reaguje na wciśnięcia.
- [ ] Mysz nie powoduje crasha systemu.
- [ ] Test obciążeniowy nie wywraca kernela.

**Wtedy fundament GemOS (Phase 2) uznajemy za stabilny i gotowy do implementacji warstwy wizualnej (GUI).**

---

## 12. Weryfikacja Phase 3.1 (Heap & Events)

Phase 3.1 wprowadziło dynamiczną alokację pamięci i kolejkę zdarzeń.

**Procedura:**
1. Uruchom `make run`.
2. Nie zamykaj QEMU.
3. Kliknij myszą w oknie QEMU.
4. Naciśnij klawisz 'p' na klawiaturze.

**Oczekiwany rezultat w logach (Serial):**
1. Na starcie:
   ```
   [HEAP] Initialized at 0x... with size: ...
   [EVENT] Event Queue initialized
   ```
2. Po kliknięciu myszą:
   ```
   [KERNEL] Mouse Click via Event Queue
   ```
   *(Uwaga: Może wymagać kilku kliknięć lub ruchu, jeśli jest "debounce" lub inne filtry)*
3. Po naciśnięciu 'p':
   ```
   [KERNEL] 'p' pressed via Event Queue
   ```

**Interpretacja:**
Jeśli widzisz te komunikaty, oznacza to, że cała ścieżka: Sprzęt -> Przerwanie -> Driver -> EventQueue -> Kernel Loop działa poprawnie.

---

## 13. Weryfikacja Phase 3.2 (Graphics Core & Clipping)

Phase 3.2 wprowadziło silnik renderujący z obsługą przycinania (clipping).

**Procedura:**
1. Uruchom `make run`.
2. Obserwuj ekran graficzny (nie logi).

**Oczekiwany rezultat wizualny:**
1. Ciemnoszare tło.
2. Dwie białe ramki (kontury).
3. **Czerwony kwadrat** w lewej ramce:
   - Porusza się w prawo.
   - Gdy "wychodzi" poza prawą krawędź białej ramki, **ucina się idealnie na linii**. Nie rysuje po tle.
4. **Zielony prostokąt** w prawej ramce:
   - Porusza się w dół.
   - Gdy "wychodzi" poza dolną krawędź, również się ucina.

**Interpretacja:**
To dowodzi, że `gfx_set_clip` i `gfx_fill_rect` działają poprawnie. Jest to matematyczny dowód na to, że w przyszłości okna nie będą rysować po sobie nawzajem w niedozwolony sposób.

---

## 14. Weryfikacja Phase 3.2.1 (Rendering Fixes)

Phase 3.2.1 naprawiło problemy z adresowaniem pikseli (powodujące pionowe pasy).

**Procedura:**
1. Uruchom `make run`.
2. Obserwuj ekran.

**Oczekiwany rezultat wizualny:**
1. **Tło**: Płynny gradient pionowy (Niebieski u góry -> Czarny u dołu).
2. **Gradient Poziomy**: Cienki pasek (Czerwony -> Czarny) animowany w poziomie.
3. **Pionowe Linie**: Białe, pionowe linie co 100 pikseli.
4. **Brak Artefaktów**:
   - Gradienty muszą być gładkie (bez pionowych pasów "jail bars").
   - Białe linie muszą być idealnie proste (nie poszarpane).
   - Kolory muszą być czyste (niebieski to niebieski, czerwony to czerwony).

**Interpretacja:**
Jeśli obraz jest czysty, oznacza to, że GemOS poprawnie obsługuje tryb graficzny (niezależnie czy QEMU wybrało 24 czy 32 bpp) i adresowanie pamięci jest precyzyjne co do bajtu.

---

## 15. Weryfikacja UX Stabilization

Mamy teraz podstawowy pulpit i kursor.

**Procedura:**
1. Uruchom `make run`.
2. Poruszaj myszą.
3. Kliknij lewym przyciskiem myszy.

**Oczekiwany rezultat wizualny:**
1. **Tło**: Jednolity kolor Teal (#008080). Żadnych gradientów testowych.
2. **Kursor**: Biała strzałka z czarnym obrysem.
3. **Ruch**: Kursor płynnie podąża za myszą, **nie zmazuje tła** i nie zostawia śladów.
4. **Logi (Serial)**: Kliknięcie powoduje wpis `[KERNEL] Click at XxY`.

**Interpretacja:**
System działa w trybie "Desktop". Mamy fundament pod okna.

---

## 16. Weryfikacja UX-FIX-1 (Obowiązkowa)

W tej fazie naprawiamy niewidoczny kursor metodą "Brute Force".

**Procedura:**
1. Uruchom `make run`.
2. Poruszaj myszą.

**Oczekiwany rezultat wizualny:**
1. **Tło**: Jednolity kolor Teal (#008080).
2. **Kursor**: **CZERWONY KWADRAT 10x10**.
   - Musi być widoczny.
   - Musi się poruszać.
   - Może lekko migać (to normalne w tej fazie, brak double-bufferingu).

**Interpretacja:**
Jeśli widzisz czerwony kwadrat, znaczy to, że pipeline `Event -> Update -> Render` działa poprawnie. Problemem była wcześniejsza, zbyt skomplikowana logika (backing store/clipping). Teraz możemy budować dalej.

---

## 17. Weryfikacja UX-FIX-2 (State Wiring)

W tej fazie połączyliśmy sterownik myszy bezpośrednio z rendererem.

**Procedura:**
1. Uruchom `make run`.
2. Poruszaj myszą w różnych kierunkach.

**Oczekiwany rezultat wizualny:**
1. **Czerwony Kwadrat**:
   - Musi **idealnie podążać** za ruchem myszy.
   - Musi reagować natychmiastowo.
   - Nie może znikać na brzegach ekranu (clamp).

**Interpretacja:**
Jeśli kursor się rusza, mamy działający Desktop.

---

## 18. Weryfikacja Phase 3.4 (Window Manager)

Mamy teraz prawdziwe okna!

**Procedura:**
1. Uruchom `make run`.
2. Zobaczysz Teal Desktop i **DWA OKNA**.
   - Okno 1 (100, 100) - powinno być pod spodem (nieaktywne, szara ramka).
   - Okno 2 (200, 150) - powinno być na wierzchu (aktywne, cyjanowa ramka).
3. **Kliknij** na Okno 1 (to pod spodem).

**Oczekiwany rezultat wizualny:**
1. Okno 1 wskakuje na wierzch.
2. Ramka Okna 1 zmienia się na **Cyjan**.
3. Ramka Okna 2 zmienia się na **Szary**.
4. Kursor nadal swobodnie lata nad wszystkim.

**Interpretacja:**
Jeśli to działa, GemOS oficjalnie posiada Window Manager.

---

## 19. Weryfikacja Phase 3.5 (Window Dragging)

Mamy interaktywny desktop!

**Procedura:**
1. Uruchom `make run`.
2. Zwróć uwagę na terminal: **Cisza**. Brak spamu IRQ.
3. Kliknij i przytrzymaj **Belkę Tytułową** (Title Bar) dowolnego okna.
4. Przesuń mysz.

**Oczekiwany rezultat wizualny:**
1. Okno przesuwa się razem z kursorem.
2. Okno jest "sztywne" (nie miga, nie skacze).
3. Puszczenie przycisku myszy kończy przesuwanie.

**Interpretacja:**
Jeśli możesz układać okna na ekranie, mamy działający GUI.

---

## 20. Weryfikacja Phase 3.6.1 (Double Buffering)

Egzamin ostateczny dla silnika graficznego.

**Procedura:**
1. Uruchom `make run`.
2. **"Stress Test"**:
   - Machaj myszką jak szalony.
   - Łap okna i rzucaj nimi po ekranie.

**Oczekiwany rezultat wizualny:**
1. **Zero Migotania**: Ani jednego "błysku" tła.
2. **Zero Tearing**: Okna nie "pękają" w połowie podczas ruchu.
3. Obraz jest stabilny jak skała.

**Interpretacja:**
Jeśli to działa, mamy profesjonalny pipeline graficzny (Input -> State -> Backbuffer -> VBE).
Jesteśmy gotowi na Top Bar.

---

## 21. Weryfikacja Phase 3.6.2 (60 FPS)

Boost wydajności.

**Procedura:**
1. Uruchom `make run`.
2. Poruszaj szybko myszą.

**Oczekiwany rezultat wizualny:**
1. Kursor porusza się **bardzo płynnie**.
2. Brak wrażenia "skakania" (stutteringu).
3. Obraz nadal stabilny (brak tearingu).

**Interpretacja:**
System działa w 60 FPS.

---

## 22. Weryfikacja Phase 3.7 (Top Bar)

Testujemy globalny pasek menu.

**Procedura:**
1. Uruchom `make run`.
2. Zobacz górę ekranu: Powinien tam być ciemnoszary pasek (24px).
3. **Test interakcji:**
   - Przeciągnij okno na samą górę. Nie powinno wejść "pod" pasek (belka tytułowa okna zatrzymuje się pod paskiem).
   - Kliknij w pasek. Terminal powinien wypisać `[TOPBAR] Clicked!`.
   - Kliknięcie w pasek nie powinno zmieniać focusu okien poniżej.

**Interpretacja:**
Jeśli pasek jest "sztywny" i zawsze na wierzchu - mamy Global UI Layer.

---

## 23. Weryfikacja Phase 3.7.1 (Tekst)

Testujemy czytelność interfejsu.

**Procedura:**
1. Uruchom `make run`.
2. Spójrz na Top Bar.

**Oczekiwany rezultat wizualny:**
1. Zamiast prostokątów widzisz wyraźne napisy:
   - "GemOS" (biały)
   - "File", "Edit", "View", "App" (szare/białe)
   - "12:34" (zegar)
2. Tekst jest piksel-perfect (8x8 bitmapa).

**Interpretacja:**
GemOS umie mówić (wyświetlać tekst).

---

## 24. Weryfikacja Phase 3.8 (Hi-DPI Font)

Testujemy nowoczesną typografię.

**Procedura:**
1. Uruchom `make run`.
2. Zobacz Top Bar.
3. Czy wysokość paska jest trochę większa (28px)?
4. Czy napis "GemOS" jest wyższy i smuklejszy (10x16)?

**Oczekiwany rezultat wizualny:**
1. Tekst wygląda na "gęstszy" (więcej pikseli).
2. Litery mają ładne kształty (np. 'e' ma okrągłe oko, 'm' ma łuki).
3. Wygląda to jak system z lat 90-tych, a nie 80-tych.

**Interpretacja:**
Mamy fundament pod poważny UI.

---

## 25. Weryfikacja Phase 3.8.1 (Hard Integration)

Potwierdzamy śmierć starego fontu.

**Procedura:**
1. Uruchom `make run`.
2. Czy system wstaje? (Jeśli tak, to nowy font działa, bo stary skasowaliśmy).
3. Czy widzisz log `[BOOT] Font System Initialized (10x16)` w terminalu?

**Interpretacja:**
Nie ma powrotu do 8-bitów.

---

## 26. Weryfikacja Phase 3.8.2 (Vector Engine)

Potwierdzamy skalowalność.

**Procedura:**
1. Uruchom `make run`.
2. Spójrz na "GemOS" (16px) i Menu (14px).
3. Czy litery składają się z cienkich linii (nie bloków)?
4. Czy "GemOS" jest wyraźnie większy od "File"?

**Interpretacja:**
JEŚLI widzisz różnicę rozmiaru -> mamy wektory.
JEŚLI widzisz cienkie linie -> mamy wektory.
To jest koniec epoki bitmap w GemOS.

---

## 27. Weryfikacja Phase 3.8.3 (Filled Fonts)

Potwierdzamy "mięsistość" czcionki.

**Procedura:**
1. Uruchom `make run`.
2. Spójrz na literę "G" w "GemOS".
3. Czy jest to pełny kształt (nie kontur)?
4. Czy "GemOS" (16px) wygląda solidnie i wyraźnie?

**Interpretacja:**
To jest docelowy engine fontów. Solidny, skalowalny, wektorowy.

---

## 28. Weryfikacja Phase 3.8.4 (Blocky Legibility)

Potwierdzamy CZYTELNOŚĆ.

**Procedura:**
1. Uruchom `make run`.
2. Przeczytaj menu: "GemOS", "File", "Edit".
3. Czy litery "e" mają wyraźną dziurę (nie kleks)?
4. Czy "G" wygląda jak G, a nie O?

**Interpretacja:**
Font może być brzydki (retro), ale MUSI być czytelny.
Jeśli jest czymy -> OK.

---

## 29. Weryfikacja Phase 3.8.6 (Typographic Quality)

Potwierdzamy 'pro' wygląd.

**Procedura:**
1. Uruchom `make run`.
2. Spójrz na literę "e" (małe).
3. Czy ma poziomy pasek i otwarte oko?
4. Czy "GemOS" (16px) ma poprawne proporcje (G duże, e małe ale nie mikroskopijne)?
5. Czy "12:34" wygląda jak zegar (cyfry tabularne)?

**Interpretacja:**
To jest finalny font. Jeśli jest czytelny i stabilny, zamykamy temat fontów.

---

## 30. Weryfikacja Phase 3.8.7 (System Native Look)

Potwierdzamy "nudę" systemową.

**Procedura:**
1. Uruchom `make run`.
2. Spójrz na "GemOS". Czy wygląda jak logo systemu, czy jak tekst?
3. Spójrz na 'e' i 'a'. Czy wyglądają jak w przeglądarce (Roboto/Arial)?
4. Czy tekst jest "niewidzialny" (czytasz treść, nie zwracając uwagi na kształt liter)?

**Interpretacja:**
Jeśli tak -> Mamy System Font.
Przechodzimy do UI.



















