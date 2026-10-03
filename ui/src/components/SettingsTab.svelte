<script lang="ts">
    import { view } from "../lib/display.svelte";
    import {
        changeSetting,
        crossfadeRange,
        editor,
        resetSize,
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

    const resizings: { value: Settings["resizing"]; text: string }[] = [
        { value: "smooth", text: "Smooth" },
        { value: "sharp", text: "Sharp steps (1-pixel lines stay crisp)" },
    ];

    const anchors: { value: Settings["resizeAnchor"]; text: string }[] = [
        { value: "oppositeCorner", text: "Opposite corner" },
        { value: "windowCenter", text: "Window center" },
    ];

    const dropoutModes: {
        value: Settings["bayDropouts"];
        text: string;
    }[] = [
        {
            value: "safe",
            text: "Safe (plays the audio that was sent, so only the bay is skipped)",
        },
        { value: "silence", text: "Silence (makes dropouts audible)" },
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
    <legend>Center the window when it first opens on</legend>
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
    <legend>Resizing</legend>
    {#each resizings as option (option.value)}
        <label class="option">
            <input
                type="radio"
                name="resizing"
                checked={editor.settings.resizing === option.value}
                onchange={() => changeSetting("resizing", option.value)}
            />
            {option.text}
        </label>
    {/each}
</fieldset>

<fieldset>
    <legend>Resize from</legend>
    {#each anchors as option (option.value)}
        <label class="option">
            <input
                type="radio"
                name="resizeAnchor"
                checked={editor.settings.resizeAnchor === option.value}
                onchange={() => changeSetting("resizeAnchor", option.value)}
            />
            {option.text}
        </label>
    {/each}
</fieldset>

<fieldset>
    <legend>Bay dropouts (missing return blocks)</legend>
    {#each dropoutModes as option (option.value)}
        <label class="option">
            <input
                type="radio"
                name="bayDropouts"
                checked={editor.settings.bayDropouts === option.value}
                onchange={() => changeSetting("bayDropouts", option.value)}
            />
            {option.text}
        </label>
    {/each}
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
        ms ({crossfadeRange.min} to {crossfadeRange.max}; 0 is off)
    </label>
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

    .size {
        display: flex;
        align-items: center;
        justify-content: space-between;
        margin-top: 14px;
    }
</style>
