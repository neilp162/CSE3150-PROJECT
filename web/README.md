# BGP WASM Website

This folder is the Website/WASM extra credit frontend. It is a static-exported
Next.js app intended for Vercel hosting. The page runs the WebAssembly-compiled
C++ simulator entirely in the browser and lets a user upload announcements plus
a target ASN. By default, it also ships with a built-in CAIDA graph snapshot
and a built-in ROV ASN file, while still allowing optional overrides.

## Local Development

From this `web/` directory:

```bash
npm install
npm run dev
```

Then open:

```text
http://localhost:3000
```

## Production Build

```bash
npm install
npm run build
```

This produces a static export in `web/out/`.

## Rebuild WASM

The generated `public/simulator.js` and `public/simulator.wasm` files are
committed so Vercel can build without an Emscripten toolchain. To rebuild them:

```bash
bash web/build_wasm.sh
```

The script expects `emcc`. If `emcc` is not on `PATH`, it will also try to
source `/tmp/emsdk/emsdk_env.sh`.

## Vercel

Use Vercel's GitHub import flow and set the repo root directory to `web`.
Vercel will auto-detect Next.js and run the standard `npm install` + `npm run build`
pipeline on the Hobby plan.
