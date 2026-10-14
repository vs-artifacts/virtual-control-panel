import { fetchDisplay, onDisplay } from './bridge'

// The page is always laid out at the design size and scaled to fill the window. The factor also covers the web view
// rendering at the monitor's own scale, which can differ from the scale the host gives the plugin's window
// (FL Studio may keep plugins at 100% on a 200% monitor).
let scale = 1

// smooth: laid out once and scaled like a picture (a transform), so everything keeps its proportions.
// sharp: laid out again at every size (zoom), so edges stay crisp but elements can shift by a pixel against each other.
export type Rendering = 'smooth' | 'sharp'
let rendering: Rendering = 'smooth'

// The editor's size in pixels, its size relative to the design size (for the Settings tab), and screen pixels per page pixel
export const view = $state({ width: 790, height: 1270, uiScale: 1, devicePixels: 1 })

// In smooth rendering the page is drawn once into an image (a layer), which the browser then stretches, so text moves
// in proportion with everything else; drawn directly at each size, text snaps to whole pixels. The image keeps the
// resolution it was drawn at, so it's drawn again at the new size once resizing pauses.
const redrawAfterMs = 250
let redrawTimer: ReturnType<typeof setTimeout> | undefined

function scheduleRedraw(root: HTMLElement): void {
    clearTimeout(redrawTimer)
    redrawTimer = setTimeout(() => {
        if (rendering !== 'smooth') return

        // One frame without the layer drops its image; the next draws a new one at the current size
        root.style.willChange = 'auto'
        requestAnimationFrame(() =>
            requestAnimationFrame(() => {
                if (rendering === 'smooth') root.style.willChange = 'transform'
            }),
        )
    }, redrawAfterMs)
}

function readPositive(value: unknown, limit: number): number | null {
    return typeof value === 'number' && Number.isFinite(value) && value > 0 && value < limit ? value : null
}

// Page pixels to the web view's own pixels, in either rendering
export function currentZoom(): number {
    return scale / window.devicePixelRatio
}

// The visible area in page pixels (before the page's scaling)
export function viewportSize(): { width: number; height: number } {
    const zoom = currentZoom()
    return { width: window.innerWidth / zoom, height: window.innerHeight / zoom }
}

function applyZoom(): void {
    const factor = currentZoom()
    const root = document.getElementById('app')

    if (rendering === 'sharp' || root === null) {
        document.documentElement.style.zoom = String(factor)

        if (root !== null) for (const property of ['position', 'left', 'top', 'width', 'height', 'transform', 'transform-origin', 'will-change']) root.style.removeProperty(property)

        return
    }

    // Fixed-position content (tabs, grips, menus) is placed within the scaled root, so it all scales together
    document.documentElement.style.zoom = ''
    const { width, height } = viewportSize()
    root.style.position = 'fixed'
    root.style.left = '0'
    root.style.top = '0'
    root.style.width = `${width}px`
    root.style.height = `${height}px`
    root.style.transformOrigin = '0 0'
    root.style.transform = `scale(${factor})`
    root.style.willChange = 'transform'
    scheduleRedraw(root)
}

export function setRendering(next: Rendering): void {
    if (next === rendering) return

    rendering = next
    applyZoom()
}

function update(display: unknown): void {
    if (typeof display !== 'object' || display === null) return

    const fields = display as Record<string, unknown>
    const newScale = readPositive(fields.scale, 100)

    if (newScale === null) return

    scale = newScale
    applyZoom()

    view.devicePixels = scale
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

    // The scaled root is sized to the window in smooth rendering
    window.addEventListener('resize', applyZoom)

    try {
        update(await fetchDisplay())
    } catch {
        // Keeps the web view's own scale
    }
}
