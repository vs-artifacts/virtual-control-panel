// Browser behaviour that doesn't belong in a plugin panel: its right-click menu, print/find/reload shortcuts,
// and browser zoom (which would also fight the page's own scaling)
const blockedWithCtrl = new Set(['KeyP', 'KeyF', 'KeyG', 'KeyR', 'KeyS', 'KeyO', 'KeyU', 'KeyJ', 'KeyH', 'Equal', 'Minus', 'Digit0', 'NumpadAdd', 'NumpadSubtract', 'Numpad0'])
const blockedAlone = new Set(['F3', 'F5', 'F7', 'BrowserRefresh', 'BrowserBack', 'BrowserForward', 'BrowserSearch'])

export function blockBrowserBehaviour(): void {
    window.addEventListener('contextmenu', (event) => event.preventDefault(), { capture: true })

    window.addEventListener(
        'wheel',
        (event) => {
            if (event.ctrlKey) event.preventDefault()
        },
        { capture: true, passive: false },
    )

    window.addEventListener(
        'keydown',
        (event) => {
            const withCtrl = (event.ctrlKey || event.metaKey) && blockedWithCtrl.has(event.code)
            const historyKey = event.altKey && (event.code === 'ArrowLeft' || event.code === 'ArrowRight')

            if (withCtrl || historyKey || blockedAlone.has(event.code)) event.preventDefault()
        },
        { capture: true },
    )
}
