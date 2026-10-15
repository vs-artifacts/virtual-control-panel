import factoryDefaults from '../../../source/SettingsDefaults.json'
import { fetchEditorState, isResetKind, onProcessing, onSettings, sendAddBus, sendAddSlot, sendCollapse, sendDiagnostics, sendGeneratorBus, sendMoveSlot, sendOpenPlugin, sendRemoveBus, sendRemoveSlot, sendResetSettings, sendResetSize, sendScroll, sendSetting, sendSettingDefault, sendSlotState, sendSwapSize, sendTab, type ResetKind, type SlotStateName } from './bridge'
import { link, listenForLink, readLink } from './link.svelte'
import { asRecord, oneOf } from './read'

const slotStateNames = ['on', 'mute', 'bypass', 'off'] as const

export const tabNames = ['Panel', 'Settings'] as const
const settingsTab = tabNames.indexOf('Settings')
let lastOtherTab = 0

// The key the plugin reads for "around the window's centre" (corner grips, Swap): see PluginEditor::wantsCenterAnchor
export const centreModifier = 'Alt'

// The page's scrolling areas, as the plugin saves them: the Panel tab (down, and its generator and bus rows sideways), the
// Settings tab, and Diagnostics
export const scrollAreas = ['panel', 'panel.generators', 'panel.buses', 'settings', 'diagnostics'] as const
export const sidewaysScrollAreas: readonly string[] = ['panel.generators', 'panel.buses']
const maxScroll = 1000000

// The sections that collapse to their bar: the Panel tab's, then the Settings tab's
export const panelSections = ['header', 'generators', 'buses'] as const
export type PanelSection = (typeof panelSections)[number]
export const settingsSections = ['settings.general', 'settings.window', 'settings.slotText', 'settings.bays', 'settings.slotChanges', 'settings.shortcuts'] as const
export type SettingsSection = (typeof settingsSections)[number]
const sections = [...panelSections, ...settingsSections] as const
export type Section = (typeof sections)[number]

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
    // Slot text (p2, p3): pixel fonts with anti-aliasing, or Barlow for everything; snapped to whole screen pixels; the
    // next whole pixel size from this far past each scale step; anti-aliased steps as a share of a screen pixel
    pixelTextAntialiasing: boolean
    fontFallback: boolean
    pixelTextSnap: boolean
    pixelStepAtPercent: number
    pixelStepAntialiasedPercent: number
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
    bayRoundTripFadeMs: number
    bayLatencyFadeOutMs: number
    bayLatencyFadeInMs: number
    muteFadeMs: number
    bypassFadeMs: number
    bayOffFadeMs: number
    bayDropoutFadeOutMs: number
    bayDropoutFadeInMs: number
    bayLatencySettleMs: number
    bayTailCutReleaseMs: number
    slotChangeFadeOutMs: number
    slotChangeFadeInMs: number
    doubleClickMs: number
    // Placeholder: double-click will open a value entry instead of resetting
    doubleClickSets: boolean
    // Long hints scroll by themselves: after a delay (smart: by the window's width), at lines per second, then wait at
    // the bottom and start again
    hintAutoScroll: boolean
    hintSmartDelay: boolean
    hintScrollDelaySeconds: number
    hintScrollSpeed: number
    hintScrollEndSeconds: number
    // How a new instance's Settings sections open
    settingsOpen: 'last' | 'expanded' | 'collapsed'
    // Keyboard shortcuts (see shortcuts.svelte.ts)
    shortcutFlipSettings: string
}

// Every setting's factory default and limits, from the file the plugin is built from (source/SettingsDefaults.json)
type Definition =
    | { type: 'flag'; default: boolean }
    | { type: 'whole' | 'decimal'; default: number; min: number; max: number; unit: string }
    | { type: 'choice'; default: string; choices: string[] }
    | { type: 'chord'; default: string }

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
    { key: 'bayRoundTripFadeMs', max: rangeOf('bayRoundTripFadeMs').max, label: 'Round trip change', covers: 'the timing jump when the round trip changes (tempo, Automatic, Settings)' },
    { key: 'bayLatencyFadeOutMs', max: rangeOf('bayLatencyFadeOutMs').max, label: 'Bay latency change: fade out', covers: "into the bay's sound held back for a latency change (e.g. linear phase)" },
    { key: 'bayLatencyFadeInMs', max: rangeOf('bayLatencyFadeInMs').max, label: 'Bay latency change: fade in', covers: "the bay's sound coming back after a latency change, and the timing jump to the new latency" },
    { key: 'bayOffFadeMs', max: rangeOf('bayOffFadeMs').max, label: 'Off', covers: "the timing jump, the bay's send and the fade back in, entering and leaving Off" },
    { key: 'bayDropoutFadeOutMs', max: rangeOf('bayDropoutFadeOutMs').max, label: 'Dropouts: fade out', covers: 'into a missing return' },
    { key: 'bayDropoutFadeInMs', max: rangeOf('bayDropoutFadeInMs').max, label: 'Dropouts: fade in', covers: "the return coming back after a dropout, and into the bay's first sound" },
] as const

// The dip around a change of the panel's structure (whole milliseconds, 0 is off)
export const slotChangeFadeSettings = [
    { key: 'slotChangeFadeOutMs', max: rangeOf('slotChangeFadeOutMs').max, label: 'Fade out', covers: 'the panel\'s sound, before a slot, bus or generator route is added, removed or moved' },
    { key: 'slotChangeFadeInMs', max: rangeOf('slotChangeFadeInMs').max, label: 'Fade in', covers: 'the panel\'s sound, once the change is made' },
] as const

// MUTE and BYPASS, the same for slots and bays (whole milliseconds, 0 is off)
export const stateFadeSettings = [
    { key: 'muteFadeMs', max: rangeOf('muteFadeMs').max, label: 'MUTE', covers: "between the dry sound and the plugin's (or the bay's), entering and leaving MUTE" },
    { key: 'bypassFadeMs', max: rangeOf('bypassFadeMs').max, label: 'BYPASS', covers: "between the dry sound and the plugin's (or the bay's, and the bay's send), entering and leaving BYPASS" },
] as const

export type FadeKey = (typeof fadeSettings)[number]['key'] | (typeof slotChangeFadeSettings)[number]['key'] | (typeof stateFadeSettings)[number]['key']

// How long a new bay latency must hold before it's used: whole milliseconds
export const settleRange = rangeOf('bayLatencySettleMs')

// How much longer a bay's sound is held back after it leaves Bypass or Off: whole milliseconds
export const releaseRange = rangeOf('bayTailCutReleaseMs')

// The longest gap between the clicks of a double-click, in whole milliseconds
export const doubleClickRange = rangeOf('doubleClickMs')

// Real blocks start at one tick at this tempo at least: BPM, 0 for none
export const lowestTempoRange = rangeOf('bayLowestTempo')

// Pixel text: where the next whole pixel size starts, and the anti-aliased step (percent)
export const pixelStepAtRange = rangeOf('pixelStepAtPercent')
export const pixelStepAntialiasedRange = rangeOf('pixelStepAntialiasedPercent')

// Hint scrolling: seconds, and lines per second (tenths)
export const hintDelayRange = rangeOf('hintScrollDelaySeconds')
export const hintSpeedRange = rangeOf('hintScrollSpeed')
export const hintEndRange = rangeOf('hintScrollEndSeconds')

// A keyboard shortcut as stored (as PluginSettings::isChord): optional "~" (overwritten by another), then Ctrl+, Alt+,
// Shift+ in that order, then one printable character or a key's name ("F5"); empty for none
export function isChord(text: string): boolean {
    if (text === '') return true
    if (Array.from(text).length > 40) return false

    let rest = text.length > 1 && text.startsWith('~') ? text.slice(1) : text

    for (const modifier of ['Ctrl+', 'Alt+', 'Shift+']) if (rest.startsWith(modifier) && rest.length > modifier.length) rest = rest.slice(modifier.length)

    const characters = Array.from(rest)

    if (characters.length === 1) {
        const c = characters[0].codePointAt(0) ?? 0
        const invisible = (c >= 0x7f && c < 0xa0) || (c >= 0x200b && c <= 0x200f) || (c >= 0x202a && c <= 0x202e) || (c >= 0x2060 && c <= 0x206f) || c === 0xfeff || c >= 0xfff0
        return c > 0x20 && c !== 0x2b && !invisible && !/\s/u.test(characters[0])
    }

    return /^[A-Z][A-Za-z0-9]{0,15}$/.test(rest)
}

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
        case 'chord':
            return typeof value === 'string' && isChord(value)
    }
}

export function factoryDefault<Name extends SettingKey>(key: Name): Settings[Name] {
    return definitions[key].default as Settings[Name]
}

// The user's own default where there is one (set from a setting's right-click menu), else the factory's
export function defaultOf<Name extends SettingKey>(key: Name): Settings[Name] {
    return (editor.userDefaults[key] ?? factoryDefault(key)) as Settings[Name]
}

// A generator or a bus's slot. Plugin: its plugin list key (empty for an empty slot).
export interface SlotInfo {
    id: number
    label: string
    plugin: string
    name: string
    status: string
    canOpen: boolean
    // A plugin's states (see SlotState in the plugin); an empty slot has none
    hasStates: boolean
    state: SlotStateName
    mix: number
    endsPath: boolean
    // An earlier slot's end of the signal path cuts this one off
    cutOff: boolean
}

export interface GeneratorInfo extends SlotInfo {
    buses: number[]
}

// parent: a sub-bus's parent bus (0 for none)
export interface BusInfo {
    id: number
    label: string
    parent: number
    slots: SlotInfo[]
}

export interface PluginChoice {
    key: string
    name: string
}

export interface StructureInfo {
    generators: GeneratorInfo[]
    buses: BusInfo[]
    master: BusInfo
    menu: { generators: PluginChoice[]; fx: PluginChoice[] }
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
    structure: null as StructureInfo | null,
    collapsed: Object.fromEntries(sections.map((section) => [section, false])) as Record<Section, boolean>,
    // Unknown (null) until the plugin says
    processing: null as boolean | null,
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

function readText(raw: unknown): string | null {
    return typeof raw === 'string' ? raw.slice(0, 200) : null
}

function readId(raw: unknown): number | null {
    return typeof raw === 'number' && Number.isInteger(raw) && raw >= 0 && raw <= 1000000 ? raw : null
}

const maxListed = 256

function readSlot(raw: unknown): SlotInfo | null {
    const fields = asRecord(raw) ?? {}
    const id = readId(fields.id)
    const label = readText(fields.label)
    const plugin = readText(fields.plugin)
    const name = readText(fields.name)
    const status = readText(fields.status)

    if (id === null || label === null || plugin === null || name === null || status === null) return null

    const mix = typeof fields.mix === 'number' && Number.isFinite(fields.mix) ? Math.min(1, Math.max(0, fields.mix)) : 1

    return {
        id,
        label,
        plugin,
        name,
        status,
        canOpen: fields.canOpen === true,
        hasStates: fields.hasStates === true,
        state: oneOf(slotStateNames, fields.state) ?? 'on',
        mix,
        endsPath: fields.endsPath === true,
        cutOff: fields.cutOff === true,
    }
}

function readBus(raw: unknown): BusInfo | null {
    const fields = asRecord(raw) ?? {}
    const id = readId(fields.id)
    const label = readText(fields.label)
    const parent = readId(fields.parent)

    if (id === null || label === null || parent === null || !Array.isArray(fields.slots)) return null

    return { id, label, parent, slots: fields.slots.slice(0, maxListed).map(readSlot).filter((slot) => slot !== null) }
}

function readChoices(raw: unknown): PluginChoice[] {
    if (!Array.isArray(raw)) return []

    return raw.slice(0, maxListed).flatMap((entry) => {
        const fields = asRecord(entry) ?? {}
        const key = readText(fields.key)
        const name = readText(fields.name)
        return key !== null && name !== null ? [{ key, name }] : []
    })
}

// The plugin's description of the generator stage, the buses and the master (see describeStructure in the plugin)
export function readStructure(raw: unknown): StructureInfo | null {
    const fields = asRecord(raw)
    const master = readBus(fields?.master)

    if (fields === null || master === null || !Array.isArray(fields.generators) || !Array.isArray(fields.buses)) return null

    const generators = fields.generators.slice(0, maxListed).flatMap((entry) => {
        const slot = readSlot(entry)
        const buses = asRecord(entry)?.buses

        if (slot === null || !Array.isArray(buses)) return []

        return [{ ...slot, buses: buses.slice(0, maxListed).map(readId).filter((bus) => bus !== null) }]
    })

    const menu = asRecord(fields.menu) ?? {}

    return {
        generators,
        buses: fields.buses.slice(0, maxListed).map(readBus).filter((bus) => bus !== null),
        master,
        menu: { generators: readChoices(menu.generators), fx: readChoices(menu.fx) },
    }
}

function readCollapsed(raw: unknown): void {
    const names = Array.isArray(raw) ? raw : []

    for (const section of sections) editor.collapsed[section] = names.includes(section)
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

export async function loadEditorState(): Promise<void> {
    onProcessing((message) => {
        const running = asRecord(message)?.running

        if (typeof running === 'boolean') editor.processing = running
    })

    listenForLink()

    onSettings((message) => {
        const fields = asRecord(message)

        if (fields === null) return

        readSettings(fields.settings, editor.settings)
        readUserDefaults(fields.userDefaults)
    })

    try {
        const state = asRecord(await fetchEditorState())

        if (state === null) return

        connected = true

        if (typeof state.title === 'string') editor.title = state.title
        if (typeof state.tab === 'number' && Number.isInteger(state.tab) && state.tab >= 0 && state.tab < tabNames.length) editor.tab = state.tab
        if (editor.tab !== settingsTab) lastOtherTab = editor.tab
        if (typeof state.diagnostics === 'boolean') editor.diagnostics = state.diagnostics
        if (typeof state.processing === 'boolean') editor.processing = state.processing

        readScroll(state.scroll)
        readCollapsed(state.collapsed)
        readSettings(state.settings, editor.settings)
        readUserDefaults(state.userDefaults)
        editor.structure = readStructure(state.structure)
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
    } catch {
        // The page keeps what it showed
    }
}

export function showTab(index: number): void {
    if (index !== settingsTab) lastOtherTab = index

    editor.tab = index

    if (connected) sendTab(index)
}

// To Settings, or back to the tab shown before it
export function flipSettingsTab(): void {
    showTab(editor.tab === settingsTab ? lastOtherTab : settingsTab)
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

// Back to the default (the user's own or the factory's) for one setting. For all of them (see ResetKind): to their
// defaults, to the factory's (the user's own kept), forgetting the user's own (values kept), or everything to the factory's.
export function resetSettings(which: SettingKey | ResetKind): void {
    const kind = isResetKind(which) ? which : null
    const settings = editor.settings as Record<SettingKey, unknown>

    if (kind === 'clearUserDefaults' || kind === 'hard') editor.userDefaults = {}

    if (kind !== 'clearUserDefaults')
        for (const key of kind === null ? [which as SettingKey] : settingKeys) settings[key] = kind === 'factory' || kind === 'hard' ? factoryDefault(key) : defaultOf(key)

    if (!connected) return

    sendResetSettings(kind === null ? { name: which } : { all: kind })
    void refreshFromPlugin()
}

export function resetSize(): void {
    if (connected) sendResetSize()
}

// The main area's width and height swap (Diagnostics stays out of it); the plugin reads the modifiers for the anchor
export function swapSize(): void {
    if (connected) sendSwapSize()
}

export function openPlugin(slot: number): void {
    if (connected) sendOpenPlugin(slot)
}

function findSlot(id: number): SlotInfo | undefined {
    const structure = editor.structure

    if (structure === null) return undefined

    return [...structure.generators, ...structure.buses.flatMap((bus) => bus.slots), ...structure.master.slots].find((slot) => slot.id === id)
}

// A slot's state: the selected one again goes back to ON. Off (and back) rewires the plugin's graph, which then sends the
// structure again.
export function setSlotState(slot: number, state: SlotStateName): void {
    const plugin = findSlot(slot)

    if (plugin === undefined || !plugin.hasStates) return

    plugin.state = state

    if (connected) sendSlotState(slot, { state })
}

export function setSlotMix(slot: number, mix: number): void {
    const plugin = findSlot(slot)

    if (plugin === undefined || !plugin.hasStates || !Number.isFinite(mix)) return

    plugin.mix = Math.min(1, Math.max(0, mix))

    if (connected) sendSlotState(slot, { mix: plugin.mix })
}

export function setEndsPath(slot: number, endsPath: boolean): void {
    const plugin = findSlot(slot)

    if (plugin === undefined || !plugin.hasStates) return

    plugin.endsPath = endsPath

    if (connected) sendSlotState(slot, { endsPath })
}

// Structure changes: the plugin answers each with the new structure (see onStructure)

// bus 0: the generator stage
export function addSlot(bus: number, index: number, plugin: string): void {
    if (connected) sendAddSlot(bus, index, plugin)
}

export function removeSlot(slot: number): void {
    if (connected) sendRemoveSlot(slot)
}

// index: where it lands, counted without the slot itself; a generator ignores the bus
export function moveSlot(slot: number, bus: number, index: number): void {
    if (connected) sendMoveSlot(slot, bus, index)
}

// An empty slot gets a plugin: the plugin's slot goes in its place
export function fillSlot(bus: number, slot: number, plugin: string): void {
    const owner = editor.structure?.buses.find((entry) => entry.id === bus) ?? (editor.structure?.master.id === bus ? editor.structure.master : undefined)
    const index = owner?.slots.findIndex((entry) => entry.id === slot) ?? -1

    if (index < 0 || !connected) return

    sendAddSlot(bus, index, plugin)
    sendRemoveSlot(slot)
}

// parent 0: a top-level bus
export function addBus(parent: number): void {
    if (connected) sendAddBus(parent)
}

export function removeBus(bus: number): void {
    if (connected) sendRemoveBus(bus)
}

export function setGeneratorBus(slot: number, bus: number, feeds: boolean): void {
    if (connected) sendGeneratorBus(slot, bus, feeds)
}

export function setCollapsed(section: Section, collapsed: boolean): void {
    editor.collapsed[section] = collapsed

    if (connected) sendCollapse(section, collapsed)
}

export function setAllSettingsCollapsed(collapsed: boolean): void {
    for (const section of settingsSections) if (editor.collapsed[section] !== collapsed) setCollapsed(section, collapsed)
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
