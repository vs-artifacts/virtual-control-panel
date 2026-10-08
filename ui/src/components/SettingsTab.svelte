<script lang="ts">
    import { view } from "../lib/display.svelte";
    import {
        changeSetting,
        crossfadeRange,
        doubleClickRange,
        editor,
        releaseRange,
        resetSize,
        settleRange,
        type Settings,
    } from "../lib/editor.svelte";

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

<fieldset>
    <legend>Center a separate window when it first opens on</legend>
    {#each placements as option (option.value)}
        <label class="option">
            <input
                type="radio"
                name="editorPlacement"
                checked={editor.settings.editorPlacement === option.value}
                onchange={() => changeSetting("editorPlacement", option.value)}
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
                onchange={() => changeSetting("resizeRendering", option.value)}
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
        window's center (separate windows, and windows attached inside FL). Add
        Ctrl for 25% steps.
    </p>
</fieldset>

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

<fieldset>
    <legend>Bay round trip (the host is told it as latency)</legend>
    {#each roundTrips as option (option.value)}
        <label class="option">
            <input
                type="radio"
                name="roundTripBlocks"
                checked={editor.settings.roundTripBlocks === option.value}
                onchange={() => changeSetting("roundTripBlocks", option.value)}
            />
            {option.text}
        </label>
    {/each}
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
        ms ({crossfadeRange.min} to {crossfadeRange.max}; 0 is off). Also blends
        the jump when the bay's delay changes.
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
        After a bay leaves Bypass or Off, its sound is held back this much
        longer (the sent audio plays on), since a plugin cleared through the
        relay's Tail cut output reacts some ms late.
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

    .size {
        display: flex;
        align-items: center;
        justify-content: space-between;
        margin-top: 14px;
    }
</style>
