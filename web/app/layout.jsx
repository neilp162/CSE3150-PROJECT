import "./globals.css";

export const metadata = {
  title: "BGP WASM Simulator",
  description: "Client-side BGP simulator running in WebAssembly.",
};

export default function RootLayout({ children }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
