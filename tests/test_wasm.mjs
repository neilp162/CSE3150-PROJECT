import createModule from "../web/public/simulator.js";

const module = await createModule({
  locateFile: (path) => new URL(`../web/public/${path}`, import.meta.url).href
});

const simulate = module.cwrap("simulate_target", "number", [
  "string",
  "string",
  "string",
  "number"
]);
const freeResult = module.cwrap("free_result", null, ["number"]);

const relationships = [
  "1|2|-1|demo",
  "1|3|0|demo",
  "3|4|-1|demo",
  "5|1|0|demo"
].join("\n");
const announcements = [
  "seed_asn,prefix,rov_invalid",
  "2,203.0.113.0/24,False",
  "4,198.51.100.0/24,False",
  "2,192.0.2.0/24,True"
].join("\n");
const rovAsns = "3\n";

const pointer = simulate(relationships, announcements, rovAsns, 4);
const csv = module.UTF8ToString(pointer);
freeResult(pointer);

if (csv.startsWith("ERROR:")) {
  throw new Error(csv);
}

const expectedLines = [
  '4,203.0.113.0/24,"(4, 3, 1, 2)",3,provider,false',
  '4,198.51.100.0/24,"(4,)",4,origin,false'
];

for (const line of expectedLines) {
  if (!csv.includes(line)) {
    console.error(csv);
    throw new Error(`Missing expected WASM output line: ${line}`);
  }
}

if (csv.includes("192.0.2.0/24")) {
  console.error(csv);
  throw new Error("ROV-invalid route should not reach target AS4 through ROV AS3");
}

console.log("WASM browser module test passed.");
