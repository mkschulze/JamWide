import { defineConfig } from 'vite';

export default defineConfig({
  root: '.',
  base: '/video/',
  build: {
    outDir: '../docs/video',
    emptyOutDir: true,
  },
  server: {
    port: 5173,
  },
});
