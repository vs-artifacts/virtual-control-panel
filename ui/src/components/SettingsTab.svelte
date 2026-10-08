<script lang="ts">
    import { sendRecheckBlockSize, sendRetryOneBlock } from "../lib/bridge";
    import { view } from "../lib/display.svelte";
    import {
        changeSetting,
        crossfadeRange,
        doubleClickRange,
        editor,
        jumpCrossfadeRange,
        lowestTempoRange,
        quietLevelRange,
        quietMsRange,
        releaseRange,
        resetSize,
        settleRange,
        type Settings,
    } from "../lib/editor.svelte";
    import { link } from "../lib/link.svelte";

    const placements: { value: Settings["editorPlacement"]; text: string }[] = [
        { value: "off", text: "Off" },
        { value: "hostPlaced", text: "The monitor the host opened it on" },
        { value: "mouse", text: "The monitor under the mouse" },
        {
            value: "hostMainWindow",
            text: "The monitor with the host's main window",
        },
    ];

    const attachedPlacements: {
        value: Settings["attachedPlacement"];
        text: string;
    }[] = [
        { value: "off", text: "Off" },
        { value: "workspace", text: "Centered in FL's workspace" },
        {
            value: "hostMonitor",
            text: "Centered on the monitor where FL is open (kept inside the workspace)",
        },
    ];

    const renderings: { value: Settings["resizeRendering"]; text: string }[] = [
        {
            value: "smooth",
            text: "Smooth (scales like a picture: everything keeps its proportions, text included; soft while resizing, sharp again when it stops)",
        },
        {
            value: "sharp",
            text: "Sharp (laid out again at every size: crisp, but elements can shift by a pixel)",
        },
    ];

    const roundTrips: { value: Settings["roundTripBlocks"]; text: string }[] = [
        {
            value: 0,
            text: "Automatic (1 block if the receiver runs after the panel, otherwise 2; checked when relays link)",
        },
        {
            value: 1,
            text: "1 block (the receiver must run after the panel)",
        },
        { value: 2, text: "2 blocks" },
        { value: 3, text: "3 blocks" },
    ];

    const stoppedShrinks: {
        value: Settings["bayStoppedShrink"];
        text: string;
    }[] = [
        { value: "never", text: "Never" },
        {
            value: "whenQuiet",
            text: "Only when the panel's output is quiet too (below)",
        },
        { value: "always", text: "Always" },
    ];

    // At 2 or 3 blocks each change of the real block size makes the receiver line up again: a gap inside the bay
    const tempoDropsPop = $derived(
        editor.settings.bayRealBlocks &&
            editor.settings.bayLowestTempo === 0 &&
            (link.status?.roundTripBlocks ?? 0) > 1,
    );

    // Whole numbers kept in range; anything else puts the field back
    function changeWhole(
        event: Event & { currentTarget: HTMLInputElement },
        name: "bayQuietLevelDb" | "bayQuietMs" | "bayJumpCrossfadeMs",
        range: { min: number; max: number },
    ) {
        const typed = event.currentTarget.valueAsNumber;

        if (Number.isFinite(typed))
            changeSetting(
                name,
                Math.min(range.max, Math.max(range.min, Math.round(typed))),
            );

        event.currentTarget.value = String(editor.settings[name]);
    }

    // Typed values are rounded and kept in range; anything else puts the field back
    function changeCrossfade(
        event: Event & { currentTarget: HTMLInputElement },
    ) {
        const typed = event.currentTarget.valueAsNumber;

        if (Number.isFinite(typed)) {
            const milliseconds = Math.min(
                crossfadeRange.max,
                Math.max(crossfadeRange.min, Math.round(typed)),
            );
            changeSetting("bypassCrossfadeMs", milliseconds);
        }

        event.currentTarget.value = String(editor.settings.bypassCrossfadeMs);
    }

    function changeSettle(event: Event & { currentTarget: HTMLInputElement }) {
        const typed = event.currentTarget.valueAsNumber;

        if (Number.isFinite(typed)) {
            const milliseconds = Math.min(
                settleRange.max,
                Math.max(settleRange.min, Math.round(typed)),
            );
            changeSetting("bayLatencySettleMs", milliseconds);
        }

        event.currentTarget.value = String(editor.settings.bayLatencySettleMs);
    }

    function changeRelease(event: Event & { currentTarget: HTMLInputElement }) {
        const typed = event.currentTarget.valueAsNumber;

        if (Number.isFinite(typed)) {
            const milliseconds = Math.min(
                releaseRange.max,
                Math.max(releaseRange.min, Math.round(typed)),
            );
            changeSetting("bayTailCutReleaseMs", milliseconds);
        }

        event.currentTarget.value = String(editor.settings.bayTailCutReleaseMs);
    }

    // An empty field is no floor (0); anything else is kept in range, to a tenth of a BPM
    function changeLowestTempo(
        event: Event & { currentTarget: HTMLInputElement },
    ) {
        const field = event.currentTarget;
        const typed = field.value.trim() === "" ? 0 : field.valueAsNumber;

        if (Number.isFinite(typed)) {
            const bpm = Math.min(
                lowestTempoRange.max,
                Math.max(lowestTempoRange.min, Math.round(typed * 10) / 10),
            );
            changeSetting("bayLowestTempo", bpm);
        }

        field.value =
            editor.settings.bayLowestTempo > 0
                ? String(editor.settings.bayLowestTempo)
                : "";
    }

    function changeDoubleClick(
        event: Event & { currentTarget: HTMLInputElement },
    ) {
        const typed = event.currentTarget.valueAsNumber;

        if (Number.isFinite(typed)) {
            const milliseconds = Math.min(
                doubleClickRange.max,
                Math.max(doubleClickRange.min, Math.round(typed)),
            );
            changeSetting("doubleClickMs", milliseconds);
        }

        event.currentTarget.value = String(editor.settings.doubleClickMs);
    }
</script>

<p class="title">{editor.title}</p>

<label class="option">
    <input
        type="checkbox"
        checked={editor.settings.keepWebViewWarm}
        onchange={(event) =>
            changeSetting("keepWebViewWarm", event.currentTarget.checked)}
    />
    Keep web UI warm (reopens instantly, uses more memory)
</label>

<label class="option">
    <input
        type="checkbox"
        checked={editor.settings.flOutputCorrection}
        onchange={(event) =>
            changeSetting("flOutputCorrection", event.currentTarget.checked)}
    />
    Correct FL Studio's gain on our control outputs (x1.283291, FL only)
</label>

<label class="option">
    <input
        type="checkbox"
        checked={editor.settings.resizeBlocksLanguageSwitch}
        onchange={(event) =>
            changeSetting(
                "resizeBlocksLanguageSwitch",
                event.currentTarget.checked,
            )}
    />
    While a window corner is dragged, stop Shift with Alt or Ctrl from switching
    Windows' input language (Windows only)
</label>

<div class="group" id="window-settings">
    <fieldset>
        <legend>Center a separate window when it first opens on</legend>
        {#each placements as option (option.value)}
            <label class="option">
                <input
                    type="radio"
                    name="editorPlacement"
                    checked={editor.settings.editorPlacement === option.value}
                    onchange={() =>
                        changeSetting("editorPlacement", option.value)}
                />
                {option.text}
            </label>
        {/each}
    </fieldset>

    <fieldset>
        <legend
            >Center a window attached inside FL Studio when it first opens</legend
        >
        {#each attachedPlacements as option (option.value)}
            <label class="option">
                <input
                    type="radio"
                    name="attachedPlacement"
                    checked={editor.settings.attachedPlacement === option.value}
                    onchange={() =>
                        changeSetting("attachedPlacement", option.value)}
                />
                {option.text}
            </label>
        {/each}
    </fieldset>

    <fieldset>
        <legend>Resizing</legend>
        {#each renderings as option (option.value)}
            <label class="option">
                <input
                    type="radio"
                    name="resizeRendering"
                    checked={editor.settings.resizeRendering === option.value}
                    onchange={() =>
                        changeSetting("resizeRendering", option.value)}
                />
                {option.text}
            </label>
        {/each}
        <label class="option">
            <input
                type="checkbox"
                checked={editor.settings.resizeFlipAlt}
                onchange={(event) =>
                    changeSetting("resizeFlipAlt", event.currentTarget.checked)}
            />
            Window resize: flip drag and alt+drag
        </label>
        <p class="hint">
            Corner grips: drag keeps the opposite corner in place, Alt+drag the
            window's center (separate windows, and windows attached inside FL).
            Add Ctrl for 25% steps. Shift sets width and height apart: the page
            keeps its scale and shows more or less (with Ctrl, each in 25% steps
            of the design size).
        </p>
    </fieldset>

    <div class="size">
        <span
            >Size: {view.width} x {view.height} ({Math.round(
                view.uiScale * 100,
            )}%)</span
        >
        <button type="button" onclick={resetSize}>Reset size</button>
    </div>
</div>

<fieldset>
    <legend>Double-click on a control</legend>
    <label class="option">
        <input
            id="double-click-ms"
            class="milliseconds"
            type="number"
            min={doubleClickRange.min}
            max={doubleClickRange.max}
            step="1"
            value={editor.settings.doubleClickMs}
            onchange={changeDoubleClick}
        />
        ms at most between the clicks ({doubleClickRange.min} to {doubleClickRange.max})
    </label>
    <label class="option">
        <input
            type="checkbox"
            checked={editor.settings.doubleClickSets}
            onchange={(event) =>
                changeSetting("doubleClickSets", event.currentTarget.checked)}
        />
        Double-click sets a value instead of resetting to the default (not built
        yet: still resets)
    </label>
</fieldset>

<div class="group" id="bay-settings">
    <fieldset>
        <legend>Bay round trip (the host is told it as latency)</legend>
        {#each roundTrips as option (option.value)}
            <div class="line">
                <label class="option">
                    <input
                        type="radio"
                        name="roundTripBlocks"
                        checked={editor.settings.roundTripBlocks ===
                            option.value}
                        onchange={() =>
                            changeSetting("roundTripBlocks", option.value)}
                    />
                    {option.text}
                </label>
                {#if option.value === 0}
                    <button
                        type="button"
                        id="retry-one-block"
                        disabled={editor.settings.roundTripBlocks !== 0}
                        onclick={sendRetryOneBlock}>Retry 1 block</button
                    >
                {/if}
            </div>
        {/each}
        <div class="line">
            <button
                type="button"
                id="recheck-block-size"
                disabled={!editor.settings.bayRealBlocks}
                onclick={sendRecheckBlockSize}>Recheck real block size</button
            >
        </div>
        <p class="hint">
            The host can automate both: "Bay 1: retry 1-block round trip" and
            "Bays: recheck block size" (each acts when switched on). A recheck
            watches the blocks afresh, so the round trip can shrink as well as
            grow.
        </p>
    </fieldset>

    <fieldset>
        <legend>Bay round trip counted in</legend>
        <label class="option">
            <input
                type="radio"
                name="bayRealBlocks"
                checked={editor.settings.bayRealBlocks}
                onchange={() => changeSetting("bayRealBlocks", true)}
            />
            Real blocks (FL: one sequencer tick each, far below the buffer length;
            grown ahead of need, 25% to spare)
        </label>
        <label class="option">
            <input
                type="radio"
                name="bayRealBlocks"
                checked={!editor.settings.bayRealBlocks}
                onchange={() => changeSetting("bayRealBlocks", false)}
            />
            Announced blocks (the host's buffer length)
        </label>
        <label class="option">
            Lowest tempo
            <input
                id="lowest-tempo"
                class="milliseconds"
                type="number"
                min="1"
                max={lowestTempoRange.max}
                step="any"
                placeholder="none"
                value={editor.settings.bayLowestTempo > 0
                    ? editor.settings.bayLowestTempo
                    : ""}
                onchange={changeLowestTempo}
            />
            BPM (empty: none)
        </label>
        <p class="hint">
            Real blocks start at one tick at the lowest tempo, so a project that
            slows down later needn't grow the round trip while playing. Setting
            it rechecks the block size.
        </p>
        {#if tempoDropsPop}
            <p class="warning" id="tempo-drop-warning">
                At 2 or 3 blocks, tempo drops can pop inside the bay (each
                growth of the round trip makes the receiver line up again). Set
                a lowest tempo to avoid it.
            </p>
        {/if}
    </fieldset>

    <fieldset>
        <legend
            >Real blocks: shrink the round trip while the transport is stopped</legend
        >
        {#each stoppedShrinks as option (option.value)}
            <label class="option">
                <input
                    type="radio"
                    name="bayStoppedShrink"
                    checked={editor.settings.bayStoppedShrink === option.value}
                    onchange={() =>
                        changeSetting("bayStoppedShrink", option.value)}
                />
                {option.text}
            </label>
        {/each}
        <label class="option">
            Quiet: below
            <input
                id="quiet-level"
                class="milliseconds"
                type="number"
                min={quietLevelRange.min}
                max={quietLevelRange.max}
                step="1"
                value={editor.settings.bayQuietLevelDb}
                onchange={(event) =>
                    changeWhole(event, "bayQuietLevelDb", quietLevelRange)}
            />
            dBFS for
            <input
                id="quiet-ms"
                class="milliseconds"
                type="number"
                min={quietMsRange.min}
                max={quietMsRange.max}
                step="1"
                value={editor.settings.bayQuietMs}
                onchange={(event) =>
                    changeWhole(event, "bayQuietMs", quietMsRange)}
            />
            ms
        </label>
        <p class="hint">
            While stopped, the block size is checked again and again, so the
            round trip follows a faster tempo. Playing, it only grows.
        </p>
    </fieldset>

    <fieldset>
        <legend>Bay delay change crossfade</legend>
        <label class="option">
            <input
                id="jump-crossfade"
                class="milliseconds"
                type="number"
                min={jumpCrossfadeRange.min}
                max={jumpCrossfadeRange.max}
                step="1"
                value={editor.settings.bayJumpCrossfadeMs}
                onchange={(event) =>
                    changeWhole(
                        event,
                        "bayJumpCrossfadeMs",
                        jumpCrossfadeRange,
                    )}
            />
            ms ({jumpCrossfadeRange.min} to {jumpCrossfadeRange.max}; 0 is off).
            Blends the old timing into the new when the round trip or the bay
            latency changes.
        </label>
    </fieldset>

    <fieldset>
        <legend>Bay bypass crossfade</legend>
        <label class="option">
            <input
                id="bypass-crossfade"
                class="milliseconds"
                type="number"
                min={crossfadeRange.min}
                max={crossfadeRange.max}
                step="1"
                value={editor.settings.bypassCrossfadeMs}
                onchange={changeCrossfade}
            />
            ms ({crossfadeRange.min} to {crossfadeRange.max}; 0 is off). Also
            blends the jump when the slot goes Off or back, fades the bay's send
            for Bypass and Off, and is the length of a dropout's fade.
        </label>
    </fieldset>

    <fieldset>
        <legend>Bay latency settle time</legend>
        <label class="option">
            <input
                id="latency-settle"
                class="milliseconds"
                type="number"
                min={settleRange.min}
                max={settleRange.max}
                step="1"
                value={editor.settings.bayLatencySettleMs}
                onchange={changeSettle}
            />
            ms ({settleRange.min} to {settleRange.max})
        </label>
        <p class="hint">
            A new bay latency must hold this long before the panel uses it;
            meanwhile the bay's sound is held back (silence, faded).
        </p>
    </fieldset>

    <fieldset>
        <legend>Bay tail cut release</legend>
        <label class="option">
            <input
                id="tail-cut-release"
                class="milliseconds"
                type="number"
                min={releaseRange.min}
                max={releaseRange.max}
                step="1"
                value={editor.settings.bayTailCutReleaseMs}
                onchange={changeRelease}
            />
            ms ({releaseRange.min} to {releaseRange.max})
        </label>
        <p class="hint">
            After a bay leaves Bypass or Off, the audio sent to it stays silent
            this much longer before it fades in, so a plugin cleared through the
            relay's Tail cut output (which reacts some ms late) comes back on
            silence. The panel's own sound plays on until the bay's sound is
            back.
        </p>
    </fieldset>
</div>

<style>
    p {
        margin: 0 0 8px;
    }

    .title {
        font-size: 16px;
        font-weight: 500;
    }

    fieldset {
        margin: 10px 0 0;
        padding: 0;
        border: none;
    }

    legend {
        padding: 0 0 2px;
        font-weight: 500;
    }

    .option {
        display: flex;
        align-items: center;
        gap: 6px;
        padding: 1px 0;
    }

    input {
        margin: 0;
    }

    .milliseconds {
        width: 4em;
    }

    .hint {
        margin: 2px 0 0;
        opacity: 0.7;
    }

    .warning {
        margin: 4px 0 0;
        color: #ffb36b;
    }

    .line {
        display: flex;
        align-items: center;
        gap: 8px;
    }

    .size {
        display: flex;
        align-items: center;
        justify-content: space-between;
        margin-top: 14px;
    }

    .group {
        margin: 14px 0;
        padding: 4px 0 12px;
        border-top: 1px solid rgba(255, 255, 255, 0.3);
        border-bottom: 1px solid rgba(255, 255, 255, 0.3);
    }
</style>
