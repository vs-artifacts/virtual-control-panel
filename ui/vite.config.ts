import { svelte } from '@sveltejs/vite-plugin-svelte'
import { defineConfig, type Plugin } from 'vite'

// The built page is embedded in the plugin: it may only load its own files, with no inline code and no network
const contentSecurityPolicy = [
    "default-src 'none'",
    "script-src 'self'",
    "style-src 'self'",
    "base-uri 'none'",
    "form-action 'none'",
].join('; ')

// Build only, so the dev server's hot reload keeps working
function contentSecurityPolicyInBuild(): Plugin {
    return {
        name: 'content-security-policy-in-build',
        apply: 'build',
        transformIndexHtml: () => [
            {
                tag: 'meta',
                attrs: { 'http-equiv': 'Content-Security-Policy', content: contentSecurityPolicy },
                injectTo: 'head-prepend',
            },
        ],
    }
}

export default defineConfig({
    plugins: [svelte(), contentSecurityPolicyInBuild()],
    base: './',
    build: {
        modulePreload: { polyfill: false },
        assetsInlineLimit: 0,
        rolldownOptions: {
            // Fixed names: the plugin embeds exactly these files (see cmake-local/Ui.cmake)
            output: {
                entryFileNames: 'assets/index.js',
                assetFileNames: 'assets/[name][extname]',
            },
        },
    },
})
