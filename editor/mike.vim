" Vim syntax file untuk bahasa Mike
" Pasang: lihat editor/README_EDITOR.md
"
" Letakkan file ini di ~/.vim/syntax/mike.vim, lalu daftarkan ekstensi .mik
" (lihat README). Setelah itu, buka file .mik apa pun -> otomatis berwarna.

if exists("b:current_syntax")
    finish
endif

" komentar: # sampai akhir baris
syn match mikeComment "#.*$"

" string (dengan escape \" di dalamnya)
syn region mikeString start=+"+ skip=+\\"+ end=+"+

" angka: integer, desimal, notasi ilmiah
syn match mikeNumber "\<\d\+\(\.\d*\)\?\([eE][-+]\?\d\+\)\?\>"

" keyword struktur modul
syn keyword mikeStructure start module modul end

" keyword kontrol alur
syn keyword mikeControl if else while for to return

" deklarasi
syn keyword mikeDeclaration func var lib asm

" operator logika (kata)
syn keyword mikeOperator and or not

" builtin: I/O, math, file, plot
syn keyword mikeBuiltin print input array
syn keyword mikeBuiltin sqrt exp ln log10 sin cos tan floor ceil abs pow
syn keyword mikeBuiltin random seed rand_range gauss
syn keyword mikeBuiltin csv_load csv_rows
syn keyword mikeBuiltin file_open file_str file_num file_close
syn keyword mikeBuiltin put_char put_str put_num

" pemetaan ke grup warna standar vim
hi def link mikeComment      Comment
hi def link mikeString       String
hi def link mikeNumber       Number
hi def link mikeStructure    Keyword
hi def link mikeControl      Conditional
hi def link mikeDeclaration  Type
hi def link mikeOperator     Operator
hi def link mikeBuiltin      Function

let b:current_syntax = "mike"
