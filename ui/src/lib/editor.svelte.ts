import { fetchEditorState, onProcessing, sendOpenPlugin, sendResetSize, sendSetting, sendTab } from './bridge'
import { link, listenForLink, readLink } from './link.svelte'
import { asRecord, oneOf } from './read'

export const tabNames = ['Panel', 'Plugins', 'Settings'] as const

const placements = ['off', 'hostPlaced', 'mouse', 'hostMainWindow'] as const
const resizings = ['smooth', 'sharp'] as const
const anchors = ['oppositeCorner', 'windowCenter'] as const
const missingModes = ['silence', 'bypass'] as const
const roundTrips = [1, 2, 3] as const

// Plugin-wide preferences, stored by the plugin
export interface Settings {
    keepWebViewWarm: boolean
    editorPlacement: (typeof placements)[number]
    resizing: (typeof resizings)[number]
    resizeAnchor: (typeof anchors)[number]
    flOutputCorrection: boolean
    missingReturnBlocks: (typeof missingModes)[number]
    roundTripBlocks: (typeof roundTrips)[number]
}

export interface HostedPlugin {
    slot: number
    label: string
    name: string
    status: string
    canOpen: boolean
}

// ready: the plugin has said which tab was showing, so the page doesn't flash another one first
export const editor = $state({
    ready: false,
    title: '',
    tab: 0,
    settings: {
        keepWebViewWarm: false,
        editorPlacement: 'off',
        resizing: 'smooth',
        resizeAnchor: 'oppositeCorner',
        flOutputCorrection: true,
        missingReturnBlocks: 'silence',
        roundTripBlocks: 2,
    } as Settings,
    plugins: [] as HostedPlugin[],
    // Unknown (null) until the plugin says
    processing: null as boolean | null,
})

// Only a page that has heard from the plugin sends changes back to it
let connected = false

function readPlugins(raw: unknown): HostedPlugin[] {
    if (!Array.isArray(raw)) return []

    return raw.flatMap((entry) => {
        const { slot, label, name, status, canOpen } = asRecord(entry) ?? {}

        if (typeof slot !== 'number' || !Number.isInteger(slot) || typeof label !== 'string' || typeof name !== 'string' || typeof status !== 'string') return []

        return [{ slot, label, name, status, canOpen: canOpen === true }]
    })
}

function readSettings(raw: unknown): void {
    const fields = asRecord(raw)

    if (fields === null) return

    const settings = editor.settings

    if (typeof fields.keepWebViewWarm === 'boolean') settings.keepWebViewWarm = fields.keepWebViewWarm
    if (typeof fields.flOutputCorrection === 'boolean') settings.flOutputCorrection = fields.flOutputCorrection

    settings.editorPlacement = oneOf(placements, fields.editorPlacement) ?? settings.editorPlacement
    settings.resizing = oneOf(resizings, fields.resizing) ?? settings.resizing
    settings.resizeAnchor = oneOf(anchors, fields.resizeAnchor) ?? settings.resizeAnchor
    settings.missingReturnBlocks = oneOf(missingModes, fields.missingReturnBlocks) ?? settings.missingReturnBlocks
    settings.roundTripBlocks = roundTrips.find((blocks) => blocks === fields.roundTripBlocks) ?? settings.roundTripBlocks
}

export async function loadEditorState(): Promise<void> {
    onProcessing((message) => {
        const running = asRecord(message)?.running

        if (typeof running === 'boolean') editor.processing = running
    })

    listenForLink()

    try {
        const state = asRecord(await fetchEditorState())

        if (state === null) return

        connected = true

        if (typeof state.title === 'string') editor.title = state.title
        if (typeof state.tab === 'number' && Number.isInteger(state.tab) && state.tab >= 0 && state.tab < tabNames.length) editor.tab = state.tab
        if (typeof state.processing === 'boolean') editor.processing = state.processing

        readSettings(state.settings)
        editor.plugins = readPlugins(state.plugins)
        link.status = readLink(state.link)
    } catch {
        // The Panel tab still works; the other tabs just have nothing from the plugin
    } finally {
        editor.ready = true
    }
}

export function showTab(index: number): void {
    editor.tab = index

    if (connected) sendTab(index)
}

export function changeSetting<Name extends keyof Settings>(name: Name, value: Settings[Name]): void {
    editor.settings[name] = value

    if (connected) sendSetting(name, value)
}

export function resetSize(): void {
    if (connected) sendResetSize()
}

export function openPlugin(slot: number): void {
    if (connected) sendOpenPlugin(slot)
}
