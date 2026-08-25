# Membuat Library Sendiri di Mike

Kamu bisa menulis file `.mik` sebagai library dan memanggilnya dari file `.mik`
lain — mirip `import` di D atau `#include` di C. Ini fitur inti Mike, bukan
sesuatu yang khusus.

## Langkah 1 — Tulis library-nya

Sebuah library adalah modul biasa yang isinya fungsi-fungsi yang ingin kamu
pakai ulang. Contoh, simpan sebagai `lib/mypvt.mik`:

```
start module; mypvt
    func api_to_sg(api)
        return 141.5 / (api + 131.5)
    end func

    func gor_standing(pressure, temp, api, gas_grav)
        var x = 0.0125 * api - 0.00091 * temp
        return gas_grav * pow(pressure / 18.2 + 1.4, 1.2048) * pow(10.0, x)
    end func
end modul
```

Nama modul (`mypvt` di sini) tidak harus sama dengan nama file, tapi nama
FILE-nya yang menentukan cara memanggil: `mypvt.mik` dipanggil dengan
`lib mypvt`.

## Langkah 2 — Panggil dari program lain

```
start module; hitung
    lib input/output
    lib mypvt              # <-- memuat fungsi-fungsi dari lib/mypvt.mik

    func main()
        print api_to_sg(35.0)
        print gor_standing(2000.0, 180.0, 35.0, 0.7)
    end func
end modul
```

Jalankan:

```sh
mike hitung.mik && ./hitung
```

## Di mana Mike mencari library

Saat kamu menulis `lib mypvt`, compiler mencari `mypvt.mik` berurutan di:

1. `./lib/` di sebelah file sumbermu       ← library proyek (paling umum)
2. `$MIKE_LIB`                              ← override lewat environment variable
3. `~/.mike/lib`                            ← library ter-install per-user
4. `/usr/local/share/mike/lib`             ← library ter-install sistem

Jadi taruh library buatanmu di folder `lib/` di samping programmu, atau salin
ke `~/.mike/lib` supaya bisa dipanggil dari mana saja seperti library bawaan.

## Aturan penting

- **Library hanya berisi fungsi.** Jangan taruh `func main()` di library — itu
  untuk program yang menjalankannya.
- **Library boleh memanggil library lain.** Semua fungsi library di-inject
  sebelum `main`, jadi urutan tidak masalah — sebuah library boleh memanggil
  fungsi dari library lain yang juga kamu `lib`-kan.
- **Library boleh memanggil builtin** (`sqrt`, `pow`, `exp`, `sin`, dst.) dan
  fungsi yang kamu definisikan di programmu (forward reference didukung).
- **Satu file = satu library.** Nama file (tanpa `.mik`) adalah nama yang kamu
  pakai di `lib`.

## Contoh: menyusun library bertingkat

Library boleh dibangun di atas library lain. Misalnya `lib/reservoir.mik`
memakai `lib math`:

```
start module; reservoir
    # butuh: lib math (untuk pi) di program pemanggil
    func pore_volume(area, thickness, porosity)
        return area * thickness * porosity
    end func

    func stoiip(area, h, phi, sw, boi)
        return 7758.0 * area * h * phi * (1.0 - sw) / boi
    end func
end modul
```

Lalu di program:

```
start module; lapangan
    lib input/output
    lib math
    lib reservoir

    func main()
        print stoiip(500.0, 45.0, 0.22, 0.30, 1.28)
    end func
end modul
```

Dengan pola ini kamu bisa membangun koleksi library domain sendiri — PVT,
material balance, decline curve — dan memakainya di banyak program.
