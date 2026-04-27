# CSE 3150 Course Project: BGP Simulator

This project builds a C++ BGP simulator that reads CAIDA AS relationship data,
seeds BGP announcements, applies ROV filtering, propagates routes using the
assignment's valley-free model, and writes the resulting local RIBs to
`ribs.csv` in the current directory.

## Build

```bash
make
```

This creates the required executable:

```bash
./bgp_simulator
```

## Required Run Format

The program accepts the three required inputs in any order:

```bash
./bgp_simulator --relationships ../bench/many/CAIDAASGraphCollector_2025.10.16.txt --announcements ../bench/many/anns.csv --rov-asns ../bench/many/rov_asns.csv
```

It writes:

```text
ribs.csv
```

in the directory where the command is run. The output columns are:

```text
asn,prefix,as_path
```

Paths are formatted to match the provided bench output, for example:

```text
1,1.2.0.0/16,"(1, 11537, 10886, 27)"
27,1.2.0.0/16,"(27,)"
```

## Input Files

Relationship files use CAIDA serial-2 format:

```text
asn1|asn2|relationship|source
```

The simulator uses:

- `relationship == -1`: `asn1` is a provider of `asn2`.
- `relationship == 0`: `asn1` and `asn2` are peers.
- Comment lines beginning with `#` are ignored.

Announcement files are CSV files with:

```text
seed_asn,prefix,rov_invalid
```

ROV files contain one ASN per line. A one-column CSV header is also tolerated.

## Design Decisions

The implementation follows the project handout closely instead of chasing the
top speed leaderboard.

- Each AS stores providers, customers, peers, whether it deploys ROV, and its
  propagation rank.
- Provider/customer cycle detection is done while building propagation ranks.
  The graph is ranked from customer leaves upward to providers. If some ASes
  cannot be ranked, the program prints an error and exits nonzero.
- Each prefix is simulated independently. This keeps memory usage low even when
  the final `ribs.csv` is large.
- Propagation has the three assignment phases:
  1. Up to providers, rank by rank.
  2. Across peers for exactly one hop.
  3. Down to customers, rank by rank in reverse.
- BGP route selection uses the required tie breakers:
  1. Best relationship: origin, then customer, then peer, then provider.
  2. Shortest AS path.
  3. Lowest next-hop ASN.
- ROV ASes drop received announcements marked `rov_invalid=True`. Origin
  announcements are seeded at their owner AS before propagation.
- AS-path loop checks are included so an AS never installs a route whose path
  already contains its own ASN.

## Tests

Run the small local regression tests:

```bash
make test
```

Run a provided bench comparison from the project root:

```bash
make
./bgp_simulator --relationships bench/prefix/CAIDAASGraphCollector_2025.10.16.txt --announcements bench/prefix/anns.csv --rov-asns bench/prefix/rov_asns.csv
bash bench/compare_output.sh bench/prefix/ribs.csv ribs.csv
```

The professor clarified that the required executable should also work from a
different current directory and still write `ribs.csv` there. This works because
the output path is intentionally just `ribs.csv`.

The large course-provided `bench/` inputs and expected outputs are kept local
for testing and submission prep, but they are not committed to the published
GitHub repository because they are bulky and already distributed separately.

## Website / WASM Extra Credit

The `web/` folder contains a static-exported Next.js frontend for Vercel. It
uses Emscripten to compile `web/src/simulator_wasm.cpp` into browser assets in
`web/public/`, and the page accepts local relationship, announcement, and ROV
files plus a target ASN. It also ships with a built-in CAIDA graph snapshot and
built-in ROV list so a user can simply upload announcements and a target ASN,
which matches the assignment wording more closely.

Build or rebuild the WASM bundle:

```bash
make wasm
```

Run the WASM smoke test:

```bash
make wasm-test
```

For local frontend development from the `web/` directory:

```bash
npm install
npm run dev
```

Then open:

```text
http://localhost:3000
```

For Vercel deployment:

- import the GitHub repository into Vercel,
- set the Root Directory to `web`,
- let Vercel auto-detect Next.js,
- keep the default install/build behavior,
- add a custom domain after the first successful deployment.

The generated `web/public/simulator.js` and `web/public/simulator.wasm` files
are committed, so Vercel does not need an Emscripten toolchain during deploys.

Live deployment URL: https://neilpatelcse.space/

Vercel preview/backup URL: https://cse-3150-project.vercel.app
