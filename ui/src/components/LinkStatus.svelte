<script lang="ts">
    import { hinted } from "../lib/hint.svelte";
    import {
        link,
        measureLag,
        resetLinkCounters,
        setBaySlotState,
        startProbe,
        type Lag,
        type LinkStatus,
        type Probe,
        type SlotState,
    } from "../lib/link.svelte";

    // A line's text: plain parts are labels (dim), { v } parts are its primary values
    type Part = string | { v: string };
    type Line = { parts: Part[]; hint: string };

    const v = (value: string | number): Part => ({ v: String(value) });

    // Each a toggle that acts like a radio button: pressing the selected one again goes back to ON
    const slotChoices: { value: SlotState; text: string; hint: string }[] = [
        {
            value: "mute",
            text: "MUTE",
            hint: "The sent audio plays at the same delay; the bay keeps getting the panel's sound, so its tails carry on.",
        },
        {
            value: "bypass",
            text: "BYPASS",
            hint: "The sent audio plays at the same delay; the bay gets silence and its tail cut output is on.",
        },
        {
            value: "off",
            text: "OFF",
            hint: "The sent audio plays at once, without the bay's delay (the host is told the lower latency); the bay gets silence and its tail cut output is on.",
        },
    ];

    // The most bay latency the panel takes (HS_LINK_MAX_LATENCY)
    const maxBayLatency = 32768;

    function milliseconds(samples: number, sampleRate: number): string {
        return sampleRate > 0
            ? ((1000 * samples) / sampleRate).toFixed(1)
            : "?";
    }

    function orDash(value: number | null): string {
        return value === null ? "–" : String(value);
    }

    function signed(samples: number): string {
        return `${samples > 0 ? "+" : ""}${samples}`;
    }

    function describeDifference(value: number | null): string {
        if (value === null) return "–";

        return value === 0 ? "identical" : value.toFixed(7);
    }

    function describeLink(status: LinkStatus): Line {
        const hint =
            "The panel's link to its relays, through the link hub a relay brings into the host.";

        if (status.hubProblem)
            return { parts: [v("Not linked"), `\n${status.hubProblem}`], hint };

        if (!status.name)
            return {
                parts: [
                    v("Not linked"),
                    `\n${status.linkProblem || "waiting for the host to start the audio"}`,
                ],
                hint,
            };

        return { parts: ["Linked as ", v(status.name)], hint };
    }

    function describeRelays(status: LinkStatus): Line {
        const receiver: Part[] = {
            none: [v("none")],
            linked: [v("linked"), " (not playing)"],
            playing: [v("playing")],
            returning: [v("playing")],
        }[status.receiver];

        const transmitter: Part[] = {
            none: [v("none")],
            linked: [v("linked"), " (not returning)"],
            playing: [v("returning")],
            returning: [v("returning")],
        }[status.transmitter];

        return {
            parts: [
                "receiver: ",
                ...receiver,
                "\ntransmitter: ",
                ...transmitter,
            ],
            hint: "The receiver plays the panel's sound into the bay; the transmitter returns the bay's sound.",
        };
    }

    function describeTransport(status: LinkStatus): Line {
        const parts: Part[] = [
            v(
                status.transport === "unknown"
                    ? "not reported"
                    : status.transport,
            ),
        ];

        if (status.hostTempo > 0)
            parts.push(
                " at ",
                v(Math.round(status.hostTempo * 100) / 100),
                " BPM",
            );

        if (status.outputQuiet) parts.push("\noutput ", v("quiet"));

        return {
            parts,
            hint: "The host's transport and tempo, as it reports them. Output quiet: below the quiet level for the quiet time (Settings).",
        };
    }

    function describeRoundTrip(status: LinkStatus): Line {
        const counted = status.realBlocks
            ? `${status.surveyingBlocks ? ", watching them" : ""}\n(announced ${status.maxBlock})`
            : "";

        return {
            parts: [
                v(status.roundTrip),
                ` samples (${milliseconds(status.roundTrip, status.sampleRate)} ms, ${status.roundTripBlocks} × ${status.blockUnit})`,
                "\ncounted in ",
                v(status.realBlocks ? "real blocks" : "announced blocks"),
                counted,
            ],
            hint: "From sending audio to the bay to playing its return: blocks × the block it counts in. The host is told it as latency.",
        };
    }

    function describeAuto(status: LinkStatus): Line {
        switch (status.autoState) {
            case "off":
                return {
                    parts: [v("fixed"), " in Settings"],
                    hint: "The round trip is set in Settings, not found automatically.",
                };
            case "checking":
                return {
                    parts: ["automatic: ", v("trying 1 block")],
                    hint: "Waits for returned audio.",
                };
            case "oneBlock":
                return {
                    parts: ["automatic: ", v("1 block works")],
                    hint: "Still watching: a miss on either side falls back to 2.",
                };
            case "fellBack":
                return {
                    parts: ["automatic: ", v("fell back to 2")],
                    hint: '1 block missed. To try 1 again: wire the panel into the receiver, then press Retry 1 block (Settings) or switch on "Bay 1: retry 1-block round trip".',
                };
        }
    }

    function describeOutputDelay(status: LinkStatus): Line {
        return {
            parts: [
                v(status.delay),
                ` samples (${milliseconds(status.delay, status.sampleRate)} ms)`,
                "\nhost told ",
                v(status.latency),
            ],
            hint: `${status.slotState === "off" ? "OFF: the bay's delay is dropped." : "The round trip plus the bay latency."} The host is told the panel's whole latency.`,
        };
    }

    function describeReading(status: LinkStatus): Line {
        switch (status.reading) {
            case "none":
                return {
                    parts: [v("none")],
                    hint: "No marker arriving: the return is stamped by the receiver's position, so it counts as 0.",
                };
            case "measured":
                return {
                    parts: ["reads ", v(status.readingSamples)],
                    hint: "",
                };
            case "unreadable":
                return {
                    parts: [v("unreadable")],
                    hint: "The marker is unreadable: the return is held back.",
                };
            case "outOfRange":
                return {
                    parts: [
                        v(status.readingSamples),
                        ` samples, out of range (0 to ${maxBayLatency})`,
                    ],
                    hint: "Out of range: the return is held back.",
                };
            case "noInput":
                return {
                    parts: [v("input off")],
                    hint: "The transmitter's Marker input is off: counts as 0.",
                };
            case "held":
                return {
                    parts: [v("paused")],
                    hint: `Paused (FL refilling its compensation, or the wire cut): the return is held back for up to ${maxBayLatency} samples.`,
                };
        }
    }

    function describeBayLatency(status: LinkStatus): Line {
        const reading = describeReading(status);
        const parts: Part[] = [
            v(status.bayLatency),
            ` samples (${milliseconds(status.bayLatency, status.sampleRate)} ms) in use`,
            "\nmarker: ",
            ...reading.parts,
        ];

        if (status.settlingTo !== null)
            parts.push(
                "\nsettling to ",
                v(status.settlingTo),
                ` (held ${status.settledMs} of ${status.settleMs} ms)`,
            );

        if (status.readingLowest !== null && status.readingHighest !== null)
            parts.push(
                "\nreadings ",
                v(
                    status.readingLowest === status.readingHighest
                        ? status.readingLowest
                        : `${status.readingLowest} to ${status.readingHighest}`,
                ),
                " since reset",
            );

        return {
            parts,
            hint: `The bay plugin's own latency, from the transmitter's position marker; a new reading must hold for the settle time (Settings) before it's used. ${reading.hint}`.trim(),
        };
    }

    function describeSlot(status: LinkStatus): Line {
        const release =
            status.tailCutReleaseMs > 0
                ? ` (after BYPASS or OFF, plus the ${status.tailCutReleaseMs} ms tail cut release)`
                : "";

        switch (status.slotState) {
            case "active":
                return status.awaitingBay
                    ? {
                          parts: [v("ON"), "\nwaiting for the bay's sound"],
                          hint: `The sent audio plays until the bay's sound is back${release}, then crossfades (${status.crossfadeMs} ms).`,
                      }
                    : {
                          parts: [
                              v("ON"),
                              `\nthe bay's sound plays (crossfade ${status.crossfadeMs} ms)`,
                          ],
                          hint: "",
                      };
            case "mute":
                return {
                    parts: [v("MUTE"), "\nthe sent audio plays"],
                    hint: slotChoices[0].hint,
                };
            case "bypass":
                return {
                    parts: [v("BYPASS"), "\nthe sent audio plays"],
                    hint: slotChoices[1].hint,
                };
            case "off":
                return {
                    parts: [v("OFF"), "\nthe sent audio plays at once"],
                    hint: slotChoices[2].hint,
                };
        }
    }

    function describeLag(lag: Lag): Line {
        switch (lag.state) {
            case "notMeasured":
                return { parts: [v("not measured")], hint: "" };
            case "measuring":
                return {
                    parts: [v("measuring…")],
                    hint: "The output is muted meanwhile.",
                };
            case "noReturn":
                return {
                    parts: [v("too little audio came back")],
                    hint: "Are both relays linked?",
                };
            case "noMatch":
                return {
                    parts: [
                        v("no match"),
                        ` (−${lag.earliest} to +${lag.latest} samples)`,
                    ],
                    hint: "The burst didn't come back recognisably in that range.",
                };
            case "found": {
                if (lag.exact && lag.paths.length === 1)
                    return {
                        parts: [
                            v(signed(lag.paths[0].samples)),
                            " samples (an exact copy)",
                        ],
                        hint: "",
                    };

                const parts = lag.paths.flatMap((path, index): Part[] => [
                    index > 0 ? "\n" : "",
                    v(signed(path.samples)),
                    ` samples (gain ${path.gain.toFixed(2)})`,
                ]);

                return {
                    parts,
                    hint:
                        lag.paths.length > 1
                            ? "Several paths through the bay, the strongest first."
                            : "",
                };
            }
        }
    }

    function describeProbe(
        probe: Probe,
        sampleRate: number,
        maxBlock: number,
    ): Line {
        switch (probe.state) {
            case "notRun":
                return { parts: [v("not run")], hint: "" };
            case "running":
                return {
                    parts: [v("running…")],
                    hint: "The output is muted meanwhile.",
                };
            case "noReturn":
                return {
                    parts: [v("too little audio came back")],
                    hint: "Are both relays linked?",
                };
            case "noChange":
                return {
                    parts: [
                        v("no change"),
                        ` (before ${probe.before.toFixed(4)}, after ${probe.after.toFixed(4)})`,
                    ],
                    hint: "The return's level didn't follow control 1: link control 1 to a level knob in the bay.",
                };
            case "found": {
                const blocks =
                    maxBlock > 0 ? (probe.delay / maxBlock).toFixed(2) : "?";

                return {
                    parts: [
                        v(signed(probe.delay)),
                        ` samples (${milliseconds(probe.delay, sampleRate)} ms, ${blocks} blocks) after the step`,
                        "\n10–90% in ",
                        v(probe.rise),
                        ` samples (${milliseconds(probe.rise, sampleRate)} ms)`,
                    ],
                    hint: "",
                };
            }
        }
    }

    function withHint(line: Line, about: string): Line {
        return {
            parts: line.parts,
            hint: line.hint ? `${about} ${line.hint}` : about,
        };
    }

    // An event line: its time stamp, then what happened
    function splitEvent(line: string): { time: string; text: string } {
        const space = line.indexOf(" ");

        return space > 0
            ? { time: line.slice(0, space), text: line.slice(space) }
            : { time: "", text: line };
    }
</script>

{#snippet text(
    parts: Part[],
)}{#each parts as part, index (index)}{#if typeof part === "string"}{part}{:else}<span
                class="v">{part.v}</span
            >{/if}{/each}{/snippet}

{#snippet row(label: string, line: Line, id: string)}
    <div class="row" use:hinted={line.hint}>
        <dt>{label}</dt>
        <dd id={id || undefined}>{@render text(line.parts)}</dd>
    </div>
{/snippet}

{#if link.status}
    {@const status = link.status}
    {@const slot = describeSlot(status)}
    {@const lag = withHint(
        describeLag(status.lag),
        "How late the bay returns what it's sent.",
    )}
    {@const probe = withHint(
        describeProbe(status.probe, status.sampleRate, status.maxBlock),
        "How long a step on control 1 takes to show in the return.",
    )}
    <section class="link">
        <h1>Bay link (test build)</h1>

        <button
            type="button"
            class="reset"
            use:hinted={"Resets the counters only: missing and held-back blocks, headroom, block sizes, readings, return vs sent."}
            onclick={resetLinkCounters}>Reset counters</button
        >

        <dl>
            {@render row("Link", describeLink(status), "link-state")}
            {@render row("Relays", describeRelays(status), "link-relays")}
            {@render row(
                "Transport",
                describeTransport(status),
                "link-transport",
            )}
        </dl>

        <dl>
            {@render row("Round trip", describeRoundTrip(status), "")}
            {@render row("", describeAuto(status), "link-auto")}
            {@render row(
                "Headroom",
                {
                    parts: [
                        "now ",
                        v(orDash(status.headroomNow)),
                        ", lowest ",
                        v(orDash(status.headroomLowest)),
                        " samples",
                    ],
                    hint: "Samples to spare before the return would be late: now, and the lowest since the counters were reset.",
                },
                "",
            )}
            {@render row(
                "Block sizes",
                {
                    parts: [
                        v(orDash(status.blockMin)),
                        " to ",
                        v(orDash(status.blockMax)),
                        " samples",
                    ],
                    hint: "The smallest and largest blocks since the counters were reset.",
                },
                "",
            )}
            {@render row(
                "Output delay",
                describeOutputDelay(status),
                "link-delay",
            )}
            {@render row(
                "Bay latency",
                describeBayLatency(status),
                "link-bay-latency",
            )}
        </dl>

        <dl>
            {@render row(
                "Missing blocks",
                {
                    parts: [
                        v(status.missing),
                        ` (late ${status.missingLate}, gone ${status.missingGone})`,
                    ],
                    hint: `Return blocks that didn't arrive in time. Late: not returned yet; gone: no longer there. Dropouts play ${status.dropouts === "safe" ? "the sent audio" : "silence"}.`,
                },
                "link-missing",
            )}
            {@render row(
                "Receiver missed",
                {
                    parts: [v(status.receiverMisses), " blocks"],
                    hint: "Blocks the receiver missed: each leaves a gap in the return.",
                },
                "link-receiver-missed",
            )}
            {@render row(
                "Held back",
                {
                    parts: [
                        v(status.heldBack),
                        " while a new bay latency settled\n",
                        v(status.heldStamped),
                        " stamped by the receiver's position\n",
                        v(status.heldByTransmitter),
                        " by the transmitter",
                    ],
                    hint: "Return blocks held back rather than played. By the transmitter: its marker reading unreadable, out of range or paused.",
                },
                "link-held-back",
            )}
        </dl>

        {#if status.replaced > 0 || status.oversized > 0}
            <p class="warning">
                Silenced: {status.replaced} returned samples that weren't numbers,
                {status.oversized} blocks longer than the host announced
            </p>
        {/if}

        <dl>
            <dt>Slot</dt>
            <dd class="choices">
                {#each slotChoices as choice (choice.value)}
                    <button
                        id="link-slot-{choice.value}"
                        type="button"
                        class="toggle"
                        aria-pressed={status.slotState === choice.value}
                        use:hinted={choice.hint}
                        onclick={() =>
                            setBaySlotState(
                                status.slotState === choice.value
                                    ? "active"
                                    : choice.value,
                            )}>{choice.text}</button
                    >
                {/each}
            </dd>

            <div class="row" use:hinted={slot.hint}>
                <dt></dt>
                <dd id="link-slot-note" class:warning={status.awaitingBay}>
                    {@render text(slot.parts)}
                </dd>
            </div>

            {@render row(
                "Return vs sent",
                {
                    parts: [
                        "now ",
                        v(describeDifference(status.differenceNow)),
                        ", largest ",
                        v(describeDifference(status.differenceLargest)),
                    ],
                    hint: "How far the returned audio is from the audio sent: a pass-through bay gives identical.",
                },
                "link-difference",
            )}

            <div class="row" use:hinted={lag.hint}>
                <dt>Lag</dt>
                <dd class="choices">
                    <span id="link-lag">{@render text(lag.parts)}</span>
                    <button
                        type="button"
                        use:hinted={"Sends a short noise burst through the bay instead of the panel's sound (output muted) and finds every delay at which it comes back. Nothing needs to be playing."}
                        onclick={measureLag}>Measure</button
                    >
                </dd>
            </div>

            {@render row(
                "Controls",
                {
                    parts: [v(status.valueSets), " value sets sent"],
                    hint: "Sent when a value changes.",
                },
                "",
            )}

            <div class="row" use:hinted={probe.hint}>
                <dt>Control timing</dt>
                <dd class="choices">
                    <span id="link-probe">{@render text(probe.parts)}</span>
                    <button
                        type="button"
                        use:hinted={"Sends a steady tone instead of the panel's sound, holds control 1 at 0, steps it to 100%, and times the change in the return. Control 1 must drive a level knob in the bay."}
                        onclick={startProbe}>Probe</button
                    >
                </dd>
            </div>
        </dl>

        <h2 use:hinted={"Newest first."}>Events</h2>
        <ul id="link-events" class="events">
            {#each status.events as line, index (index)}
                {@const event = splitEvent(line)}
                <li>
                    <span class="time">{event.time}</span><span class="v"
                        >{event.text}</span
                    >
                </li>
            {:else}
                <li>none yet</li>
            {/each}
        </ul>
    </section>
{:else}
    <p class="waiting">No bay link status from the plugin</p>
{/if}

<style>
    /* One label column for every group (each group's grid is a subgrid of this one) */
    .link {
        display: grid;
        grid-template-columns: max-content 1fr;
        column-gap: 12px;
    }

    .link > * {
        grid-column: 1 / -1;
    }

    h1 {
        margin: 0 0 8px;
    }

    h2 {
        margin: 12px 0 4px;
    }

    .reset {
        justify-self: start;
        margin-bottom: 8px;
    }

    /* Each group of lines, with a divider above it */
    dl {
        display: grid;
        grid-template-columns: subgrid;
        row-gap: 3px;
        margin: 0;
        padding: 8px 0;
        border-top: 1px solid rgba(255, 255, 255, 0.15);
        color: var(--mid-gray);
    }

    dt {
        text-align: right;
    }

    /* A line's label and value, hinted together */
    .row {
        display: contents;
    }

    dd {
        margin: 0;
        font-variant-numeric: tabular-nums;
        overflow-wrap: anywhere;
        white-space: pre-line;
    }

    .v {
        color: #fff;
    }

    .choices {
        display: flex;
        align-items: center;
        gap: 14px;
        white-space: normal;
    }

    .choices button {
        flex-shrink: 0;
    }

    .choices span {
        white-space: pre-line;
    }

    .warning {
        margin: 0 0 8px;
        color: var(--warning);
    }

    dd.warning {
        margin: 0;
    }

    dd.warning .v {
        color: inherit;
    }

    .toggle[aria-pressed="true"] {
        background: var(--accent);
        color: #000;
    }

    .events {
        margin: 0;
        padding: 0;
        list-style: none;
        color: var(--mid-gray);
        font-variant-numeric: tabular-nums;
        overflow-wrap: anywhere;
    }

    .time {
        color: var(--mid-gray);
    }

    .waiting {
        margin: 0;
        opacity: 0.7;
    }
</style>
