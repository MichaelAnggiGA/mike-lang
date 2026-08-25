# Mike standard library

`lib <name>` loads a library. The compiler looks for `<name>.mik` in a `lib/`
folder next to your program and splices its functions in before your code, so
you can call them directly. Multiple libraries stack:

```
start module; myprog
    lib input/output      # I/O + CSV (built into the compiler)
    lib math              # constants + helpers
    lib stats             # descriptive stats, percentiles, correlation
    lib matrix            # dense matrices (flat row-major arrays)
    lib regression        # least-squares fitting
    lib geometry          # 2D/3D vectors, areas

    func main()
        ...
    end func
end modul
```

## lib input/output  (built-in)

| Call                       | Result                                        |
|----------------------------|-----------------------------------------------|
| `print x` / `print "..."`  | write a number or string + newline            |
| `input "prompt"`           | read one number from the keyboard             |
| `csv_load("f.csv", arr)`   | read all numbers from a CSV into `arr`, returns count |
| `csv_rows("f.csv")`        | count numbers in a CSV without loading        |

CSV values may be separated by commas, spaces, or newlines — every number in
the file is read in order into the flat array. For (t, q) style tables, load
into one array and de-interleave with `raw[i*2]`, `raw[i*2+1]`.

## lib math

Constants `pi() e() tau()`; helpers `sq cube sign min2 max2 clamp lerp
rad deg round fact ipow`.  (The primitives `sqrt exp ln log10 sin cos tan
floor ceil abs pow` are compiler builtins, always available.)

## lib stats

`sum mean variance var_sample stdev stdev_sample amin amax range sort
percentile median skewness correlation covariance`.  `sort` is in-place;
`percentile`/`median` expect a sorted array.

## lib matrix

Matrices are flat `array(rows*cols)` in row-major order.
`mget mset mfill identity transpose matmul matvec trace det2 det3`.

## lib regression

`linreg(x, y, n, out)` writes `out[0]=intercept, out[1]=slope, out[2]=R^2`.
Also `predict mse rmse`, and `expfit` for `y = A*exp(B*x)` fits (writes
`out[0]=A, out[1]=B, out[2]=R^2`).

## lib geometry

2D: `dist2 dot2 mag2 cross2 cos_angle2 tri_area poly_area circle_area
circle_circ`.  3D: `dist3 dot3 mag3 cross3`.

## Worked example: production decline analysis

`declinefit.mik` reads a `(month, rate)` CSV, fits an exponential decline
`q = qi·exp(-D·t)` via regression on `ln(q)`, reports qi, D and R², forecasts a
future rate, and integrates an EUR — combining `input/output`, `math`,
`regression` in one program.

## Writing your own library

A library is just a normal module whose functions you want to share:

```
start module; mylib
    func my_helper(x)
        return x * 2.0
    end func
end modul
```

Save it as `lib/mylib.mik`, then `lib mylib` in any program. Libraries can call
compiler builtins and other libraries' functions (load order handles it since
all library functions are spliced in before `main`).

## lib finance

Financial mathematics with **explicit, tunable parameters** — every convention
that spreadsheets hide (period timing, compounding frequency, declining-balance
factor) is a named argument you control.

Sign convention: cash IN positive, cash OUT negative. Rate must match the
period (monthly loan → monthly rate = annual/12). Many functions take a `type`
switch: `0.0` = payment at end of period (ordinary annuity), `1.0` = beginning
(annuity due).

**Time value of money:** `fv_lump pv_lump fv_annuity pv_annuity pmt pv_full
nper`, plus helpers `growth per_period`.

**Cash-flow series:** `npv(rate, cf, n)` and `irr(cf, n, lo, hi, iters)`
(bisection — pass a bracket like `-0.5, 1.0` and an iteration count).

**Interest conversions:** `ear(nominal, m)` effective annual rate for `m`
compounding periods; `nominal_from_ear`, `ear_continuous`.

**Growth:** `cagr(begin, end, years)`, `rule_of_72(rate_percent)`.

**Loan amortization:** `interest_payment pv,rate,nper,k`,
`principal_payment`, `balance_after` — the k-th payment's split and the
remaining balance.

**Depreciation:** `dep_straight(cost, salvage, life)` and
`dep_declining(cost, salvage, life, factor, k)` where `factor` tunes the method
(2.0 = double-declining, 1.5 = 150% DB).

**Bonds:** `bond_price(face, coupon_rate, ytm, years, freq)`,
`current_yield`, `ytm_approx`.

**Returns & risk:** `hpr` (holding-period return), `sharpe(mean, rf, stdev)`
(pairs with `lib stats` for the stdev), `real_return(nominal, inflation)`
(Fisher equation).

### Worked example: KPR (mortgage) simulation

`kpr.mik` models a loan with fully tunable pokok/bunga/tenor/frekuensi, prints
the monthly payment and total interest, then walks the amortization schedule
showing how each payment splits between interest and principal over time.

## lib science

Physical constants and unit conversions. Constants are functions (call them):
`c() g_earth() avogadro() R_gas() boltzmann() planck() elementary_charge()
stefan_boltzmann() gravitation() atm_pressure()`.

Conversions: temperature (`c_to_f f_to_c c_to_k k_to_c f_to_k`), pressure
(`psi_to_pa pa_to_psi psi_to_bar bar_to_psi bar_to_pa atm_to_psi`), length
(`ft_to_m m_to_ft inch_to_cm mile_to_km`), volume incl. petroleum
(`bbl_to_m3 m3_to_bbl bbl_to_ft3 gal_to_liter`), mass (`lb_to_kg kg_to_lb`),
energy/power (`hp_to_watt btu_to_joule`).

Ideal gas helpers: `ideal_gas_pressure(n, temp_k, volume)` and
`ideal_gas_moles(pressure, volume, temp_k)`.

## lib numeric

Numerical methods. Routines that need a function evaluate a function named `f`
(or `ode` for ODEs) that YOU define in your program — Mike has no function
pointers yet, so this is the convention.

**Root finding:** `bisection(a, b, iters)`, `newton(x0, iters)` — both use your
`f(x)`.  **Calculus:** `derivative(x, h)`, `derivative2(x, h)`,
`integrate_trap(a, b, n)`, `integrate_simpson(a, b, n)`.  **On sampled data:**
`integrate_data(y, n, dx)`, `integrate_xy(x, y, n)`, `interp_linear(x, y, n,
xq)`.  **ODEs** `dy/dx = ode(x, y)`: `ode_euler(x0, y0, x_end, steps)` and
`ode_rk4(...)` (RK4 is far more accurate).

Example: define `func f(x) return x*x - 2.0 end func`, then `newton(1.0, 10.0)`
returns √2.

## lib linalg

Linear algebra for general n×n systems (flat row-major matrices, same as
`lib matrix`). `solve(a, b, x, n)` solves Ax=b by Gaussian elimination with
partial pivoting; `determinant(src, n)` and `inverse(src, inv, n)` preserve
their input.

The headline routine is `multireg(x, y, rows, cols, beta)` — **multiple linear
regression** via normal equations (X'X)β = X'y. Include a column of 1s in X for
an intercept; the fitted coefficients land in `beta`. This is what enables
multivariate fitting (history-matching-style problems) that single-variable
`lib regression` can't do.

## lib complex

Complex-number arithmetic. Mike has only doubles, so a complex number is a
(real, imag) pair; functions take components as separate args and write the
result into a 2-element `out` array (`out[0]`=real, `out[1]`=imag).

Arithmetic: `c_add c_sub c_mul c_div` (write to out). Scalars: `c_abs`,
`c_abs2` (squared magnitude — quantum probability), `c_arg_approx` (phase, via
a built-in `c_atan` approximation). Also `c_conj c_exp c_polar c_expi`
(`c_expi(theta)` = e^{iθ}, ubiquitous in quantum phases).

## lib quantum

A state-vector quantum computing **simulator**. An n-qubit state is 2·2^n
interleaved doubles (`state[2k]`=real, `state[2k+1]`=imag of amplitude k).
Needs `lib complex`.

Prep: `qinit(state, n)` sets |00…0⟩; `pow2(n)` gives the dimension. Gates fill
an 8-double array then apply: `gate_h gate_x gate_y gate_z gate_s`, applied via
`apply_gate(state, n, q, g)`. Two-qubit `cnot(state, n, control, target)`.
Measurement: `prob(state, k)` = |amplitude|²; `measure(state, n)` samples a
basis state (uses built-in `random()`, so `seed()` first); `total_prob` is a
normalization sanity check.

Worked example (`usequantum.mik`): builds a **Bell state** (H then CNOT) and
measuring it repeatedly only ever yields |00⟩ or |11⟩ — real entanglement.

## lib crypto

Number theory and **educational** cryptography. NOTE: doubles are exact only to
2^53, so these teach the mathematics of crypto with small numbers — they are
not secure and not for production (real crypto needs big-integer arithmetic).

Number theory: `gcd lcm mod modpow modinv`. Primality: `is_prime`,
`miller_test`, `is_prime_mr`, `next_prime`, `totient_pq`.

Diffie-Hellman: `dh_public(g, secret, p)` and `dh_shared(other_public, secret,
p)` — both parties derive the same shared secret without exchanging secrets.

RSA (tiny keys): `rsa_n rsa_choose_e rsa_d rsa_encrypt rsa_decrypt`. The
example runs the textbook p=61, q=53 key (n=3233, e=17, d=2753) and round-trips
a message through encrypt/decrypt.

## lib mechanic

Mechanical engineering (SI units). Rotational: `torque`, `power_rot`,
`rpm_to_rads`, `rads_to_rpm`, `power_from_rpm`. Gears: `gear_ratio`,
`gear_output_speed`, `gear_output_torque`. Stress/strain: `stress`, `strain`,
`youngs_modulus`, `elongation`. Beams: `beam_moment_center`,
`beam_deflection_center`, `inertia_rect`, `inertia_circle`. Shafts:
`polar_inertia`, `shear_stress`. Energy: `spring_force`, `spring_energy`,
`kinetic_energy`, `potential_energy`. Fluids: `reynolds`, `flow_rate`.
Machines: `mech_advantage`, `efficiency`.

## lib electric

Electrical & electronics. Ohm's law: `voltage`, `current`, `resistance`.
Power: `power`, `power_ir`, `power_vr`. Networks: `series2`, `parallel2`,
`parallel_equal`, `cap_series2`, `cap_parallel2`. Capacitors: `cap_energy`,
`charge`. Time constants: `rc_tau`, `rl_tau`, `rc_charge`, `rc_discharge`.
AC/reactance: `omega`, `reactance_c`, `reactance_l`, `resonance_lc`,
`impedance_rlc`. Utility: `voltage_divider`, `led_resistor`, `db_power`.

## lib chem

Chemistry. Moles: `moles`, `mass_from_moles`, `moles_from_particles`,
`particles_from_moles`. Ideal gas: `gas_pressure`, `gas_volume`, `gas_moles`,
`gas_temp`, `combined_v2`. Concentration: `molarity`, `moles_from_molarity`,
`dilution_v1`, `mass_percent`, `ppm`. Acids/bases: `ph`, `h_from_ph`, `poh`,
`oh_from_poh`. Thermo: `heat`, `delta_temp`, `arrhenius`. Misc:
`percent_yield`, `density`.

---

## Membuat library sendiri

Lihat `MEMBUAT_LIBRARY.md` untuk panduan lengkap. Singkatnya: tulis file `.mik`
berisi fungsi (tanpa `main`), simpan sebagai `lib/namamu.mik`, lalu panggil
`lib namamu` di program lain — persis seperti library bawaan.

## lib plot  (v1 — plotting, revisi: terminal + export)

Dua mode. Secara default plot muncul langsung di TERMINAL sebagai ASCII;
export ke file SVG hanya dilakukan jika kamu memanggilnya.

Builtin cetak tanpa newline (di compiler): put_char(code), put_str("teks"),
put_num(x). Builtin file: file_open, file_str, file_num, file_close.

MODE 1 - Terminal (ASCII):
  plot_show(xs, ys, n, xmin, xmax, ymin, ymax)
Langsung menggambar grafik di terminal (kanvas 16x62 karakter, titik '*'),
lengkap dengan label sumbu y dan garis sumbu x. Tidak menulis file apa pun.

MODE 2 - Export SVG (hanya jika diminta):
  file_open("chart.svg")
  svg_head()
  svg_axes(xmin, xmax, ymin, ymax)
  svg_title_open()  file_str("Judul")  svg_title_close()
  svg_line(xs, ys, n, xmin, xmax, ymin, ymax)   # garis biru + titik merah
  svg_foot()
  file_close()

Catatan desain: fungsi Mike maksimal 8 parameter (xmm0..xmm7), jadi ukuran
kanvas ASCII dibuat konstan di library (plot_rows/plot_cols) agar plot_show
cukup 7 argumen. Render SVG ke PNG di luar Mike: buka di browser, atau
`convert chart.svg chart.png`.
