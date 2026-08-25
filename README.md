# Mike

**Bahasa pemrograman untuk komputasi matematika, dikompilasi ke native x86-64.**

Mike adalah bahasa pemrograman kecil yang mudah dibaca namun mengompilasi kode
`.mik` langsung menjadi program executable native (bukan interpreter). Dirancang
untuk komputasi matematika, data, dan statistika, dengan 16 library siap pakai —
dari aljabar linear, statistik, keuangan, sains, hingga simulasi kuantum dan
rekayasa. Berjalan di Linux dan FreeBSD.

```
start module; hello
    lib input/output
    func main()
        print "oke"
    end func
end modul
```

```sh
mike hello.mik && ./hello
# -> oke
```

---

## Kenapa Mike?

- **Native, bukan interpreter.** Kode `.mik` → assembly x86-64 → executable ELF.
- **Fokus numerik.** Semua nilai bertipe `double`; bahasa dirancang untuk
  perhitungan, bukan aplikasi umum.
- **16 library.** Matematika, statistik, matriks, aljabar linear, keuangan,
  sains, metode numerik, bilangan kompleks, kuantum, kriptografi, mekanika,
  elektronika, kimia, plotting, dan lainnya.
- **Bisa bikin library sendiri.** Tulis file `.mik`, panggil dengan `lib namamu`.
- **Plotting.** Grafik ASCII langsung di terminal, atau export ke SVG.
- **Cross-platform Unix.** Linux dan FreeBSD (semua I/O lewat libc).

---

## Instalasi

Butuh compiler C (`cc`/`gcc` di Linux, `clang` di FreeBSD), plus `as` dan `ld`.

```sh
git clone https://github.com/safetest-dev/mike-lang.git
cd mike-lang
./install.sh
```

Installer membangun compiler dari `mike2.c`, memasang biner ke `~/.local/bin/mike`,
dan library ke `~/.mike/lib`. Pastikan `~/.local/bin` ada di PATH:

```sh
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.profile && . ~/.profile
```

Lihat [docs/INSTALL.md](docs/INSTALL.md) dan
[docs/CROSS_PLATFORM.md](docs/CROSS_PLATFORM.md) untuk detail.

---

## Contoh cepat

Menghitung dan mem-plot fungsi sinus langsung di terminal:

```
start module; sine
    lib input/output
    lib plot
    func main()
        var n = 40
        var xs = array(n)
        var ys = array(n)
        for i = 0 to n - 1
            var x = i / 4.0
            xs[i] = x
            ys[i] = sin(x)
        end for
        plot_show(xs, ys, n, 0.0, 10.0, -1.0, 1.0)
        return 0
    end func
end modul
```

Lebih banyak di folder [examples/](examples/).

---

## Bahasa singkat

- **Struktur:** program dibungkus `start module; <nama>` … `end modul`, eksekusi
  mulai dari `func main()`.
- **Variabel:** `var x = 10.0` (semua nilai `double`).
- **Operator:** `+ - * / %`, perbandingan `== != < <= > >=`, logika `and or not`.
- **Kontrol:** `if / else if / else`, `while`, `for i = a to b` (inklusif).
- **Fungsi:** `func nama(...)` … `return`, mendukung rekursi (maks 8 parameter).
- **Array:** `var a = array(n)`, akses `a[i]`.
- **Library:** `lib nama` memuat `lib/nama.mik`.

Referensi lengkap: [docs/Mike_Reference.pdf](docs/Mike_Reference.pdf) dan
tutorial [docs/Mike_Manual.pdf](docs/Mike_Manual.pdf).

---

## Library standar

| Library | Isi |
|---|---|
| `math` | konstanta & pembantu matematika |
| `stats` | statistika deskriptif, persentil, korelasi |
| `matrix` | matriks datar row-major (transpose, matmul, dll) |
| `regression` | regresi kuadrat terkecil |
| `geometry` | vektor & geometri 2D/3D |
| `finance` | matematika keuangan (npv, irr, pmt, dll) |
| `science` | konstanta fisika & konversi satuan |
| `numeric` | metode numerik (root, integral, ODE) |
| `linalg` | aljabar linear n×n (solve, inverse, determinant) |
| `complex` | bilangan kompleks |
| `quantum` | simulator komputasi kuantum |
| `crypto` | teori bilangan & kriptografi edukasi |
| `mechanic` | rekayasa mesin |
| `electric` | elektronika |
| `chem` | kimia |
| `plot` | plotting terminal (ASCII) & export SVG |

Ringkasan fungsi tiap library: [docs/README_LIB.md](docs/README_LIB.md).

---

## Membuat library sendiri

Tulis file `.mik` berisi fungsi (tanpa `main`), simpan sebagai `lib/namamu.mik`,
lalu panggil `lib namamu` — persis seperti library bawaan. Lihat
[docs/MEMBUAT_LIBRARY.md](docs/MEMBUAT_LIBRARY.md) dan contoh di
[examples/library_sendiri/](examples/library_sendiri/).

---

## Syntax highlighting

Definisi warna untuk vim, nano, dan VS Code ada di [editor/](editor/). Lihat
[editor/README_EDITOR.md](editor/README_EDITOR.md).

---

## Struktur repo

```
mike-lang/
├── mike2.c         compiler utama (edisi numerik)
├── mike.c          compiler awal (integer-only, untuk referensi)
├── install.sh      installer (Linux & FreeBSD)
├── uninstall.sh
├── lib/            16 library standar
├── examples/       contoh program
├── docs/           dokumentasi (PDF + markdown)
└── editor/         syntax highlighting (vim, nano, VS Code)
```

---

## Status & roadmap

Mike v0.1 — layak pakai. Fondasi bahasa, 16 library, plotting, cross-platform Unix.

Rencana ke depan: visualisasi 3D, sistem tipe (integer/hex), dan library
komputasi tingkat rendah. Prinsip pengembangan: *lebih dalam, bukan lebih lebar* —
setiap fitur harus memperkuat komputasi numerik.

macOS dan Windows di luar cakupan saat ini (butuh backend Mach-O/PE terpisah).

---

## Lisensi

[MIT](LICENSE) © 2026 safetest-dev
