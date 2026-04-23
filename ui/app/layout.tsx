import type { Metadata } from 'next';
import './globals.css';

export const metadata: Metadata = {
  title: 'AegisCore CCI',
  description: 'Command & Control Interface',
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
