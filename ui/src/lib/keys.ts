import '@juce-framework/webview'

// Keys the host should get (e.g. FL Studio's typing keyboard) go to C++, which decides what actually passes;
// this list only stops those keys also acting on the page
function readForwardedKeys(): string[] {
    const data = window.__JUCE__.initialisationData as unknown as Record<string, unknown>
    const keys = data['forwardedKeys']
    return Array.isArray(keys) ? keys.filter((key): key is string => typeof key === 'string') : []
}

function isTextEntry(target: EventTarget | null): boolean {
    return target instanceof HTMLElement && (target.isContentEditable || target.matches('input, textarea, select'))
}

export function forwardHostKeys(): void {
    const chords = new Set(readForwardedKeys())

    if (chords.size === 0) return

    const backend = window.__JUCE__.backend
    const held = new Set<string>()

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
            if (!held.delete(event.code)) return

            event.preventDefault()
            backend.emitEvent('key', { code: event.code, down: false })
        },
        { capture: true },
    )

    // A release the page never sees would leave a stuck note in the host
    window.addEventListener('blur', releaseAll)
    document.addEventListener('visibilitychange', () => {
        if (document.hidden) releaseAll()
    })
}
