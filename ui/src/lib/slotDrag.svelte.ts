import { currentZoom } from './display.svelte'
import { moveSlot } from './editor.svelte'

// Dragging a slot by its header: pointer tracking with a drop line, not the browser's drag and drop (which could clash
// with the host's own file dropping)
export type DragKind = 'generator' | 'fx'

export const slotDrag = $state({
    active: false,
    slot: 0,
    kind: 'fx' as DragKind,
    label: '',
    // The ghost's position, in page pixels
    x: 0,
    y: 0,
    // Where it would land: a bus (0: the generator stage), and its place there counted without the slot itself
    target: null as { bus: number; index: number } | null,
})

// Pointer travel before a press becomes a drag, and a scrolling row's edge zone and speed (page pixels, per frame)
const startDistance = 5
const edgeZone = 40
const edgeSpeed = 12

let start: { x: number; y: number; pointerId: number } | null = null
let pointer = { x: 0, y: 0 }
let frame = 0

export function beginSlotDrag(event: PointerEvent, slot: number, kind: DragKind, label: string): void {
    if (event.button !== 0 || (event.target instanceof Element && event.target.closest('button, input, select, label') !== null)) return

    end()
    start = { x: event.clientX, y: event.clientY, pointerId: event.pointerId }
    slotDrag.slot = slot
    slotDrag.kind = kind
    slotDrag.label = label
    window.addEventListener('pointermove', moved)
    window.addEventListener('pointerup', released)
    window.addEventListener('pointercancel', end)
    window.addEventListener('keydown', escape)
}

function moved(event: PointerEvent): void {
    if (start === null || event.pointerId !== start.pointerId) return

    pointer = { x: event.clientX, y: event.clientY }

    if (!slotDrag.active) {
        if (Math.hypot(pointer.x - start.x, pointer.y - start.y) < startDistance) return

        slotDrag.active = true
        frame = requestAnimationFrame(scrollAtEdges)
    }

    track()
}

function track(): void {
    const zoom = currentZoom()
    slotDrag.x = pointer.x / zoom
    slotDrag.y = pointer.y / zoom
    slotDrag.target = findTarget()
}

// Drop targets carry data-drop-bus (a bus's column, or 0: the generator row, which runs sideways) and hold their slots
// as data-drop-slot elements. Generators only move among generators, other slots only among buses.
function findTarget(): { bus: number; index: number } | null {
    const column = document.elementFromPoint(pointer.x, pointer.y)?.closest<HTMLElement>('[data-drop-bus]')

    if (column == null) return null

    const bus = Number(column.dataset.dropBus)

    if (!Number.isInteger(bus) || (bus === 0) !== (slotDrag.kind === 'generator')) return null

    const sideways = bus === 0
    const slots = [...column.querySelectorAll<HTMLElement>('[data-drop-slot]')].filter((element) => Number(element.dataset.dropSlot) !== slotDrag.slot)
    const index = slots.findIndex((element) => {
        const bounds = element.getBoundingClientRect()
        return sideways ? pointer.x < bounds.left + bounds.width / 2 : pointer.y < bounds.top + bounds.height / 2
    })

    return { bus, index: index < 0 ? slots.length : index }
}

// Near the edge of a row that scrolls sideways (data-drag-scroll), the row scrolls
function scrollAtEdges(): void {
    frame = 0

    if (!slotDrag.active) return

    const row = document.elementFromPoint(pointer.x, pointer.y)?.closest<HTMLElement>('[data-drag-scroll]')

    if (row != null) {
        const bounds = row.getBoundingClientRect()
        const zone = edgeZone * currentZoom()
        const before = row.scrollLeft

        if (pointer.x < bounds.left + zone) row.scrollLeft -= edgeSpeed
        else if (pointer.x > bounds.right - zone) row.scrollLeft += edgeSpeed

        if (row.scrollLeft !== before) track()
    }

    frame = requestAnimationFrame(scrollAtEdges)
}

function released(event: PointerEvent): void {
    if (start === null || event.pointerId !== start.pointerId) return

    if (slotDrag.active && slotDrag.target !== null) moveSlot(slotDrag.slot, slotDrag.target.bus, slotDrag.target.index)

    end()
}

function escape(event: KeyboardEvent): void {
    if (event.key === 'Escape') end()
}

function end(): void {
    start = null
    slotDrag.active = false
    slotDrag.target = null
    cancelAnimationFrame(frame)
    frame = 0
    window.removeEventListener('pointermove', moved)
    window.removeEventListener('pointerup', released)
    window.removeEventListener('pointercancel', end)
    window.removeEventListener('keydown', escape)
}
