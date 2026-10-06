<script lang="ts">
    import {
        link,
        measureLag,
        resetLinkCounters,
        setBayBypass,
        startProbe,
        type Lag,
        type LinkStatus,
        type Probe,
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

    function describeAuto(status: LinkStatus): string {
        switch (status.autoState) {
            case "off":
                return "fixed in Settings";
            case "checking":
                return "automatic: trying 1 block (waits for returned audio)";
            case "oneBlock":
                return "automatic: 1 block works (still watching: a miss on either side falls back to 2)";
            case "fellBack":
                return "automatic: 1 block missed, so 2. To try 1 again: wire the panel into the receiver, then switch the receiver's mode to Transmitter and back";
        }
    }

    function describeProbe(
        probe: Probe,
        sampleRate: number,
        maxBlock: number,
    ): string {
        switch (probe.state) {
            case "notRun":
                return "not run";
            case "running":
                return "running (output muted)…";
            case "noReturn":
                return "too little audio came back: are both relays linked?";
            case "noChange":
                return `the return's level didn't follow control 1 (before ${probe.before.toFixed(4)}, after ${probe.after.toFixed(4)}): link control 1 to a level knob in the bay`;
            case "found": {
                const blocks =
                    maxBlock > 0 ? (probe.delay / maxBlock).toFixed(2) : "?";
                return `${signed(probe.delay)} samples (${milliseconds(probe.delay, sampleRate)} ms, ${blocks} blocks) after the step; 10–90% in ${probe.rise} samples (${milliseconds(probe.rise, sampleRate)} ms)`;
            }
        }
    }

    function describeLag(lag: Lag): string {
        switch (lag.state) {
            case "notMeasured":
                return "not measured";
            case "measuring":
                return "measuring (output muted)…";
            case "noReturn":
                return "too little audio came back: are both relays linked?";
            case "noMatch":
                return `the burst didn't come back recognisably between −${lag.earliest} and +${lag.latest} samples`;
            case "found": {
                if (lag.exact && lag.paths.length === 1)
                    return `${signed(lag.paths[0].samples)} samples (an exact copy)`;

                const paths = lag.paths
                    .map(
                        (path) =>
                            `${signed(path.samples)} samples (gain ${path.gain.toFixed(2)})`,
                    )
                    .join("; ");

                return lag.paths.length > 1
                    ? `${lag.paths.length} paths: ${paths}`
                    : paths;
            }
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

            <dt></dt>
            <dd id="link-auto">{describeAuto(status)}</dd>

            <dt>Bypass</dt>
            <dd class="choices">
                <label>
                    <input
                        id="link-bypass"
                        type="checkbox"
                        checked={status.bypass}
                        onchange={(event) =>
                            setBayBypass(event.currentTarget.checked)}
                    />
                    Play the sent audio instead of the return (crossfade {status.crossfadeMs}
                    ms)
                </label>
            </dd>

            <dt>Missing blocks</dt>
            <dd id="link-missing">
                {status.missing} (dropouts play {status.dropouts === "safe"
                    ? "the sent audio"
                    : "silence"})
            </dd>

            <dt>Receiver missed</dt>
            <dd id="link-receiver-missed">
                {status.receiverMisses} blocks (each leaves a gap in the return)
            </dd>

            <dt>Controls</dt>
            <dd>
                {status.valueSets} value sets sent (sent when a value changes)
            </dd>

            <dt>Headroom</dt>
            <dd>
                now {orDash(status.headroomNow)}, lowest {orDash(
                    status.headroomLowest,
                )} samples
            </dd>

            <dt>Block sizes</dt>
            <dd>
                {orDash(status.blockMin)} to {orDash(status.blockMax)} samples (smallest
                and largest since the counters were reset)
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
                <button
                    type="button"
                    onclick={measureLag}
                    title="Sends a short noise burst through the bay instead of the panel's sound (output muted) and finds every delay at which it comes back. Nothing needs to be playing."
                    >Measure</button
                >
            </dd>

            <dt>Control timing</dt>
            <dd class="choices">
                <span id="link-probe"
                    >{describeProbe(
                        status.probe,
                        status.sampleRate,
                        status.maxBlock,
                    )}</span
                >
                <button
                    type="button"
                    onclick={startProbe}
                    title="Sends a steady tone instead of the panel's sound, holds control 1 at 0, steps it to 100%, and times the change in the return. Control 1 must drive a level knob in the bay."
                    >Probe</button
                >
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
{:else}
    <p class="waiting">No bay link status from the plugin</p>
{/if}

<style>
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

    .choices button {
        flex-shrink: 0;
    }

    input {
        margin: 0;
    }

    .warning {
        margin: 0 0 8px;
        color: #ffb36b;
    }

    .waiting {
        margin: 0;
        opacity: 0.7;
    }
</style>
