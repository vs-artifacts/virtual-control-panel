import { onParams, sendGesture, sendSet, type ValueChange } from './bridge'

export interface Param {
    h: number
    slot: string
    plugin: string
    name: string
    host: string
    value: number
    text: string
}

export const controls = $state({ params: [] as Param[] })

const indexByHandle = new Map<number, number>()
const inGesture = new Set<number>()
const pending = new Map<number, number>()
let flushRequested = false

function isRecord(value: unknown): value is Record<string, unknown> {
    return typeof value === 'object' && value !== null
}

function isNormalised(value: unknown): value is number {
    return typeof value === 'number' && Number.isFinite(value) && value >= 0 && value <= 1
}

function parseEntry(raw: unknown): Param | null {
    if (!isRecord(raw)) return null

    const { h, slot, plugin, name, host, v, t } = raw

    if (typeof h !== 'number' || !Number.isInteger(h) || !isNormalised(v) || typeof t !== 'string') return null
    if (typeof slot !== 'string' || typeof plugin !== 'string' || typeof name !== 'string' || typeof host !== 'string') return null

    return { h, slot, plugin, name, host, value: v, text: t }
}

export function loadCatalog(raw: unknown): boolean {
    if (!Array.isArray(raw)) return false

    const params = raw.map(parseEntry).filter((param) => param !== null)

    indexByHandle.clear()
    params.forEach((param, index) => indexByHandle.set(param.h, index))
    inGesture.clear()
    pending.clear()
    controls.params = params
    return true
}

function applyChanges(raw: unknown): void {
    if (!Array.isArray(raw)) return

    for (const change of raw) {
        if (!isRecord(change) || typeof change.h !== 'number') continue

        const index = indexByHandle.get(change.h)

        if (index === undefined) continue

        const param = controls.params[index]

        // While the page is moving it, its own value wins: replies still on their way would make it jitter
        if (isNormalised(change.v) && !inGesture.has(change.h)) param.value = change.v
        if (typeof change.t === 'string') param.text = change.t
    }
}

export function listenForChanges(): void {
    onParams(applyChanges)
}

function flush(): void {
    flushRequested = false

    if (pending.size === 0) return

    const batch: ValueChange[] = [...pending].map(([h, v]) => ({ h, v }))
    pending.clear()
    sendSet(batch)
}

// Sends at most one batch per frame, however often the pointer moves
export function setValue(h: number, value: number): void {
    const index = indexByHandle.get(h)

    if (index === undefined) return

    controls.params[index].value = value
    pending.set(h, value)

    if (!flushRequested) {
        flushRequested = true
        requestAnimationFrame(flush)
    }
}

export function beginGesture(h: number): void {
    if (inGesture.has(h)) return

    inGesture.add(h)
    sendGesture(h, true)
}

export function endGesture(h: number): void {
    if (!inGesture.delete(h)) return

    flush()
    sendGesture(h, false)
}
