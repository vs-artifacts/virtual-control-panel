import { view } from './display.svelte'
import { editor } from './editor.svelte'

// The hint pane shows the hint of whatever the pointer is over: the innermost element with one (see hinted). warning: a
// forced hint (see forceHint), shown in the warning colour.
export const hint = $state({ text: '', warning: false })

const texts = new Map<Element, string>()
let current: Element | null = null
let forced: string | null = null

function show(element: Element | null): void {
    current = element

    if (forced === null) hint.text = element === null ? '' : (texts.get(element) ?? '')
}

// Shown whatever the pointer is over until released: a warning waiting for its confirmation
export function forceHint(text: string): void {
    forced = text
    hint.text = text
    hint.warning = true
}

export function releaseHint(): void {
    if (forced === null) return

    forced = null
    hint.warning = false
    show(current)
}

function closestHinted(target: EventTarget | null): Element | null {
    return target instanceof Element ? target.closest('[data-hinted]') : null
}

// Alt+wheel scrolls the hint pane (a hint only shows while the pointer is over its element), a line at most per step
let altScrolled = false

function scrollHint(event: WheelEvent): void {
    const pane = document.getElementById('hint')

    if (!event.altKey || pane === null || hint.text === '') return

    event.preventDefault()
    altScrolled = true
    stopAutoScroll?.()

    const line = parseFloat(getComputedStyle(pane).lineHeight) || 21
    const delta = event.deltaMode === WheelEvent.DOM_DELTA_PIXEL ? Math.max(-line, Math.min(line, event.deltaY)) : Math.sign(event.deltaY) * line
    pane.scrollTop += delta
}

// Releasing Alt after scrolling mustn't reach the host as a lone Alt (it would open its menu bar)
function swallowAlt(event: KeyboardEvent): void {
    if (event.key !== 'Alt') return

    if (event.type === 'keydown') {
        if (!event.repeat) altScrolled = false
    } else if (altScrolled) event.preventDefault()
}

// Once, for the whole page
export function followHints(): void {
    document.addEventListener('pointerover', (event) => show(closestHinted(event.target)))
    document.addEventListener('pointerout', (event) => {
        if (event.relatedTarget === null) show(null)
    })
    document.addEventListener('wheel', scrollHint, { passive: false, capture: true })
    window.addEventListener('keydown', swallowAlt, { capture: true })
    window.addEventListener('keyup', swallowAlt, { capture: true })
}

// An element's hint: use:hinted={text}. The text may change while it shows.
export function hinted(node: HTMLElement, text: string) {
    node.dataset.hinted = ''
    texts.set(node, text)

    return {
        update(next: string) {
            texts.set(node, next)

            if (current === node && forced === null) hint.text = next
        },
        destroy() {
            texts.delete(node)

            if (current === node) show(null)
        },
    }
}

// Smart delay: 1 s, and 1 s more per 400 page pixels of the window's width (Diagnostics left out), to a tenth
export function smartHintDelay(): number {
    const width = view.width / view.uiScale - (editor.diagnostics ? 400 : 0)
    return Math.round((1 + width / 400) * 10) / 10
}

// Null: no automatic scrolling
function scrolling(): { speed: number; delay: number; end: number } | null {
    const settings = editor.settings

    if (!settings.hintAutoScroll || settings.hintScrollSpeed <= 0) return null

    const smart = settings.hintSmartDelay ? smartHintDelay() : null
    return { speed: settings.hintScrollSpeed, delay: smart ?? settings.hintScrollDelaySeconds, end: smart ?? settings.hintScrollEndSeconds }
}

let stopAutoScroll: (() => void) | null = null

// On the pane (made anew for each hint): a hint too long for it waits, scrolls at a steady speed, waits at the bottom,
// jumps back to the top and starts again. Scrolling it by hand (Alt+wheel) stops that for this hint.
export function autoScroll(node: HTMLElement) {
    let timer = 0
    let frame = 0
    let stopped = false

    const stop = () => {
        stopped = true
        clearTimeout(timer)
        cancelAnimationFrame(frame)
    }

    const room = () => node.scrollHeight - node.clientHeight

    const start = () => {
        const settings = scrolling()

        if (stopped) return

        // Checked again now and then: the setting or the pane's size may change while the hint shows
        if (settings === null || room() <= 1) timer = window.setTimeout(start, 500)
        else timer = window.setTimeout(scroll, settings.delay * 1000)
    }

    const scroll = () => {
        let position = node.scrollTop
        let last = performance.now()

        const step = (now: number) => {
            const settings = scrolling()

            if (stopped) return
            if (settings === null) return start()

            const line = parseFloat(getComputedStyle(node).lineHeight) || 21
            position += (settings.speed * line * (now - last)) / 1000
            last = now

            if (position >= room()) {
                node.scrollTop = room()
                timer = window.setTimeout(() => {
                    node.scrollTop = 0
                    start()
                }, settings.end * 1000)
                return
            }

            node.scrollTop = position
            frame = requestAnimationFrame(step)
        }

        frame = requestAnimationFrame(step)
    }

    stopAutoScroll = stop
    start()

    return {
        destroy() {
            stop()

            if (stopAutoScroll === stop) stopAutoScroll = null
        },
    }
}
