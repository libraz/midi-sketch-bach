import path from 'node:path';
import { defineConfig } from 'vitest/config';

export default defineConfig({
  resolve: {
    alias: {
      '../bach.js': path.resolve(__dirname, 'dist/bach.js'),
    },
  },
  test: {
    globals: true,
    environment: 'node',
    include: ['tests/wasm/**/*.test.ts'],
    coverage: {
      provider: 'v8',
      reporter: ['text', 'json', 'html'],
      include: ['js/src/**/*.ts'],
      exclude: ['js/**/*.test.ts', 'js/**/*.d.ts'],
    },
  },
});
