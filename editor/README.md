# Syntax Highlighting Mike untuk Editor

Warna sintaks bikin kode `.mik` jauh lebih enak dibaca: keyword, string, angka,
komentar, dan fungsi bawaan masing-masing punya warna. Pilih editor kamu.

Skema warna yang dipakai (kurang lebih sama di semua editor):
- komentar `# ...`   -> abu-abu
- string `"..."`     -> hijau
- angka              -> magenta/ungu
- start/module/end   -> kuning
- if/else/while/for  -> biru
- func/var/lib/asm   -> cyan
- and/or/not         -> merah
- print/sqrt/dst.    -> hijau terang (fungsi bawaan)

---

## 1. nano  (paling cepat)

1. Salin `mike.nanorc` ke tempat tetap, misalnya `~/.mike/mike.nanorc`:
   ```sh
   mkdir -p ~/.mike
   cp editor/mike.nanorc ~/.mike/
   ```
2. Tambahkan baris ini ke `~/.nanorc` (buat file itu bila belum ada):
   ```
   include "~/.mike/mike.nanorc"
   ```
   Catatan: sebagian nano lama tidak mengembangkan `~`. Bila warna tak muncul,
   tulis path lengkap, misalnya:
   ```
   include "/home/mike/.mike/mike.nanorc"
   ```
3. Buka file `.mik`:
   ```sh
   nano hello.mik
   ```
   Sekarang berwarna.

---

## 2. vim  (paling kuat di terminal)

1. Buat folder syntax vim dan salin file-nya:
   ```sh
   mkdir -p ~/.vim/syntax
   cp editor/mike.vim ~/.vim/syntax/mike.vim
   ```
2. Daftarkan ekstensi `.mik` agar dikenali sebagai bahasa `mike`. Tambahkan ke
   `~/.vimrc`:
   ```vim
   au BufRead,BufNewFile *.mik set filetype=mike
   ```
   (Bila `~/.vimrc` belum ada, buat saja berisi baris di atas. Pastikan juga
   `syntax on` aktif — tambahkan baris `syntax on` bila belum ada.)
3. Buka file `.mik`:
   ```sh
   vim hello.mik
   ```

Untuk **neovim**, langkahnya sama tetapi foldernya
`~/.config/nvim/syntax/mike.vim` dan pengaturan di `~/.config/nvim/init.vim`.

---

## 3. VS Code

Folder `vscode-mike/` adalah extension siap pasang.

Cara termudah (mode pengembang, tanpa publish):

1. Salin seluruh folder `vscode-mike` ke direktori extensions VS Code:
   - Linux:   `~/.vscode/extensions/`
   - (hasil akhir: `~/.vscode/extensions/vscode-mike/`)
   ```sh
   cp -r editor/vscode-mike ~/.vscode/extensions/mike-lang
   ```
2. Tutup dan buka ulang VS Code sepenuhnya.
3. Buka file `.mik` — highlighting aktif. Bila belum, klik pemilih bahasa di
   kanan bawah dan pilih "Mike", atau jalankan perintah
   "Developer: Reload Window".

Untuk mengemas jadi `.vsix` yang bisa dibagikan, pasang `vsce`
(`npm install -g @vscode/vsce`) lalu jalankan `vsce package` di dalam folder
`vscode-mike`. Ini opsional.

---

## Menambah keyword baru

Bila nanti kamu menambah builtin atau keyword ke Mike, cukup tambahkan namanya
ke daftar di ketiga file: bagian `color brightgreen ...` (nano), baris
`syn keyword mikeBuiltin ...` (vim), dan pola `builtins` (VS Code JSON). Warna
akan langsung ikut.
