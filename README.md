# CSE 3150 Course Project - BGP Simulator

This project is a BGP simulator written in C++. It reads CAIDA relationship
data, processes announcements, applies ROV filtering, and writes the selected
routes to `ribs.csv` in the current working directory.

## Build

```bash
make
```

This produces:

```bash
./bgp_simulator
```

## Run

The program takes the three required input files:

```bash
./bgp_simulator --relationships ../bench/many/CAIDAASGraphCollector_2025.10.16.txt --announcements ../bench/many/anns.csv --rov-asns ../bench/many/rov_asns.csv
```

It writes:

```text
ribs.csv
```

The output format is:

```text
asn,prefix,as_path
```

Example rows:

```text
1,1.2.0.0/16,"(1, 11537, 10886, 27)"
27,1.2.0.0/16,"(27,)"
```

## Notes

- Relationship files use the CAIDA serial-2 format.
- Announcement files use `seed_asn,prefix,rov_invalid`.
- ROV files contain one ASN per line.
- Provider/customer cycles are checked when the graph is built.
- Route choice follows the required relationship, path length, and next-hop
  tie breakers from the project handout.

## Testing

Run the local tests:

```bash
make test
```

Compare against a provided benchmark:

```bash
./bgp_simulator --relationships bench/prefix/CAIDAASGraphCollector_2025.10.16.txt --announcements bench/prefix/anns.csv --rov-asns bench/prefix/rov_asns.csv
bash bench/compare_output.sh bench/prefix/ribs.csv ribs.csv
```

The program is also set up so it can be run from another directory and still
write `ribs.csv` in that directory, which matches the professor's clarification.

## Website

There is also a browser version in `web/` that runs the simulator in the
browser and lets you upload announcements and enter a target ASN.

Local development from `web/`:

```bash
npm install
npm run dev
```

Rebuild the browser bundle:

```bash
make wasm
```

Run the browser smoke test:

```bash
make wasm-test
```

Live site: https://neilpatelcse.space/

Vercel URL: https://cse-3150-project.vercel.app
