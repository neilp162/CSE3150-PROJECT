import "./globals.css";

export const metadata = {
  title: "BGP Simulator",
  description: "Upload announcements and inspect routes at a chosen ASN.",
};

export default function RootLayout({ children }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
