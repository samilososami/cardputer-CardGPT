import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';
export default defineConfig({
  build: { rollupOptions: { input: {
    home: fileURLToPath(new URL('./index.html', import.meta.url)),
    cardputer: fileURLToPath(new URL('./cardputer/index.html', import.meta.url)),
  } } },
});
