import { defaultOf, definitionOf, editor, factoryDefault, resetSettings, setAsDefault, changeSetting, type SettingKey, type Settings } from './editor.svelte'
import { openPageMenu, type PageMenuItem } from './menu.svelte'

// Short names for the menu (the Settings tab's own lines are longer)
const choiceNames: Partial<Record<SettingKey, Record<string, string>>> = {
    editorPlacement: { off: 'Off', hostPlaced: 'the monitor the host opened it on', mouse: 'the monitor under the mouse', hostMainWindow: "the monitor with the host's main window" },
    attachedPlacement: { off: 'Off', workspace: "FL's workspace", hostMonitor: 'the monitor where FL is open' },
    resizeRendering: { smooth: 'Smooth', sharp: 'Sharp' },
    bayStoppedShrink: { never: 'Never', whenQuiet: 'Only when quiet', always: 'Always' },
}

function describe(key: SettingKey, value: unknown): string {
    if (key === 'bayRoundTrip') return value === 0 ? 'Automatic' : `${value} ${value === 1 ? 'block' : 'blocks'}`
    if (key === 'bayLowestTempo') return value === 0 ? 'none' : `${value} BPM`
    if (key === 'bayRealBlocks') return value ? 'real blocks' : 'announced blocks'

    const definition = definitionOf(key)

    switch (definition.type) {
        case 'flag':
            return value ? 'on' : 'off'
        case 'whole':
        case 'decimal':
            return `${value} ${definition.unit}`
        case 'choice':
            return choiceNames[key]?.[String(value)] ?? String(value)
    }
}

// The setting's right-click menu. value: what Set as default takes (the option right-clicked, the box's state, the
// field's typed value), or null if the field holds nothing usable.
export function openSettingMenu<Name extends SettingKey>(event: MouseEvent, key: Name, value: Settings[Name] | null): void {
    event.preventDefault()
    event.stopPropagation()

    const items: PageMenuItem[] = [
        { text: 'Set as default', enabled: value !== null, action: () => value !== null && setAsDefault(key, value) },
        { text: `Reset to default (${describe(key, defaultOf(key))})`, enabled: true, action: () => resetSettings(key) },
    ]

    if (editor.userDefaults[key] !== undefined) {
        const factory = factoryDefault(key)
        items.push({ text: `Reset to factory default (${describe(key, factory)})`, enabled: true, action: () => changeSetting(key, factory) })
    }

    openPageMenu(items, event.clientX, event.clientY)
}

// What a number field would commit, as its change handler does: whole numbers in range (decimals to a tenth; an empty
// lowest tempo is none); null if it holds no number
export function typedNumber(field: HTMLInputElement | null, key: SettingKey): number | null {
    const definition = definitionOf(key)

    if (field === null || (definition.type !== 'whole' && definition.type !== 'decimal')) return null

    const typed = field.value.trim() === '' && key === 'bayLowestTempo' ? 0 : field.valueAsNumber

    if (!Number.isFinite(typed)) return null

    const rounded = definition.type === 'whole' ? Math.round(typed) : Math.round(typed * 10) / 10
    return Math.min(definition.max, Math.max(definition.min, rounded))
}
