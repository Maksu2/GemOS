# Build and Debug

## Tooling

Recommended:

- `nasm`
- `make`
- `qemu-system-i386`
- `i686-elf-*` or equivalent freestanding cross-toolchain

## Build

```bash
make clean
make all
```

## Run

```bash
make run
```

## Debug

Start QEMU with GDB stub:

```bash
make debug
```

Then in another shell:

```bash
i686-elf-gdb build/kernel.elf
target remote :1234
```

## Useful bring-up focus points

- boot chain logs on serial
- paging enable and first `CR3` switches
- ISR / fault path
- scheduler tick and context switch path
- first `iret` into ring 3
- syscall dispatch
- ELF load and process spawn path

## Common verification goals

- system boots repeatedly without triple fault
- desktop stays alive after user process exit
- bad user process dies without panicking the kernel
- normal userland path still works after fault tests
