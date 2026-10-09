import factoryDefaults from '../../../source/SettingsDefaults.json'
import { fetchEditorState, onHostDiagnostics, onProcessing, sendDiagnostics, sendMutePlugin, sendNamesProbe, sendOpenPlugin, sendResetSettings, sendResetSize, sendScroll, sendSetting, sendSettingDefault, sendTab } from './bridge'
import { link, listenForLink, readLink } from './link.svelte'
import { asRecord } from './read'

export const tabNames = ['Panel', 'Plugins', 'Settings'] as const

// The page's scrolling areas, as the plugin saves them: each tab in two columns, each tab beside Diagnostics (one
// column), and Diagnostics
export type TabLayout = 'columns' | 'single'
export const scrollAreas = [...tabNames.flatMap((_, tab) => [`tab${tab}.columns`, `tab${tab}.single`]), 'diagnostics']
const maxScroll = 1000000

export function tabScrollArea(tab: number, layout: TabLayout): string {
    return `tab${tab}.${layout}`
}

export const renderings = ['smooth', 'sharp'] as const
const roundTrips = [0, 1, 2, 3] as const
export const stoppedShrinkRules = ['never', 'whenQuiet', 'always'] as const

// Plugin-wide preferences, stored by the plugin
export interface Settings {
    keepWebViewWarm: boolean
    editorPlacement: 'off' | 'hostPlaced' | 'mouse' | 'hostMainWindow'
    attachedPlacement: 'off' | 'workspace' | 'hostMonitor'
    resizeRendering: (typeof renderings)[number]
    resizeFlipAlt: boolean
    resizeBlocksLanguageSwitch: boolean
    flOutputCorrection: boolean
    // 0: automatic
    bayRoundTrip: (typeof roundTrips)[number]
    // Count the round trip in the host's real blocks, not the announced block size
    bayRealBlocks: boolean
    // BPM; 0: none
    bayLowestTempo: number
    bayStoppedShrink: (typeof stoppedShrinkRules)[number]
    bayQuietLevelDb: number
    bayQuietMs: number
    bayJumpCrossfadeMs: number
    bayLatencyFadeMs: number
    bayMuteFadeMs: number
    bayBypassFadeMs: number
    bayOffFadeMs: number
    bayDropoutFadeMs: number
    bayLatencySettleMs: number
    bayTailCutReleaseMs: number
    doubleClickMs: number
    // Placeholder: double-click will open a value entry instead of resetting
    doubleClickSets: boolean
}

// Every setting's factory default and limits, from the file the plugin is built from (source/SettingsDefaults.json)
type Definition =
    | { type: 'flag'; default: boolean }
    | { type: 'whole' | 'decimal'; default: number; min: number; max: number; unit: string }
    | { type: 'choice'; default: string; choices: string[] }

export type SettingKey = keyof typeof factoryDefaults
export const settingKeys = Object.keys(factoryDefaults) as SettingKey[]
const definitions = factoryDefaults as Record<SettingKey, Definition>

// Fails the type check if the page's settings and the defaults file list different settings
const sameKeys: [keyof Settings] extends [SettingKey] ? ([SettingKey] extends [keyof Settings] ? true : never) : never = true
void sameKeys

export function definitionOf(key: SettingKey): Definition {
    return definitions[key]
}

function rangeOf(key: SettingKey): { min: number; max: number } {
    const definition = definitions[key]

    if (definition.type !== 'whole' && definition.type !== 'decimal') throw new Error(`${key} has no range`)

    return { min: definition.min, max: definition.max }
}

// Real blocks while stopped: the output counts as quiet below this level (whole dBFS) for this long (whole ms)
export const quietLevelRange = rangeOf('bayQuietLevelDb')
export const quietMsRange = rangeOf('bayQuietMs')

// The bay's fades, one per cause (whole milliseconds, 0 is off), by their settings keys
export const fadeSettings = [
    { key: 'bayJumpCrossfadeMs', max: rangeOf('bayJumpCrossfadeMs').max, label: 'Round trip change', covers: 'the timing jump when the round trip changes (tempo, Automatic, Settings)' },
    { key: 'bayLatencyFadeMs', max: rangeOf('bayLatencyFadeMs').max, label: 'Bay latency change', covers: "the fade out and back in around the bay's sound held back for a latency change (e.g. linear phase), and the timing jump" },
    { key: 'bayMuteFadeMs', max: rangeOf('bayMuteFadeMs').max, label: 'Mute', covers: "between the dry sound and the bay's, entering and leaving Mute" },
    { key: 'bayBypassFadeMs', max: rangeOf('bayBypassFadeMs').max, label: 'Bypass', covers: "between the dry sound and the bay's, and the bay's send, entering and leaving Bypass" },
    { key: 'bayOffFadeMs', max: rangeOf('bayOffFadeMs').max, label: 'Off', covers: "the timing jump, the bay's send and the fade back in, entering and leaving Off" },
    { key: 'bayDropoutFadeMs', max: rangeOf('bayDropoutFadeMs').max, label: 'Dropouts', covers: "the fade out and back in around a missing return, and into the bay's first sound" },
] as const

export type FadeKey = (typeof fadeSettings)[number]['key']

// How long a new bay latency must hold before it's used: whole milliseconds
export const settleRange = rangeOf('bayLatencySettleMs')

// How much longer a bay's sound is held back after it leaves Bypass or Off: whole milliseconds
export const releaseRange = rangeOf('bayTailCutReleaseMs')

// The longest gap between the clicks of a double-click, in whole milliseconds
export const doubleClickRange = rangeOf('doubleClickMs')

// Real blocks start at one tick at this tempo at least: BPM, 0 for none
export const lowestTempoRange = rangeOf('bayLowestTempo')

// Whether a value from the plugin (or the page) fits a setting
export function fits(key: SettingKey, value: unknown): boolean {
    const definition = definitions[key]

    switch (definition.type) {
        case 'flag':
            return typeof value === 'boolean'
        case 'whole':
            return typeof value === 'number' && Number.isInteger(value) && value >= definition.min && value <= definition.max
        case 'decimal':
            return typeof value === 'number' && Number.isFinite(value) && value >= definition.min && value <= definition.max
        case 'choice':
            return typeof value === 'string' && definition.choices.includes(value)
    }
}

export function factoryDefault<Name extends SettingKey>(key: Name): Settings[Name] {
    return definitions[key].default as Settings[Name]
}

// The user's own default where there is one (set from a setting's right-click menu), else the factory's
export function defaultOf<Name extends SettingKey>(key: Name): Settings[Name] {
    return (editor.userDefaults[key] ?? factoryDefault(key)) as Settings[Name]
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
    settings: Object.fromEntries(settingKeys.map((key) => [key, definitions[key].default])) as unknown as Settings,
    // The user's own defaults, where set
    userDefaults: {} as Partial<Settings>,
    plugins: [] as HostedPlugin[],
    // Unknown (null) until the plugin says
    processing: null as boolean | null,
    // Test-build diagnostics: the last forwarded keys (newest first) and the windows around the editor
    keyLog: [] as string[],
    windows: [] as string[],
    // Names probe (test build): what the host is told Param 1 is called
    probeName: '',
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

// Only what fits each setting is taken
function readSettings(raw: unknown, into: Partial<Settings>): void {
    const fields = asRecord(raw)

    if (fields === null) return

    for (const key of settingKeys) if (fits(key, fields[key])) (into as Record<SettingKey, unknown>)[key] = fields[key]
}

function readUserDefaults(raw: unknown): void {
    const defaults: Partial<Settings> = {}
    readSettings(raw, defaults)
    editor.userDefaults = defaults
}

function readLines(raw: unknown, limit: number): string[] {
    return Array.isArray(raw) ? raw.slice(0, limit).filter((line): line is string => typeof line === 'string') : []
}

function readProbeName(raw: unknown): void {
    if (typeof raw === 'string') editor.probeName = raw.slice(0, 200)
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
        readSettings(state.settings, editor.settings)
        readUserDefaults(state.userDefaults)
        readProbeName(state.probeName)
        editor.plugins = readPlugins(state.plugins)
        link.status = readLink(state.link)
    } catch {
        // The Panel tab still works; the other tabs just have nothing from the plugin
    } finally {
        editor.ready = true
    }
}

// The settings as the plugin now has them (after a reset, which the plugin works out)
async function refreshFromPlugin(): Promise<void> {
    try {
        const state = asRecord(await fetchEditorState())

        if (state === null) return

        readSettings(state.settings, editor.settings)
        readUserDefaults(state.userDefaults)
        readProbeName(state.probeName)
    } catch {
        // The page keeps what it showed
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

// Sets the value and makes it the setting's default; one equal to the factory's forgets the user's own
export function setAsDefault<Name extends SettingKey>(name: Name, value: Settings[Name]): void {
    if (!fits(name, value)) return

    editor.settings[name] = value

    if (value === factoryDefault(name)) delete editor.userDefaults[name]
    else editor.userDefaults[name] = value

    if (connected) sendSettingDefault(name, value)
}

// Back to the default (the user's own or the factory's): one setting, or all of them
export function resetSettings(which: SettingKey | 'all'): void {
    for (const key of which === 'all' ? settingKeys : [which]) (editor.settings as Record<SettingKey, unknown>)[key] = defaultOf(key)

    if (!connected) return

    sendResetSettings(which === 'all' ? { all: true } : { name: which })
    void refreshFromPlugin()
}

// Names probe (test build): Param 1's name with or without the plugin's name
export function toggleNamesProbe(): void {
    if (!connected) return

    sendNamesProbe()
    void refreshFromPlugin()
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
