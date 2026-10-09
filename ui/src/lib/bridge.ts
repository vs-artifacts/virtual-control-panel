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

// { title, tab, diagnostics, scroll, settings, plugins }: what the tabs show, and how the editor was left
export function fetchEditorState(timeoutMs = 2000): Promise<unknown> {
    return callNative('editorState', timeoutMs)
}

export function sendTab(index: number): void {
    window.__JUCE__.backend.emitEvent('tab', { index })
}

export function sendSetting(name: string, value: string | boolean | number): void {
    window.__JUCE__.backend.emitEvent('setting', { name, value })
}

// Sets the value and makes it the setting's own default
export function sendSettingDefault(name: string, value: string | boolean | number): void {
    window.__JUCE__.backend.emitEvent('settingDefault', { name, value })
}

// Back to the default (the user's own or the factory's)
export function sendResetSettings(which: { name: string } | { all: true }): void {
    window.__JUCE__.backend.emitEvent('resetSettings', which)
}

// Names probe (test build): Param 1's name with or without the plugin's name
export function sendNamesProbe(): void {
    window.__JUCE__.backend.emitEvent('namesProbe', {})
}

export function sendResetSize(): void {
    window.__JUCE__.backend.emitEvent('resetSize', {})
}

// Opens a hosted plugin's own editor in a separate window
export function sendOpenPlugin(slot: number): void {
    window.__JUCE__.backend.emitEvent('openPlugin', { slot })
}

// Silences a generator (it keeps running)
export function sendMutePlugin(slot: number, muted: boolean): void {
    window.__JUCE__.backend.emitEvent('mutePlugin', { slot, muted })
}

export function sendDiagnostics(open: boolean): void {
    window.__JUCE__.backend.emitEvent('diagnostics', { open })
}

// Where a scrolling area was left, so the editor reopens there
export function sendScroll(area: string, top: number): void {
    window.__JUCE__.backend.emitEvent('scroll', { area, top })
}

export function onDisplay(listener: (display: unknown) => void): void {
    window.__JUCE__.backend.addEventListener('display', listener)
}

// { running }: whether the host is running the plugin's audio (FL's smart disable stops it)
export function onProcessing(listener: (message: unknown) => void): void {
    window.__JUCE__.backend.addEventListener('processing', listener)
}

// { keys, windows }: the last forwarded keys and the windows around the editor (test-build diagnostics)
export function onHostDiagnostics(listener: (message: unknown) => void): void {
    window.__JUCE__.backend.addEventListener('hostDiagnostics', listener)
}

// The bay link's status (see LinkStatus), about 4 times a second while the editor shows
export function onLink(listener: (status: unknown) => void): void {
    window.__JUCE__.backend.addEventListener('link', listener)
}

// The bay's slot state: active (the bay's sound), mute or bypass (the sent audio, delayed like the return; bypass sends
// the bay silence), or off (the sent audio at once: the bay's delay leaves the latency the host is told)
export function sendBayBypass(value: 'active' | 'mute' | 'bypass' | 'off'): void {
    window.__JUCE__.backend.emitEvent('bayBypass', { value })
}

// Compares the next stretch of returned audio with what was sent; the result comes with the link status
export function sendMeasureLag(): void {
    window.__JUCE__.backend.emitEvent('measureLag', {})
}

// Steps control 1 and times the change in the return; the result comes with the link status
export function sendStartProbe(): void {
    window.__JUCE__.backend.emitEvent('startProbe', {})
}

export function sendResetLinkCounters(): void {
    window.__JUCE__.backend.emitEvent('resetLinkCounters', {})
}

// Real blocks: watch the host's blocks afresh (the round trip may shrink)
export function sendRecheckBlockSize(): void {
    window.__JUCE__.backend.emitEvent('recheckBlockSize', {})
}

// Automatic round trip: try 1 block again
export function sendRetryOneBlock(): void {
    window.__JUCE__.backend.emitEvent('retryOneBlock', {})
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
