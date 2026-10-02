<script lang="ts">
    import {
        link,
        measureLag,
        resetLinkCounters,
        setBayOutput,
        type Lag,
        type LinkStatus,
    } from "../lib/link.svelte";

    function milliseconds(samples: number, sampleRate: number): string {
        return sampleRate > 0
            ? ((1000 * samples) / sampleRate).toFixed(1)
            : "?";
    }

    function describeLink(status: LinkStatus): string {
        if (status.hubProblem) return `Not linked: ${status.hubProblem}`;
        if (!status.name)
            return `Not linked: ${status.linkProblem || "waiting for the host to start the audio"}`;

        return `Linked as ${status.name}`;
    }

    function describeRelays(status: LinkStatus): string {
        const receiver = {
            none: "no receiver",
            linked: "receiver linked, not playing",
            playing: "receiver playing",
            returning: "receiver playing",
        }[status.receiver];

        const transmitter = {
            none: "no transmitter",
            linked: "transmitter linked, not returning",
            playing: "transmitter returning",
            returning: "transmitter returning",
        }[status.transmitter];

        return `${receiver}; ${transmitter}`;
    }

    function orDash(value: number | null): string {
        return value === null ? "–" : String(value);
    }

    function describeDifference(value: number | null): string {
        if (value === null) return "–";

        return value === 0 ? "identical" : value.toFixed(7);
    }

    function signed(samples: number): string {
        return `${samples > 0 ? "+" : ""}${samples}`;
    }

    // Never rounds a near match up to 100%: only an exact match is that
    function percent(similarity: number): string {
        return `${Math.min(similarity * 100, 99.999).toFixed(3)}%`;
    }

    function describeLag(lag: Lag): string {
        switch (lag.state) {
            case "notMeasured":
                return "not measured";
            case "measuring":
                return "measuring (needs returned audio)…";
            case "silent":
                return "the return was silent: play something, then measure";
            case "noMatch":
                return `no match between −${lag.earliest} and +${lag.latest} samples`;
            case "found":
                if (lag.exact)
                    return `exact match at ${signed(lag.samples)} samples`;

                if (lag.ambiguous)
                    return `unclear: ${signed(lag.samples)} and ${signed(lag.alternative)} samples fit about equally (${percent(lag.similarity)}); the sound repeats, so measure with short, percussive notes`;

                return `closest match at ${signed(lag.samples)} samples (${percent(lag.similarity)} similar)`;
        }
    }
</script>

{#if link.status}
    {@const status = link.status}
    <section class="link">
        <h2>Bay link (test build)</h2>
        <dl>
            <dt>Link</dt>
            <dd id="link-state">{describeLink(status)}</dd>

            <dt>Relays</dt>
            <dd id="link-relays">{describeRelays(status)}</dd>

            <dt>Round trip</dt>
            <dd>
                {status.roundTrip} samples ({milliseconds(
                    status.roundTrip,
                    status.sampleRate,
                )} ms,
                {status.roundTripBlocks} × {status.maxBlock}); host told {status.latency}
            </dd>

            <dt>Output</dt>
            <dd class="choices">
                <label>
                    <input
                        type="radio"
                        name="bayOutput"
                        checked={status.output === "direct"}
                        onchange={() => setBayOutput("direct")}
                    />
                    Direct
                </label>
                <label>
                    <input
                        type="radio"
                        name="bayOutput"
                        checked={status.output === "return"}
                        onchange={() => setBayOutput("return")}
                    />
                    Bay return
                </label>
            </dd>

            <dt>Missing blocks</dt>
            <dd id="link-missing">
                {status.missing} (played as {status.missingBlocks})
            </dd>

            <dt>Headroom</dt>
            <dd>
                now {orDash(status.headroomNow)}, lowest {orDash(
                    status.headroomLowest,
                )} samples
            </dd>

            <dt>Block sizes</dt>
            <dd>
                {orDash(status.blockMin)} to {orDash(status.blockMax)} samples
            </dd>

            <dt>Return vs sent</dt>
            <dd id="link-difference">
                now {describeDifference(status.differenceNow)}, largest {describeDifference(
                    status.differenceLargest,
                )}
            </dd>

            <dt>Lag</dt>
            <dd class="choices">
                <span id="link-lag">{describeLag(status.lag)}</span>
                <button type="button" onclick={measureLag}>Measure</button>
            </dd>
        </dl>

        {#if status.replaced > 0 || status.oversized > 0}
            <p class="warning">
                Silenced: {status.replaced} returned samples that weren't numbers,
                {status.oversized} blocks longer than the host announced
            </p>
        {/if}

        <button type="button" onclick={resetLinkCounters}>Reset counters</button
        >
    </section>
{/if}

<style>
    .link {
        margin-top: 18px;
        padding-top: 10px;
        border-top: 1px solid rgba(255, 255, 255, 0.15);
    }

    h2 {
        margin: 0 0 6px;
        font-size: 16px;
        font-weight: 500;
    }

    dl {
        display: grid;
        grid-template-columns: auto 1fr;
        gap: 3px 12px;
        margin: 0 0 8px;
    }

    dt {
        opacity: 0.7;
    }

    dd {
        margin: 0;
        font-variant-numeric: tabular-nums;
        overflow-wrap: anywhere;
    }

    .choices {
        display: flex;
        align-items: center;
        gap: 14px;
    }

    .choices label {
        display: flex;
        align-items: center;
        gap: 5px;
    }

    input {
        margin: 0;
    }

    .warning {
        margin: 0 0 8px;
        color: #ffb36b;
    }
</style>
