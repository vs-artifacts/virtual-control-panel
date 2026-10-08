import { onLink, sendBayBypass, sendMeasureLag, sendResetLinkCounters, sendStartProbe } from './bridge'
import { asRecord, count, finiteOrNull, oneOf, text } from './read'

const relayStates = ['none', 'linked', 'playing', 'returning'] as const
const dropoutModes = ['safe', 'silence'] as const
const autoStates = ['off', 'checking', 'oneBlock', 'fellBack'] as const
const lagStates = ['notMeasured', 'measuring', 'found', 'noReturn', 'noMatch'] as const
const probeStates = ['notRun', 'running', 'found', 'noReturn', 'noChange'] as const
export const slotStates = ['active', 'mute', 'bypass', 'off'] as const
const readings = ['none', 'measured', 'unreadable', 'outOfRange', 'noInput', 'held'] as const
const transportStates = ['unknown', 'playing', 'stopped'] as const

export type AutoState = (typeof autoStates)[number]
export type SlotState = (typeof slotStates)[number]
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
    // The block the round trip counts in: the announced one, or (real blocks) grown from the largest seen
    blockUnit: number
    realBlocks: boolean
    surveyingBlocks: boolean
    // The host's transport as it reports it, and whether the panel's output has been quiet for the set time
    transport: (typeof transportStates)[number]
    hostTempo: number
    outputQuiet: boolean
    sampleRate: number
    latency: number
    slotState: SlotState
    // Active, but the bay's sound isn't back yet (since a state was left, or ever): the sent audio plays
    awaitingBay: boolean
    tailCutReleaseMs: number
    // Samples the output plays behind the engine: round trip + bay latency, or 0 when Off
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
    heldStamped: number
    heldByTransmitter: number
    dropouts: (typeof dropoutModes)[number]
    crossfadeMs: number
    missing: number
    missingLate: number
    missingGone: number
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
    // The event log, newest first
    events: string[]
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
        blockUnit: count(fields.blockUnit),
        realBlocks: fields.realBlocks === true,
        surveyingBlocks: fields.surveyingBlocks === true,
        transport: oneOf(transportStates, fields.transport) ?? 'unknown',
        hostTempo: finiteOrNull(fields.hostTempo) ?? 0,
        outputQuiet: fields.outputQuiet === true,
        sampleRate: count(fields.sampleRate),
        latency: count(fields.latency),
        slotState: oneOf(slotStates, fields.slotState) ?? 'active',
        awaitingBay: fields.awaitingBay === true,
        tailCutReleaseMs: count(fields.tailCutReleaseMs),
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
        heldStamped: count(fields.heldStamped),
        heldByTransmitter: count(fields.heldByTransmitter),
        dropouts: oneOf(dropoutModes, fields.dropouts) ?? 'silence',
        crossfadeMs: count(fields.crossfadeMs),
        missing: count(fields.missing),
        missingLate: count(fields.missingLate),
        missingGone: count(fields.missingGone),
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
        events: Array.isArray(fields.events) ? fields.events.slice(0, 40).filter((line): line is string => typeof line === 'string').map((line) => text(line, 200)) : [],
    }
}

export function listenForLink(): void {
    onLink((message) => {
        const status = readLink(message)

        if (status !== null) link.status = status
    })
}

// Only offered once the plugin has reported the link, so the bridge is known to be there
export function setBaySlotState(state: SlotState): void {
    if (link.status === null) return

    link.status.slotState = state
    sendBayBypass(state)
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
