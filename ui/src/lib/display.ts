import { fetchDisplay, onDisplay } from './bridge'

// The web view renders at the monitor's own scale, which can differ from the scale the host gives the plugin's
// window (FL Studio may keep plugins at 100% on a 200% monitor). Zooming the page by the ratio makes it match.
let scale = 1

function readScale(display: unknown): number | null {
    if (typeof display !== 'object' || display === null) return null

    const value = (display as Record<string, unknown>).scale
    return typeof value === 'number' && Number.isFinite(value) && value > 0 && value < 100 ? value : null
}

export function currentZoom(): number {
    return scale / window.devicePixelRatio
}

function applyZoom(): void {
    document.documentElement.style.zoom = String(currentZoom())
}

function update(display: unknown): void {
    const newScale = readScale(display)

    if (newScale === null) return

    scale = newScale
    applyZoom()
}

// Moving to a monitor with another scale changes the web view's own pixel ratio
function watchPixelRatio(): void {
    matchMedia(`(resolution: ${window.devicePixelRatio}dppx)`).addEventListener(
        'change',
        () => {
            applyZoom()
            watchPixelRatio()
        },
        { once: true },
    )
}

export async function followDisplayScale(): Promise<void> {
    onDisplay(update)
    watchPixelRatio()

    try {
        update(await fetchDisplay())
    } catch {
        // Keeps the web view's own scale
    }
}
