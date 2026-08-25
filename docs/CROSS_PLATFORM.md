# Mike Cross-Platform (Unix)

Mike berjalan di **x86-64 Unix**: Linux dan FreeBSD (juga turunan yang
menyediakan libc + toolchain C standar).

## Bagaimana caranya

Mike mengompilasi ke assembly x86-64, lalu meng-assemble & link lewat compiler
C sistem (`cc`). Semua operasi I/O ke sistem operasi (menulis ke layar, membaca
input, file) dilakukan lewat **fungsi libc** (`write`, `read`, `fopen`,
`malloc`, `fprintf`), bukan lewat nomor syscall mentah.

Ini kuncinya: nomor syscall berbeda antar-OS (di Linux `write`=1, di FreeBSD
`write`=4), tetapi fungsi libc `write()` sama di mana-mana — libc tiap OS yang
menerjemahkannya ke syscall yang benar. Jadi assembly yang sama jalan di Linux
maupun FreeBSD tanpa perubahan.

## Toolchain

- **Linux**: `cc` = gcc. Pasang `build-essential` bila belum ada.
- **FreeBSD**: `cc` = clang (sudah termasuk di base system). Tidak perlu gcc.

Linker memakai `cc -no-pie`; bila toolchain menolak opsi itu, Mike otomatis
mencoba tanpa `-no-pie`.

## Instalasi

Sama di kedua OS:

```sh
unzip mike-lang.zip
cd mike-lang
./install.sh          # per-user: ~/.local/bin + ~/.mike/lib
```

Installer memakai `/bin/sh` (POSIX), mendeteksi OS, dan membangun compiler dari
sumber dengan `cc` yang ada. Pastikan direktori bin ada di PATH:

```sh
# Linux (bash):
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc && . ~/.bashrc
# FreeBSD (sh):
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.profile && . ~/.profile
```

Lalu:

```sh
mike hello.mik && ./hello
```

## Windows?

Belum. Windows tidak memakai model ELF + syscall/libc yang sama; ia butuh
backend PE/COFF dan pemanggilan lewat DLL (kernel32). Itu praktis merupakan
backend baru, direncanakan sebagai pekerjaan terpisah di masa depan. Untuk
sekarang, di Windows gunakan WSL (Windows Subsystem for Linux) — di dalamnya
Mike berjalan sebagai program Linux biasa.
