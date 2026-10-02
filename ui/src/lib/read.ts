// Checks for values from the plugin: everything it sends is validated before use

export function asRecord(value: unknown): Record<string, unknown> | null {
    return typeof value === 'object' && value !== null && !Array.isArray(value) ? (value as Record<string, unknown>) : null
}

export function oneOf<T extends string>(options: readonly T[], value: unknown): T | undefined {
    return options.find((option) => option === value)
}

export function finiteOrNull(value: unknown): number | null {
    return typeof value === 'number' && Number.isFinite(value) ? value : null
}

// A count or size: never negative, 0 if missing
export function count(value: unknown): number {
    const number = finiteOrNull(value)

    return number !== null && number >= 0 ? number : 0
}

export function text(value: unknown, maxLength = 200): string {
    return typeof value === 'string' ? value.slice(0, maxLength) : ''
}
