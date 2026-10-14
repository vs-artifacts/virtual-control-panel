import { view } from './display.svelte'

// Pixel text (p2, p3) is drawn at a whole number of screen pixels per font pixel, or anti-aliased in steps of a fraction
// of one: the next size from atPercent of the way past each step. Never below one step.
export function pixelsPerFontPixel(devicePixels: number, step: number, atPercent: number): number {
    return step * Math.max(1, Math.floor(devicePixels / step + 1 - atPercent / 100 + 1e-6))
}

// The stepped font sizes in page pixels (app.css falls back to them unless Barlow replaces the pixel fonts)
export function applyPixelSizes(antialias: boolean, atPercent: number, antialiasedStepPercent: number): void {
    const devicePixels = view.devicePixels
    const fontPixel = pixelsPerFontPixel(devicePixels, antialias ? antialiasedStepPercent / 100 : 1, atPercent)
    const style = document.documentElement.style

    style.setProperty('--p2-stepped', `${(16 * fontPixel) / devicePixels}px`)
    style.setProperty('--p3-stepped', `${(8 * fontPixel) / devicePixels}px`)
    scheduleSnap()
}

// Each pixel-text element (.crisp) is moved by a fraction of a pixel so its text starts on a whole screen pixel
let snapping = true
let scheduled = false
const offsets = new WeakMap<HTMLElement, { x: number; y: number }>()

export function setSnapping(on: boolean): void {
    snapping = on
    scheduleSnap()
}

export function scheduleSnap(): void {
    if (scheduled) return

    scheduled = true
    requestAnimationFrame(snap)
}

function snap(): void {
    scheduled = false

    const elements = Array.from(document.querySelectorAll<HTMLElement>('.crisp'))
    const root = document.getElementById('app')

    if (!snapping || root === null) {
        for (const element of elements) {
            element.style.removeProperty('translate')
            offsets.delete(element)
        }

        return
    }

    // Client pixels to screen pixels, whichever way the page is scaled (zoom or a transform)
    const toScreen = (window.devicePixelRatio * window.innerWidth) / root.getBoundingClientRect().width
    const devicePixels = view.devicePixels
    const range = document.createRange()

    // All reads first, then all writes, so the layout is worked out once
    const moves = elements.flatMap((element) => {
        range.selectNodeContents(element)
        const box = range.getBoundingClientRect()

        if (box.width === 0 && box.height === 0) return []

        const applied = offsets.get(element) ?? { x: 0, y: 0 }
        const left = box.left * toScreen - applied.x
        const top = box.top * toScreen - applied.y

        return [{ element, x: Math.round(left) - left, y: Math.round(top) - top }]
    })

    for (const { element, x, y } of moves) {
        offsets.set(element, { x, y })
        element.style.translate = `${x / devicePixels}px ${y / devicePixels}px`
    }
}

// Once, for the whole page: snap again whenever text, layout or scrolling may have moved it
export function followPixelText(): void {
    new MutationObserver(scheduleSnap).observe(document.body, { childList: true, subtree: true, characterData: true })
    document.addEventListener('scroll', scheduleSnap, { capture: true, passive: true })
    window.addEventListener('resize', scheduleSnap)
    void document.fonts.ready.then(scheduleSnap)
}
