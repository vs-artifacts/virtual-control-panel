import { changeSetting, editor, flipSettingsTab, isChord } from './editor.svelte'
import { forceHint, releaseHint } from './hint.svelte'
import { isTextEntry, readForwardedKeys } from './keys'

// Keyboard shortcuts, by the keys' characters on the user's layout. Stored like settings (see isChord); a "~" marks one
// that another overwrote: it gives way while that one keeps the same keys.
export type ShortcutKey = 'shortcutFlipSettings'

export interface Shortcut {
    key: ShortcutKey
    label: string
    run: () => void
}

export const shortcuts: Shortcut[] = [{ key: 'shortcutFlipSettings', label: 'Flip between Settings and the last other tab', run: flipSettingsTab }]

export function withoutMark(chord: string): string {
    return chord.length > 1 && chord.startsWith('~') ? chord.slice(1) : chord
}

const isMarked = (chord: string) => withoutMark(chord) !== chord

// Each physical key's own (unshifted) character on the current layout, where the browser can say (not on macOS)
let layout: Map<string, string> | null = null

interface KeyboardWithLayout {
    getLayoutMap(): Promise<Iterable<[string, string]>>
}

async function readLayout(): Promise<void> {
    const keyboard = (navigator as unknown as { keyboard?: Partial<KeyboardWithLayout> }).keyboard

    if (typeof keyboard?.getLayoutMap !== 'function') return

    try {
        layout = new Map(await keyboard.getLayoutMap())
    } catch {
        // Typed characters are used instead
    }
}

const modifierKeys = new Set(['Control', 'Alt', 'AltGraph', 'Shift', 'Meta', 'OS', 'CapsLock', 'NumLock', 'ScrollLock', 'Fn', 'FnLock', 'Hyper', 'Super'])

// The key's position on a US keyboard, for layouts without Latin letters
function usCharacter(code: string): string | null {
    const match = /^(?:Key([A-Z])|Digit([0-9]))$/.exec(code)
    return match === null ? null : (match[1] ?? match[2])
}

function isNonLatinLetter(character: string): boolean {
    return /\p{L}/u.test(character) && !/\p{Script=Latin}/u.test(character)
}

// The key's name in a chord: its own character on the layout (uppercase), or a name like F5, Space, Plus
function keyName(event: KeyboardEvent): string | null {
    const own = layout?.get(event.code)
    let key = own !== undefined && Array.from(own).length === 1 ? own : event.key

    if (key === 'Dead') key = usCharacter(event.code) ?? ''
    if (key === ' ') return 'Space'
    if (key === '+') return 'Plus'

    if (Array.from(key).length === 1) {
        if (isNonLatinLetter(key)) key = usCharacter(event.code) ?? key

        const upper = key.toUpperCase()
        return Array.from(upper).length === 1 ? upper : key
    }

    return /^[A-Z][A-Za-z0-9]{1,15}$/.test(key) && key !== 'Unidentified' && key !== 'Process' ? key : null
}

export function chordOf(event: KeyboardEvent): string | null {
    if (modifierKeys.has(event.key)) return null

    const name = keyName(event)

    if (name === null) return null

    const chord = `${event.ctrlKey ? 'Ctrl+' : ''}${event.altKey ? 'Alt+' : ''}${event.shiftKey ? 'Shift+' : ''}${name}`
    return isChord(chord) ? chord : null
}

// Whether the host gets these keys from the page (FL's typing keyboard): by physical key, without Alt or Shift
function passedToHost(chord: string): boolean {
    const parts = chord.split('+')
    const name = parts.pop() ?? ''
    const modifiers = new Set(parts)

    if (name === '' || modifiers.has('Alt') || modifiers.has('Shift')) return false

    const codes = new Set<string>()

    if (name === 'Space') codes.add('Space')

    for (const [code, character] of layout ?? []) {
        const shown = isNonLatinLetter(character) ? usCharacter(code) : character.toUpperCase()

        if (shown === name) codes.add(code)
    }

    if (layout === null) {
        if (/^[A-Z]$/.test(name)) codes.add(`Key${name}`)
        if (/^[0-9]$/.test(name)) codes.add(`Digit${name}`)

        const punctuation: Record<string, string> = { ',': 'Comma', '.': 'Period', '/': 'Slash', ';': 'Semicolon', "'": 'Quote', '[': 'BracketLeft', ']': 'BracketRight' }

        if (punctuation[name] !== undefined) codes.add(punctuation[name])
    }

    const forwarded = new Set(readForwardedKeys())
    return [...codes].some((code) => forwarded.has(modifiers.has('Ctrl') ? `Ctrl+${code}` : code))
}

export interface ShortcutWarning {
    text: string
    // Overwritten: another shortcut has the same keys and wins
    overwritten: boolean
}

// What's wrong with these keys for this shortcut, if anything: the stored value, or keys waiting to be applied
export function warningFor(key: ShortcutKey, chord: string, pending: boolean): ShortcutWarning | null {
    const keys = withoutMark(chord)

    if (keys === '') return null

    const rivals = shortcuts.filter((other) => other.key !== key && withoutMark(editor.settings[other.key]) === keys)
    const lines: string[] = []
    let overwritten = false

    if (rivals.length > 0) {
        const names = rivals.map((other) => `"${other.label}"`).join(', ')

        if (pending) lines.push(`Conflicts with ${names}: applying takes the keys from it.`)
        else if (isMarked(chord) && rivals.some((other) => !isMarked(editor.settings[other.key]))) {
            overwritten = true
            lines.push(`Overwritten by ${names}: these keys run that instead.`)
        } else lines.push(`Conflicts with ${names}, which gives way to this one.`)
    }

    if (passedToHost(keys)) lines.push(pending ? 'Is passed on to the host (FL\'s typing keyboard): applying keeps it for the panel.' : 'Is passed on to the host (FL\'s typing keyboard); the panel keeps it.')

    return lines.length === 0 ? null : { text: lines.join('\n'), overwritten }
}

// Recording: the shortcut whose box listens, the keys pressed (null: none yet), and whether a warning waits for its
// confirmation (the second click on ">")
export const recording = $state({ key: null as ShortcutKey | null, pending: null as string | null, armed: false })

export function startRecording(key: ShortcutKey): void {
    releaseHint()
    recording.key = key
    recording.pending = null
    recording.armed = false
}

export function cancelRecording(): void {
    recording.key = null
    recording.pending = null
    recording.armed = false
    releaseHint()
}

// The ">" button: applies the keys pressed; with a warning, the first click shows it and the second applies
export function applyRecording(key: ShortcutKey): void {
    const chord = recording.pending

    if (recording.key !== key || chord === null) return

    const warning = warningFor(key, chord, true)

    if (warning !== null && !recording.armed) {
        recording.armed = true
        forceHint(`${warning.text}\nClick > again to apply.`)
        return
    }

    const others = shortcuts.filter((other) => other.key !== key)

    for (const other of others) if (chord !== '' && withoutMark(editor.settings[other.key]) === chord) changeSetting(other.key, `~${chord}`)

    changeSetting(key, chord)
    cancelRecording()
}

function record(event: KeyboardEvent): void {
    if (event.key === 'Escape') return cancelRecording()

    if (event.key === 'Backspace' || event.key === 'Delete') recording.pending = ''
    else {
        const chord = chordOf(event)

        if (chord === null) return

        recording.pending = chord
    }

    recording.armed = false
    releaseHint()
}

// Keys that ran a shortcut: their release doesn't reach the host either
const ran = new Set<string>()

function onKeyDown(event: KeyboardEvent): void {
    if (recording.key !== null) {
        event.preventDefault()
        event.stopImmediatePropagation()

        if (!event.repeat) record(event)

        return
    }

    const chord = chordOf(event)

    if (chord === null) return

    // In a text field, plain keys type
    if (isTextEntry(event.target) && !event.ctrlKey && !event.altKey) return

    const matching = shortcuts.filter((shortcut) => withoutMark(editor.settings[shortcut.key]) === chord)
    const shortcut = matching.find((entry) => !isMarked(editor.settings[entry.key])) ?? matching[0]

    if (shortcut === undefined) return

    event.preventDefault()
    event.stopImmediatePropagation()
    ran.add(event.code)

    if (!event.repeat) shortcut.run()
}

function onKeyUp(event: KeyboardEvent): void {
    if (recording.key !== null || ran.delete(event.code)) {
        event.preventDefault()
        event.stopImmediatePropagation()
    }
}

// A click anywhere but the recording shortcut's box and ">" puts its keys back
function onPointerDown(event: PointerEvent): void {
    if (recording.key === null) return

    const control = event.target instanceof Element ? event.target.closest('[data-shortcut]') : null

    if (control === null || control.getAttribute('data-shortcut') !== recording.key) cancelRecording()
}
// Once, before the keys passed to the host are watched, so a shortcut takes its keys first
export function followShortcuts(): void {
    void readLayout()
    window.addEventListener('focus', () => void readLayout())
    window.addEventListener('keydown', onKeyDown, { capture: true })
    window.addEventListener('keyup', onKeyUp, { capture: true })
    window.addEventListener('pointerdown', onPointerDown, { capture: true })
    window.addEventListener('blur', () => {
        if (recording.key !== null) cancelRecording()
    })
}
