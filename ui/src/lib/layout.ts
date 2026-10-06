import { saveScroll, scrollPositions } from './editor.svelte'

// Calls back at most once per frame while the content or the area around it changes
function watchChanges(node: HTMLElement, area: HTMLElement, changed: () => void): () => void {
    let frame = 0

    const schedule = () => {
        if (frame === 0)
            frame = requestAnimationFrame(() => {
                frame = 0
                changed()
            })
    }

    const mutations = new MutationObserver(schedule)
    mutations.observe(node, { childList: true, subtree: true, characterData: true })

    const resizes = new ResizeObserver(schedule)
    resizes.observe(area)
    resizes.observe(node)

    return () => {
        cancelAnimationFrame(frame)
        mutations.disconnect()
        resizes.disconnect()
    }
}

// Two columns: the left one fills to the area's height, then the right one. Content longer than both columns is
// split evenly between them, and the area scrolls. Off: one column.
export function columnFlow(node: HTMLElement, enabled: boolean) {
    const area = node.parentElement ?? node

    const fit = () => {
        node.classList.toggle('columns', enabled)
        node.classList.remove('balanced')

        // Past two columns' worth, multi-column layout adds columns off to the side
        if (enabled && node.scrollWidth > node.clientWidth + 1) node.classList.add('balanced')
    }

    fit()
    const stopWatching = watchChanges(node, area, fit)

    return {
        update(next: boolean) {
            enabled = next
            fit()
        },
        destroy: stopWatching,
    }
}

// Keeps a scrolling area's position for each view it shows (by area name, see scrollAreas), sends it to the plugin
// once scrolling stops, and puts it back when that view returns: also once late content (e.g. the parameter list)
// makes the area tall enough, unless the user has scrolled since
export function scrollMemory(node: HTMLElement, name: string) {
    const content = node.firstElementChild instanceof HTMLElement ? node.firstElementChild : node
    let pending: number | null = null
    let saveTimer = 0
    const unsaved = new Set<string>()

    const restore = () => {
        if (pending === null) return

        node.scrollTop = pending

        if (Math.abs(node.scrollTop - pending) <= 1) pending = null
    }

    const show = (next: string) => {
        name = next
        pending = scrollPositions.get(name) ?? 0
        restore()
    }

    const save = () => {
        saveTimer = 0

        for (const area of unsaved) saveScroll(area, scrollPositions.get(area) ?? 0)

        unsaved.clear()
    }

    const scrolled = () => {
        // Still waiting to put a saved position back: this is the area being too short for it yet
        if (pending !== null || Math.abs(node.scrollTop - (scrollPositions.get(name) ?? 0)) < 1) return

        scrollPositions.set(name, node.scrollTop)
        unsaved.add(name)
        clearTimeout(saveTimer)
        saveTimer = window.setTimeout(save, 250)
    }

    const userTookOver = () => {
        pending = null
    }

    const takeOverEvents = ['wheel', 'pointerdown', 'keydown'] as const
    node.addEventListener('scroll', scrolled)

    for (const type of takeOverEvents) node.addEventListener(type, userTookOver, { passive: true })

    const stopWatching = watchChanges(content, node, restore)
    show(name)

    return {
        update(next: string) {
            if (next !== name) show(next)
        },
        destroy() {
            stopWatching()
            node.removeEventListener('scroll', scrolled)

            for (const type of takeOverEvents) node.removeEventListener(type, userTookOver)

            if (saveTimer !== 0) {
                clearTimeout(saveTimer)
                save()
            }
        },
    }
}
