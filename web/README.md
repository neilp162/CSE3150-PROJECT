# BGP Website

This folder contains the browser version of the simulator. The site runs the
compiled C++ code in the browser and lets a user upload announcements and enter
a target ASN. Default relationship and ROV data are included, and both can be
overridden if needed.

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

## Rebuild Browser Files

The generated `public/simulator.js` and `public/simulator.wasm` files are
committed so Vercel can deploy without Emscripten installed. To rebuild them:

```bash
bash web/build_wasm.sh
```

The script expects `emcc`. If `emcc` is not on `PATH`, it will also try to
source `/tmp/emsdk/emsdk_env.sh`.

## Vercel

Use Vercel's GitHub import flow and set the root directory to `web`. The default
Next.js build settings work as-is.
