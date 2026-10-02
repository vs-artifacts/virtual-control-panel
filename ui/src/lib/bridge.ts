import { getNativeFunction } from '@juce-framework/webview'

const helloFunction = 'hello'
const catalogFunction = 'catalog'

// Only our plugin's web view registers native functions; a normal browser tab has none
export function isInsidePlugin(): boolean {
    return window.__JUCE__.initialisationData.__juce__functions.includes(helloFunction)
}

function callNative(name: string, timeoutMs: number, ...args: unknown[]): Promise<unknown> {
    return Promise.race([
        getNativeFunction(name)(...args),
        new Promise((_, reject) => setTimeout(() => reject(new Error('No reply')), timeoutMs)),
    ])
}

export async function hello(timeoutMs = 2000): Promise<string> {
    const reply = await callNative(helloFunction, timeoutMs)

    if (typeof reply !== 'string') throw new Error('Unexpected reply')

    return reply
}

// Starts a control session: every exposed parameter, with its handle for this session
export function fetchCatalog(timeoutMs = 2000): Promise<unknown> {
    return callNative(catalogFunction, timeoutMs)
}

// { scale, uiScale, width, height }: physical pixels per page pixel, and the editor's size
export function fetchDisplay(timeoutMs = 2000): Promise<unknown> {
    return callNative('display', timeoutMs)
}

// { title, tab, settings, plugins }: what the Plugins and Settings tabs show, and which tab was showing
export function fetchEditorState(timeoutMs = 2000): Promise<unknown> {
    return callNative('editorState', timeoutMs)
}

export function sendTab(index: number): void {
    window.__JUCE__.backend.emitEvent('tab', { index })
}

export function sendSetting(name: string, value: string | boolean | number): void {
    window.__JUCE__.backend.emitEvent('setting', { name, value })
}

export function sendResetSize(): void {
    window.__JUCE__.backend.emitEvent('resetSize', {})
}

// Opens a hosted plugin's own editor in a separate window
export function sendOpenPlugin(slot: number): void {
    window.__JUCE__.backend.emitEvent('openPlugin', { slot })
}

export function onDisplay(listener: (display: unknown) => void): void {
    window.__JUCE__.backend.addEventListener('display', listener)
}

// { running }: whether the host is running the plugin's audio (FL's smart disable stops it)
export function onProcessing(listener: (message: unknown) => void): void {
    window.__JUCE__.backend.addEventListener('processing', listener)
}

// The bay link's status (see LinkStatus), about 4 times a second while the editor shows
export function onLink(listener: (status: unknown) => void): void {
    window.__JUCE__.backend.addEventListener('link', listener)
}

// What the panel plays: its own sound (delayed by the round trip) or the bay's return
export function sendBayOutput(value: 'direct' | 'return'): void {
    window.__JUCE__.backend.emitEvent('bayOutput', { value })
}

// Compares the next stretch of returned audio with what was sent; the result comes with the link status
export function sendMeasureLag(): void {
    window.__JUCE__.backend.emitEvent('measureLag', {})
}

export function sendResetLinkCounters(): void {
    window.__JUCE__.backend.emitEvent('resetLinkCounters', {})
}

// { items }: our menu for a parameter, with the host's options in a submenu
export function fetchParameterMenu(h: number, timeoutMs = 10000): Promise<unknown> {
    return callNative('parameterMenu', timeoutMs, h)
}

export function sendMenuChoice(id: number): void {
    window.__JUCE__.backend.emitEvent('menuChoice', { id })
}

export function sendMenuClosed(): void {
    window.__JUCE__.backend.emitEvent('menuClosed', {})
}

export type Corner = 'topLeft' | 'topRight' | 'bottomLeft' | 'bottomRight'

// The plugin follows the pointer itself; these only say when a corner drag starts, moves and ends
export function sendResize(corner: Corner, phase: 'start' | 'move' | 'end'): void {
    window.__JUCE__.backend.emitEvent('resize', { corner, phase })
}

export interface ValueChange {
    h: number
    v: number
}

export function sendSet(batch: ValueChange[]): void {
    window.__JUCE__.backend.emitEvent('set', batch)
}

export function sendGesture(h: number, active: boolean): void {
    window.__JUCE__.backend.emitEvent('gesture', { h, active })
}

export function onParams(listener: (batch: unknown) => void): void {
    window.__JUCE__.backend.addEventListener('params', listener)
}
