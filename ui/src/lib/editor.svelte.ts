import { fetchEditorState, onHostDiagnostics, onProcessing, sendDiagnostics, sendMutePlugin, sendOpenPlugin, sendResetSize, sendScroll, sendSetting, sendTab } from './bridge'
import { link, listenForLink, readLink } from './link.svelte'
import { asRecord, oneOf } from './read'

export const tabNames = ['Panel', 'Plugins', 'Settings'] as const

// The page's scrolling areas, as the plugin saves them: each tab in two columns, each tab beside Diagnostics (one
// column), and Diagnostics
export type TabLayout = 'columns' | 'single'
export const scrollAreas = [...tabNames.flatMap((_, tab) => [`tab${tab}.columns`, `tab${tab}.single`]), 'diagnostics']
const maxScroll = 1000000

export function tabScrollArea(tab: number, layout: TabLayout): string {
    return `tab${tab}.${layout}`
}

const placements = ['off', 'hostPlaced', 'mouse', 'hostMainWindow'] as const
const attachedPlacements = ['off', 'workspace', 'hostMonitor'] as const
export const renderings = ['smooth', 'sharp'] as const
const dropoutModes = ['safe', 'silence'] as const
const roundTrips = [0, 1, 2, 3] as const

// The bay bypass crossfade: whole milliseconds, 0 is off
export const crossfadeRange = { min: 0, max: 50 } as const

function isCrossfadeMs(value: unknown): value is number {
    return typeof value === 'number' && Number.isInteger(value) && value >= crossfadeRange.min && value <= crossfadeRange.max
}

// The longest gap between the clicks of a double-click, in whole milliseconds
export const doubleClickRange = { min: 1, max: 2000 } as const

function isDoubleClickMs(value: unknown): value is number {
    return typeof value === 'number' && Number.isInteger(value) && value >= doubleClickRange.min && value <= doubleClickRange.max
}

// Plugin-wide preferences, stored by the plugin
export interface Settings {
    keepWebViewWarm: boolean
    editorPlacement: (typeof placements)[number]
    attachedPlacement: (typeof attachedPlacements)[number]
    resizeRendering: (typeof renderings)[number]
    resizeFlipAlt: boolean
    flOutputCorrection: boolean
    bayDropouts: (typeof dropoutModes)[number]
    // 0: automatic
    roundTripBlocks: (typeof roundTrips)[number]
    bypassCrossfadeMs: number
    doubleClickMs: number
    // Placeholder: double-click will open a value entry instead of resetting
    doubleClickSets: boolean
}

export interface HostedPlugin {
    slot: number
    label: string
    name: string
    status: string
    canOpen: boolean
    canMute: boolean
    muted: boolean
}

// ready: the plugin has said which tab was showing, so the page doesn't flash another one first
export const editor = $state({
    ready: false,
    title: '',
    tab: 0,
    diagnostics: false,
    settings: {
        keepWebViewWarm: false,
        editorPlacement: 'off',
        attachedPlacement: 'off',
        resizeRendering: 'smooth',
        resizeFlipAlt: false,
        flOutputCorrection: true,
        bayDropouts: 'safe',
        roundTripBlocks: 0,
        bypassCrossfadeMs: 30,
        doubleClickMs: 500,
        doubleClickSets: false,
    } as Settings,
    plugins: [] as HostedPlugin[],
    // Unknown (null) until the plugin says
    processing: null as boolean | null,
    // Test-build diagnostics: the last forwarded keys (newest first) and the windows around the editor
    keyLog: [] as string[],
    windows: [] as string[],
})

// Only a page that has heard from the plugin sends changes back to it
let connected = false

// By area name (see scrollAreas), in page pixels; not reactive, since only the scroll areas read it
export const scrollPositions = new Map<string, number>()

function readScroll(raw: unknown): void {
    const fields = asRecord(raw) ?? {}

    for (const area of scrollAreas) {
        const top = fields[area]

        if (typeof top === 'number' && Number.isInteger(top) && top >= 0 && top <= maxScroll) scrollPositions.set(area, top)
    }
}

function readPlugins(raw: unknown): HostedPlugin[] {
    if (!Array.isArray(raw)) return []

    return raw.flatMap((entry) => {
        const { slot, label, name, status, canOpen, canMute, muted } = asRecord(entry) ?? {}

        if (typeof slot !== 'number' || !Number.isInteger(slot) || typeof label !== 'string' || typeof name !== 'string' || typeof status !== 'string') return []

        return [{ slot, label, name, status, canOpen: canOpen === true, canMute: canMute === true, muted: muted === true }]
    })
}

function readSettings(raw: unknown): void {
    const fields = asRecord(raw)

    if (fields === null) return

    const settings = editor.settings

    if (typeof fields.keepWebViewWarm === 'boolean') settings.keepWebViewWarm = fields.keepWebViewWarm
    if (typeof fields.flOutputCorrection === 'boolean') settings.flOutputCorrection = fields.flOutputCorrection
    if (typeof fields.resizeFlipAlt === 'boolean') settings.resizeFlipAlt = fields.resizeFlipAlt
    if (typeof fields.doubleClickSets === 'boolean') settings.doubleClickSets = fields.doubleClickSets
    if (isDoubleClickMs(fields.doubleClickMs)) settings.doubleClickMs = fields.doubleClickMs

    settings.editorPlacement = oneOf(placements, fields.editorPlacement) ?? settings.editorPlacement
    settings.attachedPlacement = oneOf(attachedPlacements, fields.attachedPlacement) ?? settings.attachedPlacement
    settings.resizeRendering = oneOf(renderings, fields.resizeRendering) ?? settings.resizeRendering
    settings.bayDropouts = oneOf(dropoutModes, fields.bayDropouts) ?? settings.bayDropouts
    settings.roundTripBlocks = roundTrips.find((blocks) => blocks === fields.roundTripBlocks) ?? settings.roundTripBlocks
    if (isCrossfadeMs(fields.bypassCrossfadeMs)) settings.bypassCrossfadeMs = fields.bypassCrossfadeMs
}

function readLines(raw: unknown, limit: number): string[] {
    return Array.isArray(raw) ? raw.slice(0, limit).filter((line): line is string => typeof line === 'string') : []
}

export async function loadEditorState(): Promise<void> {
    onProcessing((message) => {
        const running = asRecord(message)?.running

        if (typeof running === 'boolean') editor.processing = running
    })

    onHostDiagnostics((message) => {
        const fields = asRecord(message) ?? {}
        editor.keyLog = readLines(fields.keys, 32)
        editor.windows = readLines(fields.windows, 32)
    })

    listenForLink()

    try {
        const state = asRecord(await fetchEditorState())

        if (state === null) return

        connected = true

        if (typeof state.title === 'string') editor.title = state.title
        if (typeof state.tab === 'number' && Number.isInteger(state.tab) && state.tab >= 0 && state.tab < tabNames.length) editor.tab = state.tab
        if (typeof state.diagnostics === 'boolean') editor.diagnostics = state.diagnostics
        if (typeof state.processing === 'boolean') editor.processing = state.processing

        readScroll(state.scroll)
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

export function mutePlugin(slot: number, muted: boolean): void {
    const plugin = editor.plugins.find((entry) => entry.slot === slot)

    if (plugin === undefined || !plugin.canMute) return

    plugin.muted = muted

    if (connected) sendMutePlugin(slot, muted)
}

export function showDiagnostics(open: boolean): void {
    editor.diagnostics = open

    if (connected) sendDiagnostics(open)
}

export function saveScroll(area: string, top: number): void {
    const rounded = Math.min(maxScroll, Math.max(0, Math.round(top)))
    scrollPositions.set(area, rounded)

    if (connected) sendScroll(area, rounded)
}
