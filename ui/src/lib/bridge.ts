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

// { scale }: physical pixels per pixel of the plugin's own UI, which the page matches
export function fetchDisplay(timeoutMs = 2000): Promise<unknown> {
    return callNative('display', timeoutMs)
}

export function onDisplay(listener: (display: unknown) => void): void {
    window.__JUCE__.backend.addEventListener('display', listener)
}

// { native, items }: the host's options for a parameter; native means the host showed its own menu instead
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

export function sendPointerDown(textEntry: boolean): void {
    window.__JUCE__.backend.emitEvent('pointerDown', { textEntry })
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
