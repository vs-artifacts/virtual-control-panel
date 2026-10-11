import { svelte } from '@sveltejs/vite-plugin-svelte'
import { defineConfig, searchForWorkspaceRoot, type Plugin } from 'vite'

// The built page is embedded in the plugin: it may only load its own files, with no inline code and no network
const contentSecurityPolicy = [
    "default-src 'none'",
    "script-src 'self'",
    "style-src 'self'",
    "font-src 'self'",
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
    // The dev server may also serve the settings' defaults and the slot font, which live with the plugin's source (nothing
    // else outside ui/)
    server: {
        fs: { allow: [searchForWorkspaceRoot(process.cwd()), '../source/SettingsDefaults.json', '../assets/fonts/m5x7.ttf'] },
    },
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
