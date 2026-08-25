# Mike

**A lightweight programming language for experimenting with mathematical algorithms.**

**A programming language designed by Michael Anggi, with AI-assisted
implementation, targeting native x86-64**

Mike is a small, readable programming language that compiles `.mik` source code
directly into a native executable — no interpreter involved. It is designed for
mathematical, data, and statistical computing, and ships with sixteen ready-to-use
libraries spanning linear algebra, statistics, finance, physics, quantum
simulation, cryptography, and engineering. It runs on Linux and FreeBSD.

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

## Why Mike?

- **Native, not interpreted.** `.mik` source -> x86-64 assembly -> ELF executable.
- **Numerically focused.** Every value is a `double`; the language is built for
  computation, not general-purpose applications.
- **Sixteen libraries.** Mathematics, statistics, matrices, linear algebra,
  finance, science, numerical methods, complex numbers, quantum simulation,
  cryptography, mechanics, electronics, chemistry, plotting, and more.
- **User-defined libraries.** Write a `.mik` file and call it with `lib yourname`.
- **Plotting.** ASCII charts right in the terminal, or export to SVG.
- **Cross-platform Unix.** Linux and FreeBSD (all I/O goes through libc).

---

## Installation

Requires a C compiler (`cc`/`gcc` on Linux, `clang` on FreeBSD), plus `as` and `ld`.

```sh
git clone https://github.com/MichaelAnggiGA/mike-lang.git
cd mike-lang
./install.sh
```

The installer builds the compiler from `mike2.c`, installs the binary to
`~/.local/bin/mike`, and the libraries to `~/.mike/lib`. Make sure
`~/.local/bin` is on your PATH:

```sh
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.profile && . ~/.profile
```

See [docs/INSTALL.md](docs/INSTALL.md) and
[docs/CROSS_PLATFORM.md](docs/CROSS_PLATFORM.md) for details.

---

## Quick example

Computing and plotting a sine wave directly in the terminal:

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

More in the [examples/](examples/) folder.

---

## The language in brief

- **Structure:** a program is wrapped in `start module; <name>` ... `end modul`,
  and execution begins at `func main()`.
- **Variables:** `var x = 10.0` (every value is a `double`).
- **Operators:** `+ - * / %`, comparisons `== != < <= > >=`, logic `and or not`.
- **Control flow:** `if / else if / else`, `while`, `for i = a to b` (inclusive).
- **Functions:** `func name(...)` ... `return`, with recursion (max 8 parameters).
- **Arrays:** `var a = array(n)`, indexed with `a[i]`.
- **Libraries:** `lib name` loads `lib/name.mik`.

Full reference: [docs/Mike_Reference.pdf](docs/Mike_Reference.pdf), and a
tutorial in [docs/Mike_Manual.pdf](docs/Mike_Manual.pdf).

---

## Standard libraries

| Library | Contents |
|---|---|
| `math` | mathematical constants & helpers |
| `stats` | descriptive statistics, percentiles, correlation |
| `matrix` | flat row-major matrices (transpose, matmul, etc.) |
| `regression` | least-squares regression |
| `geometry` | 2D/3D vectors & geometry |
| `finance` | financial mathematics (npv, irr, pmt, etc.) |
| `science` | physical constants & unit conversions |
| `numeric` | numerical methods (roots, integrals, ODEs) |
| `linalg` | general n x n linear algebra (solve, inverse, determinant) |
| `complex` | complex numbers |
| `quantum` | quantum computing simulator |
| `crypto` | number theory & educational cryptography |
| `mechanic` | mechanical engineering |
| `electric` | electronics |
| `chem` | chemistry |
| `plot` | terminal (ASCII) plotting & SVG export |

Function-by-function summary: [docs/README_LIB.md](docs/README_LIB.md).

---

## Writing your own library

Write a `.mik` file containing functions (no `main`), save it as
`lib/yourname.mik`, then call `lib yourname` — exactly like a built-in library.
See [docs/MEMBUAT_LIBRARY.md](docs/MEMBUAT_LIBRARY.md) and the example in
[examples/library_sendiri/](examples/library_sendiri/).

---

## Syntax highlighting

Color definitions for vim, nano, and VS Code are in [editor/](editor/). See
[editor/README_EDITOR.md](editor/README_EDITOR.md).

---

## Repository layout

```
mike-lang/
|-- mike2.c         main compiler (numeric edition)
|-- mike.c          early compiler (integer-only, for reference)
|-- install.sh      installer (Linux & FreeBSD)
|-- uninstall.sh
|-- lib/            16 standard libraries
|-- examples/       example programs
|-- docs/           documentation (PDF + markdown)
`-- editor/         syntax highlighting (vim, nano, VS Code)
```

---

## Status & roadmap

Mike v0.1 — usable. Language foundation, sixteen libraries, plotting, and
cross-platform Unix support are in place.

Planned next: 3D visualization, a type system (integers/hex), and a low-level
computation library. Development principle: *depth over breadth* — every feature
should strengthen numerical computing rather than broaden the language's scope.

macOS and Windows are out of scope for now (they would require separate
Mach-O/PE back ends).

---

## License

[MIT](LICENSE) (c) 2026 Michael Anggi Gilang Angkasa
