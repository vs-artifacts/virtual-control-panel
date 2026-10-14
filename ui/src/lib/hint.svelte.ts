// The hint pane shows the hint of whatever the pointer is over: the innermost element with one (see hinted)
export const hint = $state({ text: '' })

const texts = new Map<Element, string>()
let current: Element | null = null

function show(element: Element | null): void {
    current = element
    hint.text = element === null ? '' : (texts.get(element) ?? '')
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

            if (current === node) hint.text = next
        },
        destroy() {
            texts.delete(node)

            if (current === node) show(null)
        },
    }
}
