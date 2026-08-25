# Installing Mike

After this, you can run `mike file.mik` from anywhere — just like `gcc`.

## Requirements

- a C compiler (`cc` or `gcc`) — to build the Mike compiler itself
- `gcc` + `as` + `ld` — Mike uses these to assemble and link your programs
  (already present on any normal Linux dev machine; on Debian/Ubuntu:
  `sudo apt install build-essential`)

## Quick install (per-user, no sudo)

From inside the `mike-lang/` folder:

```sh
./install.sh
```

This:
- builds the compiler from `mike2.c`
- installs the binary to `~/.local/bin/mike`
- copies the standard libraries to `~/.mike/lib`

If `~/.local/bin` isn't on your PATH yet, the installer tells you. Add it:

```sh
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

## System-wide install (all users, needs sudo)

```sh
sudo ./install.sh --system
```

Installs to `/usr/local/bin/mike` and `/usr/local/share/mike/lib`.

## Verify

```sh
cd /tmp
cat > hello.mik <<'EOF'
start module; hi
    lib input/output
    lib math
    func main()
        print "oke"
        print pi()
    end func
end modul
EOF
mike hello.mik && ./hello
```

You should see `oke` and `3.141593` — and note this works even though `/tmp`
has no `lib/` folder, because Mike finds the libraries in `~/.mike/lib`.

## Where Mike looks for libraries

When you write `lib math`, the compiler searches for `math.mik` in this order:

1. `./lib/` next to your source file        (project-local libraries)
2. `$MIKE_LIB`                               (environment override)
3. `~/.mike/lib`                             (your per-user install)
4. `/usr/local/share/mike/lib`              (system install)

So a library file in your project folder overrides the installed one — handy
for developing your own libraries without touching the global set.

## Writing your own installed library

```sh
cat > ~/.mike/lib/pvt.mik <<'EOF'
start module; pvt
    func rs_standing(gamma_g, api, p, t)
        # ... your correlation ...
        return 0.0
    end func
end modul
EOF
```

Then `lib pvt` in any program, anywhere.

## Uninstall

```sh
./uninstall.sh              # per-user
sudo ./uninstall.sh --system   # system-wide
```

## Command reference

```sh
mike program.mik       # compile -> ./program (native executable)
mike -S program.mik    # emit program.s (assembly) and stop
```
