import { onLink, sendBayOutput, sendMeasureLag, sendResetLinkCounters } from './bridge'
import { asRecord, count, finiteOrNull, oneOf, text } from './read'

const relayStates = ['none', 'linked', 'playing', 'returning'] as const
const outputs = ['direct', 'return'] as const
const missingModes = ['silence', 'bypass'] as const
const lagStates = ['notMeasured', 'measuring', 'silent', 'found', 'noMatch'] as const

export type Output = (typeof outputs)[number]

// How far the return is from the audio it was sent with; positive samples: the return is late
export interface Lag {
    state: (typeof lagStates)[number]
    samples: number
    exact: boolean
    similarity: number
    earliest: number
    latest: number
    // Another delay fits about as well (a held note repeats), so `samples` is only a guess
    ambiguous: boolean
    alternative: number
}

// The external module bay's link, as the plugin reports it about 4 times a second
export interface LinkStatus {
    hubProblem: string
    name: string
    linkProblem: string
    receiver: (typeof relayStates)[number]
    transmitter: (typeof relayStates)[number]
    roundTrip: number
    roundTripBlocks: number
    maxBlock: number
    sampleRate: number
    latency: number
    output: Output
    missingBlocks: (typeof missingModes)[number]
    missing: number
    replaced: number
    oversized: number
    headroomNow: number | null
    headroomLowest: number | null
    blockMin: number | null
    blockMax: number | null
    differenceNow: number | null
    differenceLargest: number | null
    lag: Lag
}

// Unknown (null) until the plugin says
export const link = $state({ status: null as LinkStatus | null })

function readLag(raw: unknown): Lag {
    const fields = asRecord(raw) ?? {}

    return {
        state: oneOf(lagStates, fields.state) ?? 'notMeasured',
        samples: finiteOrNull(fields.samples) ?? 0,
        exact: fields.exact === true,
        similarity: finiteOrNull(fields.similarity) ?? 0,
        earliest: count(fields.earliest),
        latest: count(fields.latest),
        ambiguous: fields.ambiguous === true,
        alternative: finiteOrNull(fields.alternative) ?? 0,
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
        roundTrip: count(fields.roundTrip),
        roundTripBlocks: count(fields.roundTripBlocks),
        maxBlock: count(fields.maxBlock),
        sampleRate: count(fields.sampleRate),
        latency: count(fields.latency),
        output: oneOf(outputs, fields.output) ?? 'direct',
        missingBlocks: oneOf(missingModes, fields.missingBlocks) ?? 'silence',
        missing: count(fields.missing),
        replaced: count(fields.replaced),
        oversized: count(fields.oversized),
        headroomNow: finiteOrNull(fields.headroomNow),
        headroomLowest: finiteOrNull(fields.headroomLowest),
        blockMin: finiteOrNull(fields.blockMin),
        blockMax: finiteOrNull(fields.blockMax),
        differenceNow: finiteOrNull(fields.differenceNow),
        differenceLargest: finiteOrNull(fields.differenceLargest),
        lag: readLag(fields.lag),
    }
}

export function listenForLink(): void {
    onLink((message) => {
        const status = readLink(message)

        if (status !== null) link.status = status
    })
}

// Only offered once the plugin has reported the link, so the bridge is known to be there
export function setBayOutput(output: Output): void {
    if (link.status === null) return

    link.status.output = output
    sendBayOutput(output)
}

export function measureLag(): void {
    if (link.status === null) return

    link.status.lag.state = 'measuring'
    sendMeasureLag()
}

export function resetLinkCounters(): void {
    if (link.status !== null) sendResetLinkCounters()
}
