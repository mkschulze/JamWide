import { defineConfig } from 'vite';

export default defineConfig({
  root: '.',
  base: '/video/',
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
  server: {
    port: 5173,
  },
});
