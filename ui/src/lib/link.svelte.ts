import { onLink, sendBayBypass, sendMeasureLag, sendResetLinkCounters, sendStartProbe } from './bridge'
import { asRecord, count, finiteOrNull, oneOf, text } from './read'

const relayStates = ['none', 'linked', 'playing', 'returning'] as const
const dropoutModes = ['safe', 'silence'] as const
const autoStates = ['off', 'checking', 'oneBlock', 'fellBack'] as const
const lagStates = ['notMeasured', 'measuring', 'found', 'noReturn', 'noMatch'] as const
const probeStates = ['notRun', 'running', 'found', 'noReturn', 'noChange'] as const
export const bypassModes = ['off', 'synced', 'dropLatency'] as const
const dropSendsModes = ['silence', 'audio'] as const
const readings = ['none', 'measured', 'unreadable', 'outOfRange', 'noInput'] as const

export type AutoState = (typeof autoStates)[number]
export type BypassMode = (typeof bypassModes)[number]
export type Reading = (typeof readings)[number]

// A delay at which the panel's test burst comes back, and how strongly (1: as loud as sent)
export interface LagPath {
    samples: number
    gain: number
}

// How late the bay returns what it's sent; several paths through the bay show as several delays, the strongest first
export interface Lag {
    state: (typeof lagStates)[number]
    paths: LagPath[]
    exact: boolean
    earliest: number
    latest: number
}

// How long a step on control 1 took to show in the return (samples from the step to halfway, and 10% to 90%)
export interface Probe {
    state: (typeof probeStates)[number]
    delay: number
    rise: number
    before: number
    after: number
}

// The external module bay's link, as the plugin reports it about 4 times a second
export interface LinkStatus {
    hubProblem: string
    name: string
    linkProblem: string
    receiver: (typeof relayStates)[number]
    transmitter: (typeof relayStates)[number]
    // 0: automatic
    roundTripSetting: number
    autoState: AutoState
    roundTrip: number
    roundTripBlocks: number
    maxBlock: number
    sampleRate: number
    latency: number
    bypass: BypassMode
    // What a bay bypassed with its latency dropped is sent
    dropSends: (typeof dropSendsModes)[number]
    // Samples the output plays behind the engine: round trip + bay latency, or 0 with latency dropped
    delay: number
    // The bay plugin's own latency in use, and the transmitter's latest reading of it (from the position marker)
    bayLatency: number
    reading: Reading
    readingSamples: number
    readingLowest: number | null
    readingHighest: number | null
    // A new bay latency waiting to hold for settleMs, and how long it has so far
    settlingTo: number | null
    settledMs: number
    settleMs: number
    heldBack: number
    dropouts: (typeof dropoutModes)[number]
    crossfadeMs: number
    missing: number
    receiverMisses: number
    replaced: number
    oversized: number
    valueSets: number
    headroomNow: number | null
    headroomLowest: number | null
    blockMin: number | null
    blockMax: number | null
    differenceNow: number | null
    differenceLargest: number | null
    lag: Lag
    probe: Probe
}

// Unknown (null) until the plugin says
export const link = $state({ status: null as LinkStatus | null })

function readLag(raw: unknown): Lag {
    const fields = asRecord(raw) ?? {}
    const paths = Array.isArray(fields.paths) ? fields.paths.slice(0, 8) : []

    return {
        state: oneOf(lagStates, fields.state) ?? 'notMeasured',
        paths: paths.flatMap((entry) => {
            const path = asRecord(entry)
            const samples = finiteOrNull(path?.samples)
            const gain = finiteOrNull(path?.gain)

            return samples === null || gain === null ? [] : [{ samples, gain }]
        }),
        exact: fields.exact === true,
        earliest: count(fields.earliest),
        latest: count(fields.latest),
    }
}

function readProbe(raw: unknown): Probe {
    const fields = asRecord(raw) ?? {}

    return {
        state: oneOf(probeStates, fields.state) ?? 'notRun',
        delay: finiteOrNull(fields.delay) ?? 0,
        rise: finiteOrNull(fields.rise) ?? 0,
        before: finiteOrNull(fields.before) ?? 0,
        after: finiteOrNull(fields.after) ?? 0,
    }
}

export function readLink(raw: unknown): LinkStatus | null {
    const fields = asRecord(raw)

    if (fields === null) return null

    return {
        hubProblem: text(fields.hubProblem),
        name: text(fields.name, 40),
        linkProblem: text(fields.linkProblem),
        receiver: oneOf(relayStates, fields.receiver) ?? 'none',
        transmitter: oneOf(relayStates, fields.transmitter) ?? 'none',
        roundTripSetting: count(fields.roundTripSetting),
        autoState: oneOf(autoStates, fields.autoState) ?? 'off',
        roundTrip: count(fields.roundTrip),
        roundTripBlocks: count(fields.roundTripBlocks),
        maxBlock: count(fields.maxBlock),
        sampleRate: count(fields.sampleRate),
        latency: count(fields.latency),
        bypass: oneOf(bypassModes, fields.bypass) ?? 'off',
        dropSends: oneOf(dropSendsModes, fields.dropSends) ?? 'silence',
        delay: count(fields.delay),
        bayLatency: count(fields.bayLatency),
        reading: oneOf(readings, fields.reading) ?? 'none',
        readingSamples: finiteOrNull(fields.readingSamples) ?? 0,
        readingLowest: finiteOrNull(fields.readingLowest),
        readingHighest: finiteOrNull(fields.readingHighest),
        settlingTo: finiteOrNull(fields.settlingTo),
        settledMs: count(fields.settledMs),
        settleMs: count(fields.settleMs),
        heldBack: count(fields.heldBack),
        dropouts: oneOf(dropoutModes, fields.dropouts) ?? 'safe',
        crossfadeMs: count(fields.crossfadeMs),
        missing: count(fields.missing),
        receiverMisses: count(fields.receiverMisses),
        replaced: count(fields.replaced),
        oversized: count(fields.oversized),
        valueSets: count(fields.valueSets),
        headroomNow: finiteOrNull(fields.headroomNow),
        headroomLowest: finiteOrNull(fields.headroomLowest),
        blockMin: finiteOrNull(fields.blockMin),
        blockMax: finiteOrNull(fields.blockMax),
        differenceNow: finiteOrNull(fields.differenceNow),
        differenceLargest: finiteOrNull(fields.differenceLargest),
        lag: readLag(fields.lag),
        probe: readProbe(fields.probe),
    }
}

export function listenForLink(): void {
    onLink((message) => {
        const status = readLink(message)

        if (status !== null) link.status = status
    })
}

// Only offered once the plugin has reported the link, so the bridge is known to be there
export function setBayBypass(bypass: BypassMode): void {
    if (link.status === null) return

    link.status.bypass = bypass
    sendBayBypass(bypass)
}

export function measureLag(): void {
    if (link.status === null) return

    link.status.lag.state = 'measuring'
    sendMeasureLag()
}

export function startProbe(): void {
    if (link.status === null) return

    link.status.probe.state = 'running'
    sendStartProbe()
}

export function resetLinkCounters(): void {
    if (link.status !== null) sendResetLinkCounters()
}
