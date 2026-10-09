import { fetchParameterMenu, sendMenuChoice, sendMenuClosed } from './bridge'
import { currentZoom } from './display.svelte'

export type MenuItem =
    | { kind: 'action'; id: number; text: string; enabled: boolean; checked: boolean }
    | { kind: 'separator' }
    | { kind: 'header'; text: string }
    | { kind: 'submenu'; text: string; enabled: boolean; items: MenuItem[] }

// Where the pointer was, in page pixels (before the page's zoom)
export const menu = $state({ open: false, x: 0, y: 0, items: [] as MenuItem[] })

const maxDepth = 8

// Only the latest right-click gets its menu, however long the replies take
let latestRequest = 0

// A menu the page made: each item's action, by its id. Null for the plugin's menus, whose choices go to the plugin.
let pageActions: (() => void)[] | null = null

export interface PageMenuItem {
    text: string
    enabled: boolean
    action: () => void
}

function parseItems(raw: unknown, depth: number): MenuItem[] {
    if (!Array.isArray(raw) || depth > maxDepth) return []

    const items: MenuItem[] = []

    for (const entry of raw) {
        if (typeof entry !== 'object' || entry === null) continue

        const { id, text, enabled, checked, separator, header, items: children } = entry as Record<string, unknown>

        if (separator === true) items.push({ kind: 'separator' })
        else if (typeof header === 'string') items.push({ kind: 'header', text: header })
        else if (typeof text === 'string' && Array.isArray(children)) items.push({ kind: 'submenu', text, enabled: enabled === true, items: parseItems(children, depth + 1) })
        else if (typeof text === 'string' && typeof id === 'number' && Number.isInteger(id)) items.push({ kind: 'action', id, text, enabled: enabled === true, checked: checked === true })
    }

    return items
}

// Asks the plugin for the options for this parameter, then shows them where the pointer is
export async function openParameterMenu(h: number, clientX: number, clientY: number): Promise<void> {
    closeMenu()

    const request = ++latestRequest
    let reply: unknown

    try {
        reply = await fetchParameterMenu(h)
    } catch {
        return
    }

    if (request !== latestRequest || typeof reply !== 'object' || reply === null) return

    const items = parseItems((reply as Record<string, unknown>).items, 0)
    pageActions = null
    show(items.length > 0 ? items : [{ kind: 'header', text: 'No options from the host' }], clientX, clientY)
}

export function openPageMenu(items: PageMenuItem[], clientX: number, clientY: number): void {
    closeMenu()
    ++latestRequest
    pageActions = items.map((item) => item.action)
    show(
        items.map((item, id) => ({ kind: 'action', id, text: item.text, enabled: item.enabled, checked: false })),
        clientX,
        clientY,
    )
}

function show(items: MenuItem[], clientX: number, clientY: number): void {
    const zoom = currentZoom()
    menu.items = items
    menu.x = clientX / zoom
    menu.y = clientY / zoom
    menu.open = true
}

export function chooseMenuItem(id: number): void {
    menu.open = false

    if (pageActions === null) {
        sendMenuChoice(id)
        return
    }

    const action = pageActions[id]
    pageActions = null
    action?.()
}

export function closeMenu(): void {
    if (!menu.open) return

    menu.open = false

    if (pageActions === null) sendMenuClosed()

    pageActions = null
}
