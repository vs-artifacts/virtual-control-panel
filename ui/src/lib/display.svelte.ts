import { fetchDisplay, onDisplay } from './bridge'

// The page is always laid out at the design size and zoomed to fill the window. The zoom also covers the web view
// rendering at the monitor's own scale, which can differ from the scale the host gives the plugin's window
// (FL Studio may keep plugins at 100% on a 200% monitor).
let scale = 1

// The editor's size in pixels, and its size relative to the design size (for the Settings tab)
export const view = $state({ width: 560, height: 600, uiScale: 1 })

function readPositive(value: unknown, limit: number): number | null {
    return typeof value === 'number' && Number.isFinite(value) && value > 0 && value < limit ? value : null
}

export function currentZoom(): number {
    return scale / window.devicePixelRatio
}

// The visible area in page pixels (before the page's zoom)
export function viewportSize(): { width: number; height: number } {
    const zoom = currentZoom()
    return { width: window.innerWidth / zoom, height: window.innerHeight / zoom }
}

function applyZoom(): void {
    document.documentElement.style.zoom = String(currentZoom())
}

function update(display: unknown): void {
    if (typeof display !== 'object' || display === null) return

    const fields = display as Record<string, unknown>
    const newScale = readPositive(fields.scale, 100)

    if (newScale === null) return

    scale = newScale
    applyZoom()

    view.uiScale = readPositive(fields.uiScale, 100) ?? view.uiScale
    view.width = readPositive(fields.width, 100000) ?? view.width
    view.height = readPositive(fields.height, 100000) ?? view.height
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
