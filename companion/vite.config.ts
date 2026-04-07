import { defineConfig } from 'vite';
import { resolve } from 'path';

export default defineConfig({
  root: '.',
  base: '/video/',
  build: {
    outDir: '../docs/video',
    emptyOutDir: true,
    rollupOptions: {
      input: {
        main: resolve(__dirname, 'index.html'),
        popout: resolve(__dirname, 'popout.html'),
      },
    },
  },
  server: {
    port: 5173,
  },
});
