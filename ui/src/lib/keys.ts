import '@juce-framework/webview'
import { sendPointerDown } from './bridge'

// Keys the host should get (e.g. FL Studio's typing keyboard) go to C++, which decides what actually passes;
// this list only stops those keys also acting on the page
function readForwardedKeys(): string[] {
    const data = window.__JUCE__.initialisationData as unknown as Record<string, unknown>
    const keys = data['forwardedKeys']
    return Array.isArray(keys) ? keys.filter((key): key is string => typeof key === 'string') : []
}

// Inputs that take typing; sliders, checkboxes and buttons don't, so keys still pass while they have focus
const nonTextInputTypes = new Set(['range', 'checkbox', 'radio', 'button', 'submit', 'reset', 'color', 'image', 'file'])

export function isTextEntry(target: EventTarget | null): boolean {
    if (!(target instanceof HTMLElement)) return false
    if (target instanceof HTMLInputElement) return !nonTextInputTypes.has(target.type)

    return target.isContentEditable || target.matches('textarea, select')
}

// Lets the plugin hand keyboard focus back to the host after clicks outside text fields (an experimental setting)
export function reportPointerDowns(): void {
    window.addEventListener('pointerdown', (event) => sendPointerDown(isTextEntry(event.target)), { capture: true })
}

export function forwardHostKeys(): void {
    const chords = new Set(readForwardedKeys())

    if (chords.size === 0) return

    const backend = window.__JUCE__.backend
    const held = new Set<string>()
    const isForwardedCode = (code: string) => chords.has(code) || chords.has('Ctrl+' + code)

    const releaseAll = () => {
        if (held.size === 0) return
        held.clear()
        backend.emitEvent('releaseKeys', {})
    }

    window.addEventListener(
        'keydown',
        (event) => {
            if (isTextEntry(event.target) || event.altKey || event.shiftKey || event.metaKey) return
            if (!chords.has((event.ctrlKey ? 'Ctrl+' : '') + event.code)) return

            event.preventDefault()
            held.add(event.code)
            backend.emitEvent('key', { code: event.code, down: true, repeat: event.repeat })
        },
        { capture: true },
    )

    window.addEventListener(
        'keyup',
        (event) => {
            if (held.delete(event.code)) {
                event.preventDefault()
                backend.emitEvent('key', { code: event.code, down: false })
            } else if (isForwardedCode(event.code)) {
                // Held since before the page had focus: the host saw it go down, so it must see it come up
                backend.emitEvent('key', { code: event.code, down: false })
            }
        },
        { capture: true },
    )

    // A release the page never sees would leave a stuck note in the host
    window.addEventListener('blur', releaseAll)
    document.addEventListener('visibilitychange', () => {
        if (document.hidden) releaseAll()
    })
}
