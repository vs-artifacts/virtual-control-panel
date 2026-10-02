<script lang="ts">
    import { view } from "../lib/display.svelte";
    import {
        changeSetting,
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

    .size {
        display: flex;
        align-items: center;
        justify-content: space-between;
        margin-top: 14px;
    }
</style>
